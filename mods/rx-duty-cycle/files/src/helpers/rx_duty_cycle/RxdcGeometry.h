#pragma once

#include <stdint.h>

// SX1262 receive duty cycle timing. Pure; RadioLib programs the result.

#define RXDC_TICK_US          15.625f  // the radio's timer unit
#define RXDC_CATCH_SYMBOLS    10.0f    // preamble symbols the receiver still hears after the longest sleep
#define RXDC_CAPTURE_SYMBOLS  6.0f     // what an SX1262 needs to lock on
#define RXDC_PAD_SYMBOLS      0.2f     // keeps every geometry off the register pairs that drop headers
#define RXDC_SLACK_SYMBOLS    1.0f     // margin over the timer rule
#define RXDC_HEADER_SYMBOLS   8.0f
#define RXDC_SLEEP_MARGIN_US  250      // shorter programmed sleeps arm but detect nothing

struct RxdcGeometry {
  uint32_t rx_us;                      // listen window as handed to RadioLib
  uint32_t sleep_us;                   // sleep period as handed to RadioLib, wake transition included
  uint32_t reload_ticks;               // the SX1262's post-preamble timer for this pair
};

// Symbol time in microseconds.
float rxdcSymbolUs(uint8_t sf, float bw_khz);
// The SX1262's measured post-preamble timer: (S + 2*(R & 0xFFFF) + ((R & 63) << 18)) & 0xFFFFFF, in ticks.
uint32_t rxdcReloadTicks(uint32_t rx_ticks, uint32_t sleep_ticks);
// Ticks the preamble, sync word, header and slack take at this SF and bandwidth.
uint32_t rxdcNeedTicks(uint8_t sf, float bw_khz, uint8_t preamble);
// Listen and sleep for a sender's preamble; false when no safe pair exists (sleep under the floor, or no window covers the header).
bool rxdcGeometry(uint8_t sf, float bw_khz, uint8_t preamble, uint32_t transition_us, RxdcGeometry* out);
