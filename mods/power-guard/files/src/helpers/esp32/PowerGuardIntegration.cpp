#include <helpers/esp32/PowerGuardIntegration.h>

#include <Arduino.h>
#include <esp_system.h>
#include <helpers/esp32/PowerGuardPolicy.h>

#ifdef P_LORA_PA_POWER
#define POWER_GUARD_HAS_POWERDOWN 1
#include <PowerGuard.h>
#endif

#ifndef POWER_GUARD_BACKOFF_MAX_SECS
#define POWER_GUARD_BACKOFF_MAX_SECS 900
#endif
#ifndef POWER_GUARD_BACKOFF_START_SECS
#define POWER_GUARD_BACKOFF_START_SECS 60
#endif
#ifndef POWER_GUARD_CLOCK_FALLBACK_EPOCH
#define POWER_GUARD_CLOCK_FALLBACK_EPOCH 1715770351UL
#endif
#ifndef POWER_GUARD_POWEROFF_MIN_SECS
#define POWER_GUARD_POWEROFF_MIN_SECS 60
#endif
#ifndef POWER_GUARD_POWEROFF_MAX_SECS
#define POWER_GUARD_POWEROFF_MAX_SECS 86400
#endif

static PowerGuardPolicy power_guard;
RTC_DATA_ATTR static uint32_t boot_pwr_check_magic;
RTC_DATA_ATTR static uint32_t boot_pwr_check_fails;

static void bootPowerCheck() {
#ifdef POWER_GUARD_HAS_POWERDOWN
  powerGuardReleaseHolds();
#endif
  if (POWER_GUARD_RESUME_MV == 0) return;

  uint16_t mv = modBoardBattMilliVolts();
  if (mv == 0) return;

  if (boot_pwr_check_magic != 0x50575243UL) {
    boot_pwr_check_magic = 0x50575243UL;
    boot_pwr_check_fails = 0;
  }
  if (mv >= POWER_GUARD_RESUME_MV) {
    boot_pwr_check_fails = 0;
    return;
  }

  uint32_t shift = boot_pwr_check_fails < 16 ? boot_pwr_check_fails : 16;
  uint32_t secs = (uint32_t)POWER_GUARD_BACKOFF_START_SECS << shift;
  if (secs > POWER_GUARD_BACKOFF_MAX_SECS) secs = POWER_GUARD_BACKOFF_MAX_SECS;
  boot_pwr_check_fails++;

  Serial.printf("batt %umV below %umV -- deep sleep %us (retry %u)\n",
                mv, (unsigned)POWER_GUARD_RESUME_MV, (unsigned)secs,
                (unsigned)boot_pwr_check_fails);
  Serial.flush();
#ifdef POWER_GUARD_HAS_POWERDOWN
  powerGuardDownPreRadio();
#endif
  modBoardDeepSleep(secs);
}

static void bootClockGuard() {
  if (esp_reset_reason() != ESP_RST_BROWNOUT) return;
  uint32_t now = modClockGet();
  if (now == POWER_GUARD_CLOCK_FALLBACK_EPOCH) return;
  Serial.printf("brownout reset -- clock %u rebased to %u\n",
                (unsigned)now, (unsigned)POWER_GUARD_CLOCK_FALLBACK_EPOCH);
  modClockSet(POWER_GUARD_CLOCK_FALLBACK_EPOCH);
}

void powerGuardBeforeRadioInit() {
  bootClockGuard();
  bootPowerCheck();
}

void powerGuardLoop() {
  power_guard.loop();
}

bool powerGuardWantsPowerSaving() {
  return power_guard.isActive();
}

bool powerGuardHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (memcmp(command, "powersaving safe.mv ", 20) == 0) {
    uint32_t mv = (uint32_t)atol(command + 20);
    if (!power_guard.setSleepMilliVolts((uint16_t)mv)) {
      sprintf(reply, "ERR: %u-%u or 0", (unsigned)POWER_GUARD_SAFE_FLOOR_MV,
              (unsigned)POWER_GUARD_SAFE_MAX_MV);
    } else if (mv == 0) {
      strcpy(reply, "OK - safe.mv 0 (rung disabled)");
    } else {
      sprintf(reply, "OK - safe.mv %umV%s", (unsigned)mv,
              power_guard.safeEnabled() ? "" : " (safe is off)");
    }
    return true;
  }

  if (memcmp(command, "powersaving safe", 16) == 0
      && (command[16] == 0 || command[16] == ' ')) {
    const char* arg = command[16] == ' ' ? command + 17 : "";
    if (*arg == 0) {
      sprintf(reply, "safe %s, %umV, batt %umV", power_guard.safeEnabled() ? "on" : "off",
              (unsigned)power_guard.sleepMilliVolts(), (unsigned)power_guard.lastMilliVolts());
    } else if (memcmp(arg, "on", 2) == 0) {
      if (power_guard.sleepMilliVolts() == 0) {
        strcpy(reply, "ERR: no threshold -- set powersaving safe.mv first");
      } else {
        power_guard.setSafeEnabled(true);
        sprintf(reply, "OK - safe on, %umV", (unsigned)power_guard.sleepMilliVolts());
      }
    } else if (memcmp(arg, "off", 3) == 0) {
      power_guard.setSafeEnabled(false);
      sprintf(reply, "OK - safe off (%umV kept)", (unsigned)power_guard.sleepMilliVolts());
    } else {
      strcpy(reply, "ERR: usage: powersaving safe [on|off]");
    }
    return true;
  }

  if (memcmp(command, "powersaving auto", 16) == 0
      && (command[16] == 0 || command[16] == ' ')) {
    const char* arg = command[16] == ' ' ? command + 17 : "";
    if (*arg == 0) {
      sprintf(reply, "auto %s, active %s, transitions %u", power_guard.isAuto() ? "on" : "off",
              power_guard.isActive() ? "yes" : "no", (unsigned)power_guard.transitionCount());
    } else if (memcmp(arg, "on", 2) == 0 || memcmp(arg, "off", 3) == 0) {
      power_guard.setAuto(*arg == 'o' && arg[1] == 'n');
      sprintf(reply, "OK - auto %s", power_guard.isAuto() ? "on" : "off");
    } else {
      strcpy(reply, "ERR: usage: powersaving auto [on|off]");
    }
    return true;
  }

  if (memcmp(command, "poweroff", 8) != 0 && memcmp(command, "shutdown", 8) != 0) {
    return false;
  }

  const char* arg = command + 8;
  uint32_t secs = *arg == ' ' ? (uint32_t)atol(arg + 1) : 0;
  if (context.sender_timestamp != 0) {
    strcpy(reply, "ERR: poweroff is serial-only");
  } else if (secs < POWER_GUARD_POWEROFF_MIN_SECS || secs > POWER_GUARD_POWEROFF_MAX_SECS) {
    sprintf(reply, "ERR: usage: poweroff <secs> (%u-%u)",
            (unsigned)POWER_GUARD_POWEROFF_MIN_SECS,
            (unsigned)POWER_GUARD_POWEROFF_MAX_SECS);
  } else {
    Serial.printf("OK - deep sleep %us (%uh%um), then reboots\n",
                  (unsigned)secs, (unsigned)(secs / 3600),
                  (unsigned)((secs % 3600) / 60));
    Serial.flush();
#ifdef POWER_GUARD_HAS_POWERDOWN
    powerGuardDownPostRadio();
#endif
    modBoardDeepSleep(secs);
  }
  return true;
}
