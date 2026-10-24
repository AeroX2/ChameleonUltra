import argparse
import contextlib
import io
import re
import struct
from pathlib import Path
from types import SimpleNamespace

import pytest

from chameleon_cli_unit import LFIndalaRead
from chameleon_cmd import ChameleonCMD
from chameleon_com import Response
from chameleon_enum import Command, Status
from chameleon_utils import UnexpectedResponseError


def test_command_ids_are_unique_and_match_firmware():
    # IntEnum silently aliases duplicate values: iteration alone misses clashes.
    members = Command.__members__
    assert len({int(v) for v in members.values()}) == len(members)
    header = Path(__file__).resolve().parents[3] / "firmware/application/src/data_cmd.h"
    definitions = dict(re.findall(r"#define DATA_CMD_(\w+)\s+\((\d+)\)", header.read_text()))
    assert len(set(definitions.values())) == len(definitions)
    for name, value in definitions.items():
        if name in members:
            assert int(members[name]) == int(value), name


def make_cmd(status, payload):
    def send(cmd):
        assert cmd == Command.INDALA_SCAN
        return Response(cmd, status, payload)
    return ChameleonCMD(SimpleNamespace(send_cmd_sync=send))


def test_indala_raw_frame_and_cli_output():
    raw = bytes.fromhex("a0000000a1b2c3d4")
    cmd = make_cmd(Status.LF_TAG_OK, raw)
    assert cmd.indala_scan() == raw
    unit = LFIndalaRead()
    unit._device_cmd = cmd
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        unit.on_exec(argparse.Namespace())
    assert "A0000000A1B2C3D4" in out.getvalue()


@pytest.mark.parametrize("payload", [b"", bytes(7), bytes(9)])
def test_indala_rejects_malformed_success(payload):
    with pytest.raises(ValueError):
        make_cmd(Status.LF_TAG_OK, payload).indala_scan()


def test_indala_no_tag_is_not_a_success():
    with pytest.raises(UnexpectedResponseError):
        make_cmd(Status.LF_TAG_NO_FOUND, b"").indala_scan()


def test_settings_v14_response_includes_sleep_timeout():
    payload = struct.pack("!BBBBBBB6sB", 14, 0, 1, 2, 4, 4, 0, b"123456", 8)
    def send(cmd):
        assert cmd == Command.GET_DEVICE_SETTINGS
        return Response(cmd, Status.SUCCESS, payload)
    result = ChameleonCMD(SimpleNamespace(send_cmd_sync=send)).get_device_settings()
    assert result["settings_version"] == 14
    assert result["sleep_timeout"] == 8
