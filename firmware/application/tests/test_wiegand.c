#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "wiegand.h"

static void test_valid_h10301(void) {
    wiegand_card_t source = {
        .facility_code = 127,
        .card_number = 1248,
        .format = H10301,
    };
    uint64_t raw = pack(&source);
    assert(raw == 0x2006fe09c1ULL);
    wiegand_card_t *decoded = unpack(0, 26, 0, raw);

    assert(decoded != NULL);
    assert(decoded->format == H10301);
    assert(decoded->facility_code == source.facility_code);
    assert(decoded->card_number == source.card_number);
    free(decoded);
}

static void test_invalid_parity_h10301_fallback(void) {
    const uint64_t raw = 0x2006d51e1dULL;
    wiegand_card_t *decoded = unpack(0, 26, 0, raw);

    assert(decoded != NULL);
    assert(decoded->format == H10301);
    assert(decoded->facility_code == 106);
    assert(decoded->card_number == 36622);
    free(decoded);
}

static void test_invalid_parity_respects_explicit_format(void) {
    const uint64_t raw = 0x2006d51e1dULL;

    assert(unpack(IND26, 26, 0, raw) == NULL);

    wiegand_card_t *decoded = unpack(H10301, 26, 0, raw);
    assert(decoded != NULL);
    assert(decoded->format == H10301);
    free(decoded);
}

int main(void) {
    test_valid_h10301();
    test_invalid_parity_h10301_fallback();
    test_invalid_parity_respects_explicit_format();
    return 0;
}
