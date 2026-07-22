#pragma once

#include "protocols.h"
#include "utils/fskdemod.h"
#include "wiegand.h"

#define HIDPROX_DATA_SIZE (16)

typedef enum {
    STATE_SOF,
    STATE_DATA_LO,
    STATE_DATA_HI,
    STATE_DONE,
} hidprox_codec_state_t;

typedef struct {
    uint8_t data[HIDPROX_DATA_SIZE];

    bool bit;
    uint8_t sof;
    uint64_t raw;
    uint8_t raw_length;

    fsk_t *modem;
    hidprox_codec_state_t state;

    uint8_t format_hint;
    bool return_all_candidates;
    wiegand_card_t *card;
} hidprox_codec;

extern const protocol hidprox;

size_t hidprox_get_candidates(hidprox_codec *d, wiegand_candidate_t *candidates, size_t capacity);
uint8_t hidprox_t55xx_writer(wiegand_card_t *card, uint32_t *blks);
