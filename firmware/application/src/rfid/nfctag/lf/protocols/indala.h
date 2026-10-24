#ifndef INDALA_H
#define INDALA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Wire/storage: version, PSK encoding, bit count (big endian), raw bytes.
#define INDALA_RECORD_SIZE 32U
#define INDALA_MAX_BITS 224U
bool indala_valid(const uint8_t *record, size_t length);
uint16_t indala_type(const uint8_t *record);
size_t indala_phases(const uint8_t *record, uint8_t *phases, size_t capacity);
uint8_t indala_t55xx_blocks(const uint8_t *record, uint32_t blocks[8]);
#endif
