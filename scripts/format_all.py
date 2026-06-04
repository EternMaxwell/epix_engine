#!/usr/bin/env python3
"""
Format all engine source, header, and module files using clang-format.

Usage:
    python scripts/format_all.py                # Format all engine files in-place
    python scripts/format_all.py --check        # Check only (exit 1 if any file needs formatting)
    python scripts/format_all.py --staged       # Format only git-staged engine files
    python scripts/format_all.py -c clang-format-23  # Use a specific clang-format binary
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
ENGINE_DIR = PROJECT_ROOT / "epix_engine"
CLANG_FORMAT_CONFIG = PROJECT_ROOT / ".clang-format"

EXTS = {".cpp", ".cc", ".cxx", ".c", ".hpp", ".hh", ".hxx", ".h", ".cppm", ".ixx", ".mpp"}


def collect_files(dirs: list[Path], staged_only: bool = False) -> list[Path]:
    if staged_only:
        result = subprocess.run(
            ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
            capture_output=True, text=True, cwd=PROJECT_ROOT,
        )
        if result.returncode != 0:
            return []
        files = []
        for line in result.stdout.strip().splitlines():
            f = PROJECT_ROOT / line.strip()
            if f.suffix in EXTS and f.exists():
                files.append(f)
        return sorted(files)

    files = []
    for d in dirs:
        if not d.is_dir():
            continue
        for root, _, names in os.walk(d):
            for name in names:
                f = Path(root) / name
                if f.suffix in EXTS:
                    files.append(f)
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description="Format epix_engine source/header/module files.")
    parser.add_argument("--check", action="store_true", help="Exit 1 if any file needs formatting.")
    parser.add_argument("--staged", action="store_true", help="Only format git-staged files.")
    parser.add_argument("--path", type=Path, nargs="+", help="Additional directories to include.")
    parser.add_argument("-c", "--clang-format", type=str, default="clang-format", help="clang-format binary.")
    parser.add_argument("--max-size", type=int, default=1_000_000, help="Skip files larger than N bytes (0=unlimited).")
    parser.add_argument("-t", "--timeout", type=int, default=10, help="Seconds per file timeout.")
    args = parser.parse_args()

    # Locate clang-format
    cf = args.clang_format
    if subprocess.run(["which", cf], capture_output=True).returncode != 0:
        print(f"Error: '{cf}' not found.", file=sys.stderr)
        return 1

    ver = subprocess.run([cf, "--version"], capture_output=True, text=True)
    print(f"Using: {cf}  |  {ver.stdout.splitlines()[0].strip()}")
    if CLANG_FORMAT_CONFIG.exists():
        print(f"Config: {CLANG_FORMAT_CONFIG}")

    # Collect and filter files
    dirs = [ENGINE_DIR] + (args.path or [])
    files = collect_files(dirs, staged_only=args.staged)

    if args.max_size > 0:
        kept, skipped = [], []
        for f in files:
            (skipped if f.stat().st_size > args.max_size else kept).append(f)
        for f in skipped:
            print(f"Skip (size {f.stat().st_size:,}): {f.relative_to(PROJECT_ROOT)}")
        files = kept

    print(f"Files: {len(files)}")

    total = len(files)

    if args.check:
        dirty = 0
        for i, f in enumerate(files, 1):
            rel = f.relative_to(PROJECT_ROOT)
            try:
                r = subprocess.run(
                    [cf, "--dry-run", "--Werror", str(f)],
                    capture_output=True, text=True, timeout=args.timeout,
                )
            except subprocess.TimeoutExpired:
                dirty += 1
                print(f"\r\033[KTIMEOUT [{i}/{total}]: {rel}")
                continue
            if r.returncode != 0:
                dirty += 1
                print(f"\r\033[KNEEDS-FMT [{i}/{total}]: {rel}")
            else:
                print(f"\r\033[K[{i}/{total}] {rel}", end="", flush=True)
        print(f"\r\033[K", end="")
        if dirty:
            print(f"{dirty} file(s) need formatting, {total - dirty} clean.")
            return 1
        print("All files properly formatted.")
        return 0

    # Format in-place
    ok, fail = 0, 0
    for i, f in enumerate(files, 1):
        rel = f.relative_to(PROJECT_ROOT)
        try:
            r = subprocess.run(
                [cf, "-i", str(f)],
                capture_output=True, text=True, timeout=args.timeout,
            )
        except subprocess.TimeoutExpired:
            fail += 1
            print(f"\r\033[KTIMEOUT [{i}/{total}]: {rel}")
            continue
        if r.returncode != 0:
            fail += 1
            print(f"\r\033[KFAIL [{i}/{total}]: {rel}  ({r.stderr.strip()})")
        else:
            ok += 1
            print(f"\r\033[K[{i}/{total}] {rel}", end="", flush=True)

    print(f"\r\033[K", end="")
    print(f"Done: {ok} formatted, {fail} failed, {total} total.")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
