#!/usr/bin/env python3
"""Reject unsafe entries before extracting a Monzero binary package."""

from __future__ import annotations

import pathlib
import stat
import sys
import tarfile
import zipfile

MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_EXTRACTED_BYTES = 512 * 1024 * 1024
MAX_ENTRIES = 256


def fail(message: str) -> None:
    raise SystemExit(message)


def validate_path(name: str, seen: set[str]) -> None:
    if not name or "\\" in name or "\x00" in name:
        fail(f"Archive contains an unsafe path: {name!r}")
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        fail(f"Archive contains an unsafe path: {name}")
    canonical = "/".join(path.parts)
    if name.rstrip("/") != canonical:
        fail(f"Archive contains a noncanonical path: {name}")
    if canonical in seen:
        fail(f"Archive contains a duplicate path: {name}")
    seen.add(canonical)


def validate_tar(path: pathlib.Path) -> None:
    seen: set[str] = set()
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        if not members:
            fail("Archive is empty")
        if len(members) > MAX_ENTRIES:
            fail("Archive contains too many entries")
        total_size = 0
        for member in members:
            validate_path(member.name, seen)
            if not (member.isfile() or member.isdir()):
                fail(f"Archive contains a link or special file: {member.name}")
            total_size += member.size
            if member.size > MAX_EXTRACTED_BYTES or total_size > MAX_EXTRACTED_BYTES:
                fail("Archive exceeds the extracted-size limit")


def validate_zip(path: pathlib.Path) -> None:
    seen: set[str] = set()
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if not entries:
            fail("Archive is empty")
        if len(entries) > MAX_ENTRIES:
            fail("Archive contains too many entries")
        total_size = 0
        for entry in entries:
            validate_path(entry.filename.rstrip("/"), seen)
            mode = (entry.external_attr >> 16) & 0xFFFF
            file_type = stat.S_IFMT(mode)
            if entry.is_dir():
                if file_type not in (0, stat.S_IFDIR):
                    fail(f"Archive directory has an invalid type: {entry.filename}")
            elif file_type not in (0, stat.S_IFREG):
                fail(f"Archive contains a link or special file: {entry.filename}")
            total_size += entry.file_size
            if entry.file_size > MAX_EXTRACTED_BYTES or total_size > MAX_EXTRACTED_BYTES:
                fail("Archive exceeds the extracted-size limit")


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[1] not in {"tar.gz", "zip"}:
        fail(f"Usage: {sys.argv[0]} <tar.gz|zip> <archive>")
    path = pathlib.Path(sys.argv[2])
    if not path.is_file():
        fail(f"Archive not found: {path}")
    if path.stat().st_size > MAX_ARCHIVE_BYTES:
        fail("Archive exceeds the compressed-size limit")
    try:
        (validate_tar if sys.argv[1] == "tar.gz" else validate_zip)(path)
    except (tarfile.TarError, zipfile.BadZipFile, OSError) as error:
        fail(f"Invalid archive: {error}")


if __name__ == "__main__":
    main()
