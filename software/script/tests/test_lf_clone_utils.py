import unittest

from lf_clone_utils import (
    DEFAULT_T55XX_OLD_KEYS,
    LFCloneVerificationError,
    write_and_verify,
)


class ReadFailed(Exception):
    pass


class LFCloneVerificationTests(unittest.TestCase):
    def test_zero_password_is_tried(self):
        self.assertIn(b"\x00\x00\x00\x00", DEFAULT_T55XX_OLD_KEYS)

    def test_first_matching_read_succeeds(self):
        writes = []

        attempt, observed = write_and_verify(
            lambda: writes.append(True),
            lambda: b"expected",
            lambda value: value == b"expected",
        )

        self.assertEqual(attempt, 1)
        self.assertEqual(observed, b"expected")
        self.assertEqual(len(writes), 1)

    def test_read_failure_retries_complete_write(self):
        writes = []
        reads = iter((ReadFailed(), b"expected"))

        def read():
            result = next(reads)
            if isinstance(result, Exception):
                raise result
            return result

        attempt, _ = write_and_verify(
            lambda: writes.append(True),
            read,
            lambda value: value == b"expected",
            retry_exceptions=(ReadFailed,),
        )

        self.assertEqual(attempt, 2)
        self.assertEqual(len(writes), 2)

    def test_mismatch_reports_last_read(self):
        writes = []

        with self.assertRaises(LFCloneVerificationError) as context:
            write_and_verify(
                lambda: writes.append(True),
                lambda: b"wrong",
                lambda value: value == b"expected",
                attempts=3,
            )

        self.assertEqual(context.exception.attempts, 3)
        self.assertEqual(context.exception.last_observed, b"wrong")
        self.assertIsNone(context.exception.last_error)
        self.assertEqual(len(writes), 3)

    def test_missing_tag_reports_last_error(self):
        def read():
            raise ReadFailed()

        with self.assertRaises(LFCloneVerificationError) as context:
            write_and_verify(
                lambda: None,
                read,
                lambda value: True,
                attempts=2,
                retry_exceptions=(ReadFailed,),
            )

        self.assertIsInstance(context.exception.last_error, ReadFailed)
        self.assertIsNone(context.exception.last_observed)

    def test_attempt_count_must_be_positive(self):
        with self.assertRaises(ValueError):
            write_and_verify(lambda: None, lambda: None, lambda value: True, attempts=0)


if __name__ == "__main__":
    unittest.main()
