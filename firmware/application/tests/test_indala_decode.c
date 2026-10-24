#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "indala_decode.h"
#include "protocols/indala.h"
#include "protocols/t55xx.h"

static int16_t samples[INDALA_CAPTURE_SAMPLES];
static uint32_t rng = 1;
static bool simulate_dma_gaps = false;
static bool simulate_envelope = false;
static int noise(void) { rng = rng * 1664525U + 1013904223U; return (int)((rng >> 24) & 31) - 16; }

static void synth(const uint8_t *record, double phase, unsigned shift, unsigned rotation, bool mismatch) {
    // Independent transmitter model, deliberately not using indala_phases.
    unsigned n = record[3];
    uint8_t states[1000];
    unsigned value = 0;
    for (size_t b = 0; b < 1000; b++) {
        unsigned raw = (record[4 + (b % n) / 8] >> (7 - b % 8)) & 1;
        if (mismatch && b % n == 50 && b / n % 2) raw ^= 1;
        value = record[1] == 1 ? raw : value ^ raw;
        states[b] = value;
    }
    for (size_t i = 0; i < INDALA_CAPTURE_SAMPLES; i++) {
        // Old 2048-sample DMA restarts could lose a sample at each boundary.
        size_t time = i + (simulate_dma_gaps ? i / 2048 : 0);
        unsigned bit = (time + shift) * 3 / 128 + rotation;
        int sign = states[bit] ? 1 : -1;
        samples[i] = 128 + (int)(60 * sign * cos(2 * 3.141592653589793 * 3 * time / 8 + phase)) + noise();
        if (simulate_envelope) samples[i] += 5500 + (int)(1200 * sin(2 * 3.141592653589793 * time / 131.0));
    }
}

int main(void) {
    uint8_t output[32];
    unsigned cases = 0;
    for (unsigned size = 0; size < 2; size++) {
        unsigned bits = size ? 224 : 64;
        for (unsigned psk = 1; psk <= 2; psk++) {
            uint8_t record[32] = {1, psk, 0, bits};
            record[4] = size ? 0x80 : 0xa0;
            for (size_t i = 8; i < 4 + bits / 8; i++) record[i] = 0xa1 ^ (i * 37);
            record[8] |= 0x80;
            assert(indala_valid(record, 4 + bits / 8));
            for (unsigned rotation = 0; rotation < bits; rotation++) {
                for (unsigned phase = 0; phase < 4; phase++) {
                    synth(record, phase * 1.31, phase * 11, rotation, false);
                    assert(indala_decode_ex(samples, INDALA_CAPTURE_SAMPLES, output, psk));
                    if (memcmp(output, record, 32)) {
                        printf("Mismatch bits=%u psk=%u rotation=%u phase=%u got=", bits, psk, rotation, phase);
                        for (int k=0;k<32;k++) printf("%02x",output[k]);
                        printf("\n"); fflush(stdout);
                    }
                    assert(memcmp(output, record, 32) == 0);
                    cases++;
                }
            }
            if ((bits == 64 && psk == 1) || (bits == 224 && psk == 2)) {
                synth(record, 0.4, 17, 0, false);
                assert(indala_decode(samples, INDALA_CAPTURE_SAMPLES, output));
                assert(memcmp(output, record, 32) == 0);
            }
            if (bits == 224 && psk == 2) {
                simulate_dma_gaps = true;
                synth(record, 0.4, 17, 0, false);
                assert(!indala_decode_ex(samples, INDALA_CAPTURE_SAMPLES, output, psk));
                simulate_dma_gaps = false;
                synth(record, 0.4, 17, 0, false);
                assert(indala_decode_ex(samples, INDALA_CAPTURE_SAMPLES, output, psk));
                assert(memcmp(output, record, 32) == 0);
            }
            simulate_envelope = true;
            for (unsigned phase = 0; phase < 8; phase++) {
                synth(record, phase * 0.7, phase * 5, phase * 7, false);
                assert(indala_decode_ex(samples, INDALA_CAPTURE_SAMPLES, output, psk));
                assert(memcmp(output, record, 32) == 0);
            }
            simulate_envelope = false;
            synth(record, 0.4, 17, 0, true);
            assert(!indala_decode_ex(samples, INDALA_CAPTURE_SAMPLES, output, psk));
            uint32_t blocks[8] = {0};
            assert(indala_t55xx_blocks(record, blocks) == bits / 32 + 1);
            assert(((blocks[0] >> T5577_MAXBLOCK_SHIFT) & 7) == bits / 32);
            assert(!!(blocks[0] & T5577_PWD) == (bits == 64));
            assert((blocks[0] & 0x3000) == (psk == 1 ? T5577_MODULATION_PSK1 : T5577_MODULATION_PSK2));
            for (unsigned i = 0; i < bits / 8; i++) assert(((blocks[1 + i / 4] >> (24 - i % 4 * 8)) & 255) == record[4 + i]);
            uint8_t phases[448];
            size_t count = indala_phases(record, phases, sizeof(phases));
            assert(count == bits || count == bits * 2);
            for (size_t i = 0; i < count; i++) {
                unsigned raw = (record[4 + (i % bits) / 8] >> (7 - i % 8)) & 1;
                unsigned decoded = psk == 1 ? phases[i] : phases[i] ^ phases[(i + count - 1) % count];
                assert(decoded == raw);
            }
            assert(!indala_phases(record, phases, 1));
            record[0] = 2;
            assert(!indala_valid(record, 4 + bits / 8));
        }
    }
    memset(samples, 0, sizeof(samples));
    assert(!indala_decode(samples, INDALA_CAPTURE_SAMPLES, output));
    for (size_t i = 0; i < INDALA_CAPTURE_SAMPLES; i++) samples[i] = 128 + noise();
    assert(!indala_decode(samples, INDALA_CAPTURE_SAMPLES, output));
    assert(!indala_decode(NULL, INDALA_CAPTURE_SAMPLES, output));
    assert(!indala_decode(samples, 100, output));
    printf("%u Indala noisy alignment/encoding cases, rejection, T55xx and phase-loop checks passed\n", cases);
}
