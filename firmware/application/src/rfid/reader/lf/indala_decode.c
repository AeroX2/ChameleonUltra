#include "indala_decode.h"
#include "protocols/indala.h"
#include <string.h>

static const int16_t cos_ref[8] = {1024, -724, 0, 724, -1024, 724, 0, -724};
static const int16_t sin_ref[8] = {0, 724, -1024, 724, 0, -724, 1024, -724};
#define MAX_SYMBOLS 675U

static bool find_frame(const uint8_t *bits, size_t count, uint8_t encoding, uint8_t *out, unsigned size_filter) {
    const unsigned sizes[] = {224, 64};
    for (size_t f = 0; f < 2; f++) {
        size_t size = sizes[f];
        if (size_filter && size != size_filter) continue;
        for (size_t start = 0; start + 2 * size <= count; start++) {
            for (unsigned invert = 0; invert < (encoding == 1 ? 2U : 1U); invert++) {
                uint8_t record[INDALA_RECORD_SIZE] = {1, encoding, 0, size};
                bool match = true;
                for (size_t j = 0; j < size; j++) {
                    uint8_t bit = bits[start + j];
                    if (bit > 1 || bit != bits[start + size + j]) { match = false; break; }
                    record[4 + j / 8] |= (bit ^ invert) << (7 - j % 8);
                }
                if (match && indala_valid(record, 4 + size / 8)) {
                    memcpy(out, record, sizeof(record));
                    return true;
                }
            }
        }
    }
    return false;
}

bool indala_decode_ex(const int16_t *samples, size_t count, uint8_t record[32], uint8_t psk) {
    if (!samples || !record || psk > 2 || count < INDALA_CAPTURE_SAMPLES) return false;
    // One IQ axis per capture avoids sign flips near a 45-degree phase.
    for (size_t offset = 0; offset < 43; offset++) {
        int32_t re[MAX_SYMBOLS], im[MAX_SYMBOLS];
        int64_t re_energy = 0, im_energy = 0;
        size_t count_bits = 0;
        for (; count_bits < MAX_SYMBOLS; count_bits++) {
            size_t start = offset + count_bits * 128 / 3;
            if (start + 40 > INDALA_CAPTURE_SAMPLES) break;
            int32_t r = 0, q = 0;
            for (size_t j = 0; j < 40; j++) {
                r += samples[start + j] * cos_ref[(start + j) & 7];
                q += samples[start + j] * sin_ref[(start + j) & 7];
            }
            re[count_bits] = r; im[count_bits] = q;
            re_energy += (int64_t)r * r; im_energy += (int64_t)q * q;
        }
        const int32_t *phase = re_energy >= im_energy ? re : im;
        uint8_t bits[MAX_SYMBOLS];
        for (size_t i = 0; i < count_bits; i++) {
            bits[i] = phase[i] > -40960 && phase[i] < 40960 ? 2 : phase[i] > 0;
        }
        if (psk != 2 && find_frame(bits, count_bits, 1, record, psk == 1 ? 0 : 64)) return true;
        if (psk == 1) continue;
        // Work backwards to preserve the previous PSK1 symbol.
        for (size_t i = count_bits - 1; i > 0; i--) {
            bits[i] = bits[i] > 1 || bits[i - 1] > 1 ? 2 : bits[i] ^ bits[i - 1];
        }
        if (find_frame(bits + 1, count_bits - 1, 2, record, 0)) return true;
    }
    return false;
}

// Auto follows the standard interpretations: 64-bit PSK1, then PSK2.
// A 224-bit PSK1 phase stream can also be a valid PSK2 frame with different
// raw bits, so that less-common interpretation must be requested explicitly.
bool indala_decode(const int16_t *samples, size_t count, uint8_t record[32]) {
    return indala_decode_ex(samples, count, record, 0);
}
