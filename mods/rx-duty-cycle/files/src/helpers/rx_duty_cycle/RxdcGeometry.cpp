#include <helpers/rx_duty_cycle/RxdcGeometry.h>

#include <math.h>

#define MAX_TICKS 0xFFFFFFUL

float rxdcSymbolUs(uint8_t sf, float bw_khz) {
  return 1000.0f * (float)(1UL << sf) / bw_khz;
}

uint32_t rxdcReloadTicks(uint32_t rx_ticks, uint32_t sleep_ticks) {
  return (sleep_ticks + 2 * (rx_ticks & 0xFFFF) + ((rx_ticks & 63) << 18)) & MAX_TICKS;
}

uint32_t rxdcNeedTicks(uint8_t sf, float bw_khz, uint8_t preamble) {
  float sync = sf <= 6 ? 6.25f : 4.25f;
  return (uint32_t)ceilf((preamble + sync + RXDC_HEADER_SYMBOLS + RXDC_SLACK_SYMBOLS) * rxdcSymbolUs(sf, bw_khz) / RXDC_TICK_US);
}

bool rxdcGeometry(uint8_t sf, float bw_khz, uint8_t preamble, uint32_t transition_us, RxdcGeometry* out) {
  if (out == nullptr || sf < 5 || sf > 12 || !(bw_khz > 0) || preamble <= RXDC_CATCH_SYMBOLS) return false;
  float sym = rxdcSymbolUs(sf, bw_khz);
  float sleep = (preamble - RXDC_CATCH_SYMBOLS - RXDC_PAD_SYMBOLS) * sym;
  if (sleep < (float)(transition_us + RXDC_SLEEP_MARGIN_US) || sleep > 0.9f * MAX_TICKS * RXDC_TICK_US) return false;
  uint32_t sleep_us = (uint32_t)sleep;
  // RadioLib subtracts the transition and truncates to ticks; the timer works on what is left.
  uint32_t sleep_ticks = (uint32_t)((uint64_t)(sleep_us - transition_us) * 8 / 125);
  uint32_t need = rxdcNeedTicks(sf, bw_khz, preamble);
  // The window starts from the datasheet rule (2*rx + sleep covers the header); the low six bits then lengthen the timer where needed.
  uint32_t base = need > sleep_ticks ? (need - sleep_ticks + 1) / 2 : 0;
  uint32_t min_rx = (uint32_t)ceilf(sym / RXDC_TICK_US);
  if (base < min_rx) base = min_rx;
  uint32_t rx = ((base + 62) & ~63UL) + 1;
  for (; rx <= MAX_TICKS && (rx & 63) <= 62; rx++) {
    uint32_t reload = rxdcReloadTicks(rx, sleep_ticks);
    if (reload >= need) {
      out->rx_us = (uint32_t)(((uint64_t)rx * 125 + 7) / 8);   // rounded up so RadioLib's truncation keeps the tick count
      out->sleep_us = sleep_us;
      out->reload_ticks = reload;
      return true;
    }
  }
  return false;
}

uint8_t rxdcSymbolTimeout(uint32_t rx_us, uint8_t sf, float bw_khz, uint8_t preamble) {
  constexpr uint8_t timeout = 96;
  const float sync = sf <= 6 ? 6.25f : 4.25f;
  const float window_symbols = rx_us / rxdcSymbolUs(sf, bw_khz);
  return window_symbols + preamble + sync + RXDC_HEADER_SYMBOLS + RXDC_SLACK_SYMBOLS <= timeout
             ? timeout : 0;
}
