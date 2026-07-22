#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "wiegand.h"

static bool candidate_has_valid_parity(const wiegand_candidate_t *candidate) {
    return (candidate->flags & WIEGAND_CANDIDATE_HAS_PARITY) &&
           (candidate->flags & WIEGAND_CANDIDATE_PARITY_VALID);
}

static void test_valid_h10301_candidates(void) {
    wiegand_card_t source = {
        .facility_code = 127,
        .card_number = 1248,
        .format = H10301,
    };
    uint64_t raw = pack(&source);
    wiegand_candidate_t candidates[WIEGAND_MAX_CANDIDATES] = {0};
    size_t count = unpack_all(0, 26, 0, raw, candidates, WIEGAND_MAX_CANDIDATES);

    assert(raw == 0x2006fe09c1ULL);
    assert(count == 2);
    assert(candidates[0].card.format == H10301);
    assert(candidates[0].card.facility_code == 127);
    assert(candidates[0].card.card_number == 1248);
    assert(candidate_has_valid_parity(&candidates[0]));
    assert(candidates[1].card.format == IND26);

    wiegand_card_t *selected = unpack(0, 26, 0, raw);
    assert(selected != NULL);
    assert(selected->format == H10301);
    free(selected);
}

static void test_invalid_parity_candidates(void) {
    const uint64_t raw = 0x2006d51e1dULL;
    wiegand_candidate_t candidates[WIEGAND_MAX_CANDIDATES] = {0};
    size_t count = unpack_all(0, 26, 0, raw, candidates, WIEGAND_MAX_CANDIDATES);

    assert(count == 2);
    assert(candidates[0].card.format == H10301);
    assert(candidates[0].card.facility_code == 106);
    assert(candidates[0].card.card_number == 36622);
    assert(!candidate_has_valid_parity(&candidates[0]));
    assert(candidates[1].card.format == IND26);
    assert(!candidate_has_valid_parity(&candidates[1]));

    wiegand_card_t *selected = unpack(0, 26, 0, raw);
    assert(selected != NULL);
    assert(selected->format == H10301);
    assert(selected->facility_code == 106);
    assert(selected->card_number == 36622);
    free(selected);
}

static void test_hint_and_capacity(void) {
    const uint64_t raw = 0x2006d51e1dULL;
    wiegand_candidate_t candidate = {0};

    assert(unpack_all(0, 26, 0, raw, &candidate, 1) == 2);
    assert(candidate.card.format == H10301);

    assert(unpack_all(IND26, 26, 0, raw, &candidate, 1) == 1);
    assert(candidate.card.format == IND26);
    assert(unpack(IND26, 26, 0, raw) == NULL);
    assert(unpack_all(0, 25, 0, raw, &candidate, 1) == 0);
}

static void test_all_format_round_trips(void) {
    static const struct {
        uint8_t format;
        uint8_t length;
    } cases[] = {
        {H10301, 26}, {IND26, 26}, {IND27, 27}, {INDASC27, 27}, {TECOM27, 27},
        {W2804, 28}, {IND29, 29}, {ATSW30, 30}, {ADT31, 31}, {HCP32, 32},
        {HPP32, 32}, {KASTLE, 32}, {KANTECH, 32}, {WIE32, 32}, {D10202, 33},
        {H10306, 34}, {N10002, 34}, {OPTUS34, 34}, {SMP34, 34}, {BQT34, 34},
        {C1K35S, 35}, {C15001, 36}, {S12906, 36}, {SIE36, 36}, {ACTPHID, 36},
        {H10320, 37}, {H10302, 37}, {H10304, 37}, {P10004, 37}, {HGEN37, 37},
        {MDI37, 37},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        wiegand_card_t source = {
            .facility_code = 1,
            .card_number = 1,
            .issue_level = 1,
            .oem = 1,
            .format = cases[i].format,
        };
        uint64_t raw = pack(&source);
        wiegand_candidate_t candidate = {0};

        assert(raw != 0);
        assert(unpack_all(cases[i].format, cases[i].length, 0, raw, &candidate, 1) == 1);
        assert(candidate.card.format == cases[i].format);
        if (candidate.flags & WIEGAND_CANDIDATE_HAS_PARITY) {
            assert(candidate.flags & WIEGAND_CANDIDATE_PARITY_VALID);
        }

        wiegand_card_t *selected = unpack(cases[i].format, cases[i].length, 0, raw);
        assert(selected != NULL);
        assert(selected->format == cases[i].format);
        free(selected);
    }
}

int main(void) {
    test_valid_h10301_candidates();
    test_invalid_parity_candidates();
    test_hint_and_capacity();
    test_all_format_round_trips();
    return 0;
}
