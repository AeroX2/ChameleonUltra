import unittest

from chameleon_cli_unit import LFT55xxClone
from chameleon_enum import TagSpecificType
from chameleon_utils import ArgsParserError, UnexpectedResponseError


class FakeEM410xCommands:
    def __init__(self, reads):
        self.reads = iter(reads)
        self.writes = []

    @staticmethod
    def get_device_model():
        return 0

    def em410x_write_to_t55xx(self, card_id):
        self.writes.append(card_id)

    def em410x_scan(self):
        result = next(self.reads)
        if isinstance(result, Exception):
            raise result
        return result


class FakeIDTECKCommands:
    def __init__(self):
        self.writes = []

    @staticmethod
    def get_device_model():
        return 0

    def idteck_write_to_t55xx(self, frame):
        self.writes.append(frame)


class LFCloneCLITests(unittest.TestCase):
    @staticmethod
    def make_unit(commands):
        unit = LFT55xxClone()
        unit._device_cmd = commands
        return unit

    def test_em410x_mismatch_rewrites_then_verifies(self):
        expected = bytes.fromhex("1234567890")
        commands = FakeEM410xCommands((
            (TagSpecificType.EM410X_64, bytes.fromhex("DEADBEEF0D")),
            (TagSpecificType.EM410X_64, expected),
        ))
        unit = self.make_unit(commands)
        args = unit.args_parser().parse_args(("-t", "em410x", "--id", expected.hex()))

        unit.on_exec(args)

        self.assertEqual(commands.writes, [expected, expected])

    def test_em410x_missing_tag_fails_after_three_writes(self):
        missing = UnexpectedResponseError("LF tag not found")
        commands = FakeEM410xCommands((missing, missing, missing))
        unit = self.make_unit(commands)
        args = unit.args_parser().parse_args(("-t", "em410x", "--id", "1234567890"))

        with self.assertRaisesRegex(ArgsParserError, "tag could not be read back"):
            unit.on_exec(args)

        self.assertEqual(len(commands.writes), 3)

    def test_idteck_requires_explicit_unverified_write(self):
        commands = FakeIDTECKCommands()
        unit = self.make_unit(commands)
        args = unit.args_parser().parse_args(("-t", "idteck", "--id", "DEADBEEF"))

        with self.assertRaisesRegex(ArgsParserError, "--no-verify"):
            unit.on_exec(args)

        self.assertEqual(commands.writes, [])

    def test_idteck_no_verify_attempts_write(self):
        commands = FakeIDTECKCommands()
        unit = self.make_unit(commands)
        args = unit.args_parser().parse_args((
            "-t", "idteck", "--id", "DEADBEEF", "--no-verify",
        ))

        unit.on_exec(args)

        self.assertEqual(commands.writes, [bytes.fromhex("4944544BDEADBEEF")])


if __name__ == "__main__":
    unittest.main()
