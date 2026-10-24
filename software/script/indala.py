"""Lossless Indala frames shared by CLI read, file, slot and write operations."""

import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class IndalaFrame:
    raw: bytes
    psk: int = 1

    def __post_init__(self):
        if len(self.raw) not in (8, 28) or self.psk not in (1, 2):
            raise ValueError("Indala requires 8 or 28 raw bytes and PSK1 or PSK2")
        prefix = int.from_bytes(self.raw[:5], "big")
        valid = (
            (prefix >> 7) == 0x140000001
            if len(self.raw) == 8
            else (int.from_bytes(self.raw[:4], "big") >> 2) == 0x20000000
        )
        if not valid:
            raise ValueError("Invalid Indala preamble")

    @property
    def bits(self):
        return len(self.raw) * 8

    @property
    def tag_type(self):
        return {(64, 1): 300, (224, 2): 301, (64, 2): 302, (224, 1): 303}[
            self.bits, self.psk
        ]

    def to_payload(self):
        return bytes((1, self.psk, 0, self.bits)) + self.raw

    @classmethod
    def from_payload(cls, payload):
        if (
            len(payload) not in (12, 32)
            or payload[0] != 1
            or payload[2] != 0
            or payload[3] != (len(payload) - 4) * 8
        ):
            raise ValueError("Invalid Indala response")
        return cls(bytes(payload[4:]), payload[1])

    def save(self, path):
        Path(path).write_text(
            json.dumps(
                {
                    "format": "indala",
                    "version": 1,
                    "bits": self.bits,
                    "psk": self.psk,
                    "raw": self.raw.hex().upper(),
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

    @classmethod
    def load(cls, path):
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        if data.get("format") != "indala" or data.get("version") != 1:
            raise ValueError("Not an Indala v1 file")
        frame = cls(bytes.fromhex(data["raw"]), data["psk"])
        if data.get("bits") != frame.bits:
            raise ValueError("Indala file bit count does not match raw data")
        return frame
