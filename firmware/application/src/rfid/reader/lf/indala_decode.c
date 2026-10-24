#include "indala_decode.h"

// fc/2 is DFT bin 3/8 at this sampling rate. Integrate five complete
// periods so the DC envelope cancels. Use one axis for the entire capture;
// choosing an axis per bit can invert the polarity near a 45-degree phase.
static const int16_t cos_ref[8] = {1024, -724, 0, 724, -1024, 724, 0, -724};
static const int16_t sin_ref[8] = {0, 724, -1024, 724, 0, -724, 1024, -724};

bool indala_decode(const int16_t *samples, size_t count, uint8_t raw[8]) {
    if (!samples || !raw || count < INDALA_CAPTURE_SAMPLES) return false;
    // Search all symbol alignments (128/3 samples per bit). Two identical
    // consecutive frames are required: a preamble alone is not a valid read.
    for (size_t offset = 0; offset < 43; offset++) {
        int32_t re[192], im[192];
        int64_t re_energy = 0, im_energy = 0;
        size_t bits = 0;
        for (; bits < 192; bits++) {
            size_t start = offset + bits * 128 / 3;
            if (start + 40 > INDALA_CAPTURE_SAMPLES) break;
            int32_t r = 0, q = 0;
            for (size_t j = 0; j < 40; j++) {
                r += samples[start + j] * cos_ref[(start + j) & 7];
                q += samples[start + j] * sin_ref[(start + j) & 7];
            }
            re[bits] = r;
            im[bits] = q;
            re_energy += (int64_t)r * r;
            im_energy += (int64_t)q * q;
        }
        const int32_t *phase = re_energy >= im_energy ? re : im;
        uint64_t previous = 0, current = 0;
        for (size_t bit = 0; bit < bits; bit++) {
            previous = (previous << 1) | (current >> 63);
            current = (current << 1) | (phase[bit] > 0);
            if (bit < 127 || previous != current) continue;
            uint64_t frame = current;
            const uint64_t mask = UINT64_C(0xffffffff80000000);
            const uint64_t preamble = UINT64_C(0xa000000080000000);
            if ((frame & mask) != preamble) frame = ~frame;
            if ((frame & mask) != preamble) continue;
            // Reject effectively absent subcarriers even if quantization noise
            // happens to form a repeating bit pattern (ADC units, before LUT).
            bool strong = true;
            for (size_t j = bit - 127; j <= bit; j++) {
                if (phase[j] > -40960 && phase[j] < 40960) strong = false;
            }
            if (!strong) continue;
            for (size_t j = 0; j < 8; j++) raw[j] = frame >> (56 - j * 8);
            return true;
        }
    }
    return false;
}
