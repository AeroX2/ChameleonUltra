#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "indala_decode.h"

static int16_t samples[INDALA_CAPTURE_SAMPLES];
static const uint64_t frame = UINT64_C(0xa0000000a1b2c3d4);
static uint32_t rng = 1;
static int noise(void) {
    rng = rng * 1664525U + 1013904223U;
    return (int)((rng >> 24) & 31) - 16;
}

static void synth(double phase, unsigned shift, unsigned rotation, bool mismatch) {
    for (size_t i = 0; i < INDALA_CAPTURE_SAMPLES; i++) {
        unsigned bit = ((i + shift) * 3 / 128) + rotation;
        uint64_t word = frame;
        if (mismatch) word ^= (uint64_t)(bit / 64) << 8;
        int sign = ((word >> (63 - bit % 64)) & 1) ? 1 : -1;
        samples[i] = 128 + (int)(60 * sign * cos(2 * 3.141592653589793 * 3 * i / 8 + phase)) + noise();
    }
}

int main(void) {
    uint8_t raw[8];
    const uint8_t expected[8] = {0xa0, 0, 0, 0, 0xa1, 0xb2, 0xc3, 0xd4};
    // All frame rotations, several symbol offsets and full carrier phase range.
    for (unsigned rotation = 0; rotation < 64; rotation++) {
        for (unsigned phase = 0; phase < 16; phase++) {
            synth(phase * 3.141592653589793 / 8, phase * 3, rotation, false);
            assert(indala_decode(samples, INDALA_CAPTURE_SAMPLES, raw));
            assert(memcmp(raw, expected, 8) == 0);
        }
    }
    synth(0.4, 17, 0, true);
    assert(!indala_decode(samples, INDALA_CAPTURE_SAMPLES, raw));
    memset(samples, 0, sizeof(samples));
    assert(!indala_decode(samples, INDALA_CAPTURE_SAMPLES, raw));
    for (size_t i = 0; i < INDALA_CAPTURE_SAMPLES; i++) samples[i] = 128 + noise();
    assert(!indala_decode(samples, INDALA_CAPTURE_SAMPLES, raw));
    assert(!indala_decode(NULL, INDALA_CAPTURE_SAMPLES, raw));
    assert(!indala_decode(samples, 100, raw));
    puts("Indala decoder: 1024 noisy phase/alignment cases and rejection checks passed");
    return 0;
}
