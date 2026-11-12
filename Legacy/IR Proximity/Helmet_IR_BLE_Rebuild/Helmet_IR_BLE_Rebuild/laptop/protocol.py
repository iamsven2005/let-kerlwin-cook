"""The same bounded wire format as firmware/src/Protocol.h."""
from dataclasses import dataclass
import re

PATTERN = re.compile(rb"([LR]),([0-6]),([0-6]),([0-9]{1,5})")


@dataclass(frozen=True)
class Reading:
    board: str
    outer: int
    inner: int
    sequence: int


def parse_reading(data: bytes) -> Reading | None:
    match = PATTERN.fullmatch(data)
    if match is None:
        return None
    board, outer, inner, sequence = match.groups()
    seq = int(sequence)
    if seq > 65535:
        return None
    return Reading(board.decode("ascii"), int(outer), int(inner), seq)
