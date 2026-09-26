"""Executable checks for the phase-one Journal A/B selection rules.

The target is intentionally a small, dependency-free model of the ABI rules;
it is also useful on a host where the STM32 flash HAL is unavailable.
"""

from dataclasses import dataclass, replace
import unittest


IDLE, PENDING, WRITING, JUMPING = range(4)
NONE, UPDATE, ROLLBACK = range(3)
MAGIC = 0x55524A4C
VERSION = 1


@dataclass(frozen=True)
class Record:
    magic: int = MAGIC
    version: int = VERSION
    size: int = 24
    sequence: int = 1
    state: int = IDLE
    target: int = NONE
    crc: int = 0


def valid_pair(state, target):
    return target == NONE if state == IDLE else state <= JUMPING and target in (UPDATE, ROLLBACK)


def valid(record):
    return (isinstance(record, Record) and record.magic == MAGIC and
            record.version == VERSION and record.size == 24 and
            valid_pair(record.state, record.target) and record.crc == checksum(record))


def checksum(record):
    # The production test vector uses a deterministic stand-in for CRC32.
    return ((record.magic ^ record.version ^ record.size ^ record.sequence ^
             record.state ^ record.target) & 0xFFFFFFFF)


def sealed(**kwargs):
    record = replace(Record(), **kwargs)
    return replace(record, crc=checksum(record))


def newer(left, right):
    delta = (left - right) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def read_latest(slots):
    valid_slots = [i for i, record in enumerate(slots) if record is not None and valid(record)]
    if not valid_slots:
        return None if all(record is None for record in slots) else "error"
    if len(valid_slots) == 1:
        return slots[valid_slots[0]]
    left, right = slots
    if left.sequence == right.sequence:
        return left if left == right else "error"
    return right if newer(right.sequence, left.sequence) else left


class JournalRulesTest(unittest.TestCase):
    def test_empty_and_single_valid_slots(self):
        self.assertIsNone(read_latest([None, None]))
        record = sealed()
        self.assertEqual(read_latest([record, None]), record)
        self.assertEqual(read_latest([None, record]), record)

    def test_same_sequence_and_conflict(self):
        record = sealed(state=PENDING, target=UPDATE)
        self.assertEqual(read_latest([record, record]), record)
        conflict = sealed(state=WRITING, target=UPDATE)
        self.assertEqual(read_latest([record, conflict]), "error")

    def test_newest_and_wraparound(self):
        self.assertEqual(read_latest([sealed(sequence=10), sealed(sequence=9)]).sequence, 10)
        self.assertEqual(read_latest([sealed(sequence=0), sealed(sequence=0xFFFFFFFF)]).sequence, 0)

    def test_invalid_combinations_and_torn_slot(self):
        invalid = sealed(state=IDLE, target=UPDATE)
        self.assertEqual(read_latest([invalid, sealed(sequence=2)]), sealed(sequence=2))
        self.assertEqual(read_latest(["torn", sealed(sequence=2)]), sealed(sequence=2))


if __name__ == "__main__":
    unittest.main()
