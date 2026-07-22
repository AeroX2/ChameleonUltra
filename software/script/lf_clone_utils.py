DEFAULT_T55XX_NEW_KEY = b"\x20\x20\x66\x66"
DEFAULT_T55XX_OLD_KEYS = (
    b"\x00\x00\x00\x00",
    b"\x51\x24\x36\x48",
    b"\x19\x92\x04\x27",
)


class LFCloneVerificationError(Exception):
    def __init__(self, attempts, last_observed=None, last_error=None):
        self.attempts = attempts
        self.last_observed = last_observed
        self.last_error = last_error
        super().__init__(f"LF clone verification failed after {attempts} attempts")


def write_and_verify(write, read, matches, attempts=3, retry_exceptions=()):
    """Write and read back an LF credential until it matches or attempts expire."""
    if attempts < 1:
        raise ValueError("attempts must be at least 1")

    last_observed = None
    last_error = None
    for attempt in range(1, attempts + 1):
        write()
        try:
            observed = read()
        except retry_exceptions as error:
            last_error = error
            last_observed = None
            continue

        last_error = None
        last_observed = observed
        if matches(observed):
            return attempt, observed

    raise LFCloneVerificationError(attempts, last_observed, last_error)
