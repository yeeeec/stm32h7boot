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


class SnapshotFlow:
    """Small host model for the phase-two ownership and recovery rules."""

    def __init__(self, state=IDLE, target=NONE, current="v1", update="v2", last=None):
        self.state = state
        self.target = target
        self.current = current
        self.update = update
        self.last = last
        self.sd_mounts = 0
        self.last_saves = 0
        self.installs = []
        self.update_removed = False
        self.fail_last = False
        self.fail_current = False

    def process(self):
        if self.state == IDLE and self.target == NONE:
            return "launch"
        self.sd_mounts += 1
        if self.state == PENDING and self.target == UPDATE:
            if self.fail_last:
                return "error-pending"
            self.last = self.current
            self.last_saves += 1
            self.state = WRITING
        elif self.state == PENDING and self.target == ROLLBACK:
            if self.last is None:
                return "error-pending"
            self.state = WRITING
        elif self.state == JUMPING:
            self.state = WRITING
        if self.state != WRITING:
            return "error"
        source = self.update if self.target == UPDATE else self.last
        if source is None or self.fail_current:
            return "error-writing"
        self.installs.append(source)
        self.current = source
        self.state = JUMPING
        if self.target == UPDATE and self.update_removed:
            return "error-source"
        return "launch"


class SnapshotFlowTest(unittest.TestCase):
    def test_idle_is_short_path_without_sd(self):
        flow = SnapshotFlow()
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.sd_mounts, 0)

    def test_pending_update_saves_last_once(self):
        flow = SnapshotFlow(state=PENDING, target=UPDATE)
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.last, "v1")
        self.assertEqual(flow.last_saves, 1)
        flow.process()
        self.assertEqual(flow.last_saves, 1)

    def test_writing_update_does_not_overwrite_last(self):
        flow = SnapshotFlow(state=WRITING, target=UPDATE, last="old")
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.last, "old")
        self.assertEqual(flow.last_saves, 0)

    def test_jumping_update_reuses_update_source(self):
        flow = SnapshotFlow(state=JUMPING, target=UPDATE, last="v1")
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["v2"])
        self.assertFalse(flow.update_removed)

    def test_request_file_is_not_an_idle_trigger(self):
        flow = SnapshotFlow()
        flow.request_present = True
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.sd_mounts, 0)

    def test_pending_transaction_does_not_require_request_file(self):
        flow = SnapshotFlow(state=PENDING, target=UPDATE)
        flow.request_present = False
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["v2"])

    def test_rollback_reuses_last_source(self):
        flow = SnapshotFlow(state=PENDING, target=ROLLBACK, current="v2", last="v1")
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["v1"])
        self.assertEqual(flow.current, "v1")

    def test_last_failure_keeps_pending(self):
        flow = SnapshotFlow(state=PENDING, target=UPDATE)
        flow.fail_last = True
        self.assertEqual(flow.process(), "error-pending")
        self.assertEqual((flow.state, flow.target), (PENDING, UPDATE))

    def test_current_rebuild_failure_keeps_writing(self):
        flow = SnapshotFlow(state=WRITING, target=UPDATE)
        flow.fail_current = True
        self.assertEqual(flow.process(), "error-writing")
        self.assertEqual((flow.state, flow.target), (WRITING, UPDATE))


if __name__ == "__main__":
    unittest.main()
