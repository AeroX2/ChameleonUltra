#ifndef INDALA_DECODE_H
#define INDALA_DECODE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 64-bit PSK1, RF/32, fc/2. Input: signed ADC samples at 16 MHz / 96,
// with a 125 kHz field. Output: raw frame, MSB first (not a Wiegand UID).
#define INDALA_CAPTURE_SAMPLES 8192U
bool indala_decode(const int16_t *samples, size_t count, uint8_t raw[8]);
#endif
