#include <stdio.h>
#include <string.h>
#include <helpers/CoreIntegration.h>

static bool isStatsCommand(const char* command) {
  static const char* const names[] = { "stats-core", "stats-radio", "stats-packets" };
  for (const char* name : names) {
    size_t len = strlen(name);
    if (memcmp(command, name, len) == 0 && (command[len] == 0 || command[len] == ' ')) return true;
  }
  return false;
}

bool coreHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (coreProbeCli(context, command, reply)) return true;
  // Upstream serial-gates the stats commands; re-dispatching with timestamp 0 clears that gate. Safe remotely: the mesh CLI is authenticated and the replies are read-only.
  if (context.sender_timestamp != 0 && isStatsCommand(command)) {
    modCliDispatch(0, command, reply);
    return true;
  }
  // Every MobMesh build names its own build after upstream's, whichever mods it carries.
  if (memcmp(command, "ver", 3) == 0) {
    sprintf(reply, "%s (%s) + MobMesh (%s)", context.fw_version, context.fw_build_date, OTA_MOD_BUILD_DATE);
    return true;
  }
  // Upstream's poweroff never wakes, so it is never reachable; a mod that can wake handles it first.
  if (memcmp(command, "poweroff", 8) == 0 || memcmp(command, "shutdown", 8) == 0) {
    strcpy(reply, "ERR: poweroff not available in this build");
    return true;
  }
  return false;
}
