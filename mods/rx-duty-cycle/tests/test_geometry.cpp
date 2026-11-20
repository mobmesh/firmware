#include <helpers/rx_duty_cycle/RxdcGeometry.h>

#include "check.h"

#include <math.h>

static uint32_t ticks(uint32_t us) { return (uint32_t)((uint64_t)us * 8 / 125); }

int main() {
  RxdcGeometry g;

  // SF7 / 62.5 kHz, 32-symbol preamble, V4 transition (5 ms TCXO + 1 ms): the probe's sleep, measured 100/100.
  CHECK(fabsf(rxdcSymbolUs(7, 62.5f) - 2048.0f) < 0.01f);
  CHECK(rxdcGeometry(7, 62.5f, 32, 6000, &g));
  CHECK(g.sleep_us == 44646);
  // The window starts from the datasheet rule and lands on a tick count with low six bits 1..62.
  uint32_t rx = ticks(g.rx_us);
  CHECK((rx & 63) >= 1 && (rx & 63) <= 62);
  CHECK(rx >= (rxdcNeedTicks(7, 62.5f, 32) - ticks(44646 - 6000) + 1) / 2);
  CHECK(g.rx_us > 25000 && g.rx_us < 30000);
  // RadioLib truncates microseconds to ticks; the rounding keeps the tick count exact.
  CHECK((uint32_t)((uint64_t)g.rx_us * 8 / 125) == rx);
  // The measured post-preamble timer covers preamble, sync, header and slack.
  CHECK(g.reload_ticks == rxdcReloadTicks(rx, ticks(44646 - 6000)));
  CHECK(g.reload_ticks >= rxdcNeedTicks(7, 62.5f, 32));

  // The measured reload formula itself: only the low 16 bits of R count twice, its low six bits add 2^18 each.
  CHECK(rxdcReloadTicks(65, 100) == 100 + 130 + (1UL << 18));
  CHECK(rxdcReloadTicks(0x10041, 0) == (2 * 0x41 + (1UL << 18)));

  // A shorter TCXO delay shortens nothing but the programmed sleep.
  RxdcGeometry fast;
  CHECK(rxdcGeometry(7, 62.5f, 32, 2600, &fast) && fast.sleep_us == g.sleep_us);

  // Every SF and bandwidth MeshCore offers yields a safe pair, or a refusal; never a window shorter than a symbol.
  const float bws[] = {62.5f, 125.0f, 250.0f, 500.0f};
  for (uint8_t sf = 7; sf <= 12; sf++) {
    for (float bw : bws) {
      uint8_t pre = sf <= 8 ? 32 : 16;
      RxdcGeometry x;
      if (!rxdcGeometry(sf, bw, pre, 6000, &x)) continue;
      CHECK(x.rx_us >= rxdcSymbolUs(sf, bw));
      CHECK(x.sleep_us >= 6000 + RXDC_SLEEP_MARGIN_US);
      CHECK(x.reload_ticks >= rxdcNeedTicks(sf, bw, pre));
      // Worst case the preamble starts as a window closes: what is left after the sleep is at least the catch.
      CHECK(pre - x.sleep_us / rxdcSymbolUs(sf, bw) >= RXDC_CATCH_SYMBOLS);
    }
  }

  // Too short a sleep is refused rather than clamped: SF7 at 500 kHz sleeps 5.6 ms, under the 6.25 ms floor.
  CHECK(!rxdcGeometry(7, 500.0f, 32, 6000, &g));
  // A 16-symbol preamble at SF9 / 62.5 kHz still fits (5.8 symbols of 8.2 ms).
  CHECK(rxdcGeometry(9, 62.5f, 16, 6000, &g));
  // Nonsense in, refusal out.
  CHECK(!rxdcGeometry(4, 62.5f, 32, 6000, &g) && !rxdcGeometry(7, 0, 32, 6000, &g) && !rxdcGeometry(7, 62.5f, 10, 6000, &g));
  DONE();
}
