"""Application ELF load-segment reader."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


class PackageToolError(RuntimeError):
    """The supplied input cannot form a valid release package."""


@dataclass(frozen=True)
class LoadSegment:
    """One file-backed load segment, described by ELF load addresses."""

    physical_address: int
    virtual_address: int
    data: bytes

    @property
    def end_address(self) -> int:
        return self.physical_address + len(self.data)


def read_load_segments(elf_path: Path) -> tuple[LoadSegment, ...]:
    """Return all non-empty PT_LOAD segments from an Application ELF."""

    try:
        from elftools.elf.elffile import ELFFile
    except ModuleNotFoundError as error:
        raise PackageToolError(
            "pyelftools is required; install Tools/UpgradePackage/requirements.txt"
        ) from error

    try:
        stream = elf_path.open("rb")
    except OSError as error:
        raise PackageToolError(f"cannot open ELF {elf_path}: {error}") from error

    with stream:
        try:
            elf = ELFFile(stream)
        except Exception as error:
            raise PackageToolError(f"invalid ELF {elf_path}: {error}") from error

        segments: list[LoadSegment] = []
        for index, segment in enumerate(elf.iter_segments()):
            if segment["p_type"] != "PT_LOAD":
                continue
            file_size = int(segment["p_filesz"])
            if file_size == 0:
                continue
            data = segment.data()
            if len(data) != file_size:
                raise PackageToolError(
                    f"PT_LOAD segment {index} data length does not match p_filesz"
                )
            segments.append(
                LoadSegment(
                    physical_address=int(segment["p_paddr"]),
                    virtual_address=int(segment["p_vaddr"]),
                    data=data,
                )
            )

    if not segments:
        raise PackageToolError("ELF has no file-backed PT_LOAD segments")
    return tuple(segments)
