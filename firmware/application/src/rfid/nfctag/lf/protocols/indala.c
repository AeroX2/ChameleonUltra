#include "indala.h"
#include "t55xx.h"

bool indala_valid(const uint8_t *r, size_t length) {
    if (!r || length < 4 || r[0] != 1 || (r[1] != 1 && r[1] != 2) || r[2]) return false;
    if (r[3] != 64 && r[3] != 224) return false;
    if (length != 4U + r[3] / 8U) return false;
    if (r[3] == 64) return r[4] == 0xa0 && !r[5] && !r[6] && !r[7] && (r[8] & 0x80);
    return r[4] == 0x80 && !r[5] && !r[6] && !(r[7] & 0xfc);
}

uint16_t indala_type(const uint8_t *r) {
    if (r[3] == 64) return r[1] == 1 ? 300 : 302;
    return r[1] == 2 ? 301 : 303;
}

size_t indala_phases(const uint8_t *r, uint8_t *phases, size_t capacity) {
    if (!r || !indala_valid(r, 4U + r[3] / 8U)) return 0;
    size_t bits = r[3];
    unsigned parity = 0;
    for (size_t i = 0; i < bits; i++) parity ^= (r[4 + i / 8] >> (7 - i % 8)) & 1;
    // Odd PSK2 transition parity needs two frames to close the phase loop.
    size_t count = bits * ((r[1] == 2 && parity) ? 2 : 1);
    if (!phases || capacity < count) return 0;
    uint8_t phase = 0;
    for (size_t i = 0; i < count; i++) {
        size_t bit = i % bits;
        uint8_t value = (r[4 + bit / 8] >> (7 - bit % 8)) & 1;
        phase = r[1] == 1 ? value : phase ^ value;
        phases[i] = phase;
    }
    return count;
}

uint8_t indala_t55xx_blocks(const uint8_t *r, uint32_t blocks[8]) {
    if (!r || !blocks || !indala_valid(r, 4U + r[3] / 8U)) return 0;
    uint8_t data_blocks = r[3] / 32;
    blocks[0] = T5577_BITRATE_RF_32 | T5577_PSKCF_RF_2 | (data_blocks == 2 ? T5577_PWD : 0) |
        (r[1] == 1 ? T5577_MODULATION_PSK1 : T5577_MODULATION_PSK2) |
        (data_blocks << T5577_MAXBLOCK_SHIFT);
    for (uint8_t i = 0; i < data_blocks; i++) {
        blocks[i + 1] = 0;
        for (uint8_t j = 0; j < 4; j++) blocks[i + 1] = (blocks[i + 1] << 8) | r[4 + i * 4 + j];
    }
    return data_blocks + 1;
}
