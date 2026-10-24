import argparse
import contextlib
import io
import re
import struct
from pathlib import Path
from types import SimpleNamespace

import pytest
from chameleon_cli_unit import LFIndalaEconfig, LFIndalaRead, LFIndalaWrite
from chameleon_cmd import ChameleonCMD
from chameleon_com import Response
from chameleon_enum import Command, Status
from chameleon_utils import UnexpectedResponseError
from indala import IndalaFrame
from lf_clone_utils import LFCloneVerificationError


def test_command_ids_are_unique_and_match_firmware():
    # IntEnum silently aliases duplicate values: iteration alone misses clashes.
    members = Command.__members__
    assert len({int(v) for v in members.values()}) == len(members)
    header = Path(__file__).resolve().parents[3] / "firmware/application/src/data_cmd.h"
    definitions = dict(
        re.findall(r"#define DATA_CMD_(\w+)\s+\((\d+)\)", header.read_text())
    )
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
    unit._device_cmd = SimpleNamespace(indala_scan_ex=lambda psk: IndalaFrame(raw))
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        unit.on_exec(argparse.Namespace(psk=0, output=None))
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


@pytest.fixture(params=[(8, 1), (8, 2), (28, 1), (28, 2)])
def frame(request):
    length, psk = request.param
    raw = bytearray(length)
    raw[0] = 0xA0 if length == 8 else 0x80
    raw[4:] = bytes((i * 17 + 0x80) % 256 for i in range(length - 4))
    return IndalaFrame(bytes(raw), psk)


def test_frame_file_and_payload_roundtrip(frame, tmp_path):
    assert IndalaFrame.from_payload(frame.to_payload()) == frame
    target = tmp_path / "indala.json"
    frame.save(target)
    assert IndalaFrame.load(target) == frame


def test_scan_ex_preserves_encoding(frame):
    def send(command, payload):
        assert command == Command.INDALA_SCAN_EX
        assert payload == bytes((frame.psk,))
        return Response(command, Status.LF_TAG_OK, frame.to_payload())

    assert (
        ChameleonCMD(SimpleNamespace(send_cmd_sync=send)).indala_scan_ex(frame.psk)
        == frame
    )


def test_cli_read_saves_lossless_file(frame, tmp_path):
    unit = LFIndalaRead()
    unit._device_cmd = SimpleNamespace(indala_scan_ex=lambda psk: frame)
    target = tmp_path / "tag.json"
    unit.on_exec(unit.args_parser().parse_args(["--output", str(target)]))
    assert IndalaFrame.load(target) == frame


def test_cli_write_verifies_and_rejects_mismatch(frame):
    writes = []
    unit = LFIndalaWrite()
    unit._device_cmd = SimpleNamespace(
        indala_write_to_t55xx=writes.append, indala_scan_ex=lambda psk: frame
    )
    args = unit.args_parser().parse_args(
        ["--raw", frame.raw.hex(), "--psk", str(frame.psk)]
    )
    unit.on_exec(args)
    assert writes == [frame]
    wrong = bytearray(frame.raw)
    wrong[-1] ^= 1
    unit._device_cmd.indala_scan_ex = lambda psk: IndalaFrame(bytes(wrong), frame.psk)
    with pytest.raises(LFCloneVerificationError):
        unit.on_exec(args)


def test_cli_slot_save_and_export(frame, tmp_path):
    calls = []
    unit = LFIndalaEconfig()
    unit._device_cmd = SimpleNamespace(
        get_active_slot=lambda: 0,
        set_slot_tag_type=lambda slot, tag: calls.append(("type", int(tag))),
        set_slot_data_default=lambda slot, tag: calls.append(("default", int(tag))),
        indala_set_emu_id=lambda value: calls.append(("frame", value)),
        set_slot_enable=lambda *args: calls.append(("enable", args)),
        slot_data_config_save=lambda: calls.append(("save",)),
    )
    target = tmp_path / "slot.json"
    args = unit.args_parser().parse_args(
        ["--raw", frame.raw.hex(), "--psk", str(frame.psk), "--output", str(target)]
    )
    unit.on_exec(args)
    assert calls[0] == ("type", frame.tag_type)
    assert calls[2] == ("frame", frame)
    assert calls[-1] == ("save",)
    assert IndalaFrame.load(target) == frame


@pytest.mark.parametrize(
    "payload",
    [bytes(12), bytes([1, 3, 0, 64]) + bytes(8), bytes([1, 1, 0, 224]) + bytes(8)],
)
def test_bad_extended_payloads(payload):
    with pytest.raises(ValueError):
        IndalaFrame.from_payload(payload)
