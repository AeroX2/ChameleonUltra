#ifndef INDALA_DECODE_H
#define INDALA_DECODE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// RF/32, fc/2, 64/224-bit PSK1/PSK2. Three longest frames plus alignment.
// Input: SAADC results in signed 16-bit DMA storage. Output: wire record.
#define INDALA_CAPTURE_SAMPLES 28800U
bool indala_decode_ex(const int16_t *samples, size_t count, uint8_t record[32], uint8_t psk);
bool indala_decode(const int16_t *samples, size_t count, uint8_t record[32]);
#endif
