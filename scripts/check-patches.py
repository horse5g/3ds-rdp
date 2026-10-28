#!/usr/bin/env python3
"""Validate unified diff hunk lengths before expensive ARM11 CI builds.

This checks patch syntax/counts, not whether a patch applies to upstream source.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATTERNS = sorted((ROOT / "patches").glob("freerdp-*.patch"))
HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")

def validate(path: pathlib.Path) -> list[str]:
    problems: list[str] = []
    hunks = 0
    expected_old = expected_new = old = new = 0
    active = False

    def verify() -> None:
        if active and (old != expected_old or new != expected_new):
            problems.append(
                f"{path.name}: hunk {hunks}: expected {expected_old}/{expected_new}"
                f" old/new lines, found {old}/{new}"
            )

    for num, line in enumerate(path.read_text().splitlines(), 1):
        match = HUNK.match(line)
        if match:
            verify()
            active = True
            hunks += 1
            expected_old = int(match.group(2) or 1)
            expected_new = int(match.group(4) or 1)
            old = new = 0
        elif line.startswith(("diff --git ", "--- ", "+++ ")):
            verify()
            active = False
        elif active:
            if line.startswith(" "):
                old += 1
                new += 1
            elif line.startswith("-"):
                old += 1
            elif line.startswith("+"):
                new += 1
            elif line == r"\ No newline at end of file":
                pass
            else:
                problems.append(f"{path.name}:{num}: unrecognized hunk line")
    verify()
    if not hunks:
        problems.append(f"{path.name}: no unified diff hunks")
    return problems

def main() -> int:
    if not PATTERNS:
        print("No FreeRDP patch files found", file=sys.stderr)
        return 1
    problems = [problem for path in PATTERNS for problem in validate(path)]
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"Validated hunk lengths for {len(PATTERNS)} FreeRDP patches")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
