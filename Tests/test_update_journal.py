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


def version_allowed(update, current=None):
    """Mirror the bootloader policy: first install and equal versions are valid."""
    return current is None or update >= current


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
    if state == IDLE:
        return target == NONE
    if state == JUMPING:
        return target == UPDATE
    return state in (PENDING, WRITING) and target in (UPDATE, ROLLBACK)


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


def reset_idle(slots):
    if "io" in slots:
        raise OSError("journal storage I/O")
    valid_slots = [record for record in slots if isinstance(record, Record) and valid(record)]
    newest = None
    for record in valid_slots:
        if newest is None or newer(record.sequence, newest.sequence):
            newest = record
    sequence = 1 if newest is None else (newest.sequence + 1) & 0xFFFFFFFF
    return sealed(sequence=sequence, state=IDLE, target=NONE)


class JournalRulesTest(unittest.TestCase):
    def test_legal_state_target_pairs(self):
        self.assertTrue(valid_pair(IDLE, NONE))
        self.assertTrue(valid_pair(PENDING, UPDATE))
        self.assertTrue(valid_pair(PENDING, ROLLBACK))
        self.assertTrue(valid_pair(WRITING, UPDATE))
        self.assertTrue(valid_pair(WRITING, ROLLBACK))
        self.assertTrue(valid_pair(JUMPING, UPDATE))
        self.assertFalse(valid_pair(JUMPING, ROLLBACK))
        self.assertFalse(valid_pair(IDLE, UPDATE))
        self.assertFalse(valid_pair(IDLE, ROLLBACK))

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

    def test_reset_idle_rebuilds_erased_and_invalid_slots(self):
        self.assertEqual(reset_idle([None, None]).sequence, 1)
        self.assertEqual(reset_idle(["invalid", "invalid"]).sequence, 1)

    def test_reset_idle_wins_equal_sequence_conflict(self):
        first = sealed(state=PENDING, target=UPDATE, sequence=9)
        second = sealed(state=WRITING, target=UPDATE, sequence=9)
        reset = reset_idle([first, second])
        self.assertEqual((reset.sequence, reset.state, reset.target), (10, IDLE, NONE))

    def test_reset_idle_wraps_after_newest_record(self):
        reset = reset_idle([sealed(sequence=0xFFFFFFFF), None])
        self.assertEqual((reset.sequence, reset.state, reset.target), (0, IDLE, NONE))

    def test_reset_idle_propagates_storage_io_error(self):
        with self.assertRaises(OSError):
            reset_idle(["io", None])


class SnapshotFlow:
    """Small host model for the phase-two ownership and recovery rules."""

    def __init__(self, state=IDLE, target=NONE, current="v1", update="v2", last=None,
                 current_version=(1, 0, 0), update_version=(2, 0, 0), debug=False,
                 current_present=None):
        self.state = state
        self.target = target
        self.current = current
        self.current_present = current is not None if current_present is None else current_present
        self.update = update
        self.last = last
        self.current_version = current_version
        self.update_version = update_version
        self.current_corrupt = False
        self.version_checks = 0
        self.debug = debug
        self.runtime_valid = True
        self.sd_mounts = 0
        self.last_saves = 0
        self.installs = []
        self.update_removed = False
        self.fail_last = False
        self.fail_current = False
        self.package_valid = True
        self.journal_writes = 0

    def process(self):
        if self.debug:
            self.sd_mounts += 1
            if not self.package_valid:
                return "launch"
            if self.fail_current:
                return "error-writing"
            self.installs.append(self.update)
            self.current = self.update
            self.current_present = True
            self.update_removed = True
            return "launch"

        if self.state == IDLE and self.target == NONE:
            return "launch"
        self.sd_mounts += 1
        if self.state == PENDING and self.target == UPDATE:
            self.version_checks += 1
            if self.current_corrupt:
                return "error-pending"
            if not version_allowed(self.update_version,
                                   self.current_version if self.current_present else None):
                return "error-pending"
            if self.fail_last:
                return "error-pending"
            if self.current_present:
                self.last = self.current
                self.last_saves += 1
            self.state = WRITING
            self.journal_writes += 1
        elif self.state == PENDING and self.target == ROLLBACK:
            if self.last is None:
                return "error-pending"
            self.state = WRITING
            self.journal_writes += 1
        elif self.state == JUMPING:
            if self.target != UPDATE or self.last is None or not self.runtime_valid:
                return "error-jumping"
            self.installs.append(self.last)
            self.current = self.last
            self.current_present = True
            self.state, self.target = IDLE, NONE
            self.journal_writes += 1
            return "launch"
        if self.state != WRITING:
            return "error"
        source = self.update if self.target == UPDATE else self.last
        if source is None or self.fail_current:
            return "error-writing"
        self.installs.append(source)
        self.current = source
        self.current_present = True
        if self.target == UPDATE:
            self.state = JUMPING
        else:
            self.state, self.target = IDLE, NONE
        self.journal_writes += 1
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

    def test_jumping_update_restores_last_source(self):
        flow = SnapshotFlow(state=JUMPING, target=UPDATE, last="v1")
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["v1"])
        self.assertEqual((flow.state, flow.target), (IDLE, NONE))
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

    def test_first_install_does_not_create_last(self):
        flow = SnapshotFlow(state=PENDING, target=UPDATE, current=None, last=None)
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.last, None)
        self.assertEqual(flow.last_saves, 0)
        self.assertEqual(flow.installs, ["v2"])

    def test_corrupt_current_is_not_first_install(self):
        flow = SnapshotFlow(state=PENDING, target=UPDATE, current=None, current_present=True)
        flow.current_corrupt = True
        self.assertEqual(flow.process(), "error-pending")
        self.assertEqual((flow.state, flow.target), (PENDING, UPDATE))

    def test_upgrade_policy_allows_new_and_equal_versions(self):
        for update_version in ((1, 0, 1), (1, 0, 0)):
            flow = SnapshotFlow(state=PENDING, target=UPDATE, current="v1",
                                update_version=update_version)
            self.assertEqual(flow.process(), "launch")
            self.assertEqual(flow.last_saves, 1)

    def test_upgrade_policy_rejects_downgrade(self):
        flow = SnapshotFlow(state=PENDING, target=UPDATE, current="v2",
                            current_version=(2, 0, 0), update_version=(1, 0, 0))
        self.assertEqual(flow.process(), "error-pending")
        self.assertEqual(flow.last_saves, 0)
        self.assertEqual((flow.state, flow.target), (PENDING, UPDATE))

    def test_rollback_does_not_run_upgrade_policy(self):
        flow = SnapshotFlow(state=PENDING, target=ROLLBACK, current="v2", last="v1",
                            current_version=(2, 0, 0), update_version=(1, 0, 0))
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.version_checks, 0)

    def test_writing_does_not_run_upgrade_policy_again(self):
        flow = SnapshotFlow(state=WRITING, target=UPDATE, current="v2", last="v1",
                            current_version=(2, 0, 0), update_version=(1, 0, 0))
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.version_checks, 0)

    def test_debug_ignores_journal(self):
        flow = SnapshotFlow(state=JUMPING, target=UPDATE, debug=True)
        self.assertEqual(flow.process(), "launch")
        self.assertEqual((flow.state, flow.target), (JUMPING, UPDATE))
        self.assertEqual(flow.journal_writes, 0)

    def test_debug_missing_or_invalid_update_launches(self):
        for package_valid in (False,):
            flow = SnapshotFlow(debug=True)
            flow.package_valid = package_valid
            self.assertEqual(flow.process(), "launch")
            self.assertEqual(flow.journal_writes, 0)

    def test_debug_allows_downgrade_and_cleans_update(self):
        flow = SnapshotFlow(debug=True, current_version=(2, 0, 0), update_version=(1, 0, 0))
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["v2"])
        self.assertTrue(flow.update_removed)
        self.assertEqual(flow.version_checks, 0)
        self.assertEqual(flow.last_saves, 0)

    def test_rollback_reuses_last_source(self):
        flow = SnapshotFlow(state=PENDING, target=ROLLBACK, current="v2", last="v1")
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["v1"])
        self.assertEqual(flow.current, "v1")
        self.assertEqual((flow.state, flow.target), (IDLE, NONE))

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


class RecoveryFlow:
    """Host model for production recovery when Journal cannot be trusted."""

    def __init__(self, journal="invalid", current="valid", runtime_match=True):
        self.journal = journal
        self.current = current
        self.runtime_match = runtime_match
        self.install_fail = False
        self.runtime_fail = False
        self.post_hash_fail = False
        self.reset_fail = False
        self.mounts = 0
        self.installs = []
        self.journal_reset = False
        self.source_used = None

    def process(self):
        if self.journal == "io":
            return "error"
        if self.journal not in ("missing", "invalid"):
            return "launch"
        self.mounts += 1
        if self.current != "valid":
            return "error"
        self.source_used = "CURRENT"
        if not self.runtime_match:
            self.installs = ["app", "gui", "therapy", "voice", "config"]
            if self.install_fail:
                return "error"
            if self.runtime_fail or self.post_hash_fail:
                return "error"
        if self.runtime_fail:
            return "error"
        if self.reset_fail:
            return "error"
        self.journal_reset = True
        return "launch"


class RecoveryFlowTest(unittest.TestCase):
    def test_missing_or_invalid_journal_uses_current_only(self):
        for journal in ("missing", "invalid"):
            flow = RecoveryFlow(journal=journal)
            self.assertEqual(flow.process(), "launch")
            self.assertEqual(flow.source_used, "CURRENT")
            self.assertEqual(flow.installs, [])
            self.assertTrue(flow.journal_reset)

    def test_journal_io_error_does_not_enter_recovery(self):
        flow = RecoveryFlow(journal="io")
        self.assertEqual(flow.process(), "error")
        self.assertEqual(flow.mounts, 0)
        self.assertFalse(flow.journal_reset)

    def test_current_hash_mismatch_reinstalls_all_components(self):
        flow = RecoveryFlow(runtime_match=False)
        self.assertEqual(flow.process(), "launch")
        self.assertEqual(flow.installs, ["app", "gui", "therapy", "voice", "config"])
        self.assertTrue(flow.journal_reset)

    def test_invalid_current_does_not_fallback_to_last_or_update(self):
        flow = RecoveryFlow(current="corrupt")
        self.assertEqual(flow.process(), "error")
        self.assertEqual(flow.installs, [])
        self.assertIsNone(flow.source_used)
        self.assertFalse(flow.journal_reset)

    def test_recovery_failures_keep_journal_invalid(self):
        for attribute in ("install_fail", "runtime_fail", "post_hash_fail", "reset_fail"):
            flow = RecoveryFlow(runtime_match=False)
            setattr(flow, attribute, True)
            self.assertEqual(flow.process(), "error", attribute)
            self.assertFalse(flow.journal_reset, attribute)


if __name__ == "__main__":
    unittest.main()
