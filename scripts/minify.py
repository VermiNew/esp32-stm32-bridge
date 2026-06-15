#!/usr/bin/env python3
"""
minify.py — strip comments and blank lines from PS1/PSM1 and Python scripts.

Removes:
  - PS1/PSM1 : block comments <# ... #>, comment-only lines starting with #
               (preserves #Requires directives)
  - Python   : comment-only lines starting with # (preserves shebang on line 1)

Does NOT modify:
  - Inline comments appearing after code  (safe approach, no regex on strings)
  - String contents of any kind
  - #Requires directives in PowerShell

Limitation: PowerShell here-strings (@" ... "@) with blank lines inside them
            are handled correctly only if the blank lines are preserved (which
            they are — only comment-only lines are dropped, not blank lines
            inside here-strings).  Completely blank lines outside here-strings
            are always removed.

Usage:
    python scripts/minify.py                          # minify all scripts to dist/
    python scripts/minify.py --outdir build/min       # custom output directory
    python scripts/minify.py scripts/test.py          # specific file(s)
"""

import argparse
import re
import sys
from pathlib import Path

# Files to minify when no explicit list is given
_DEFAULT_TARGETS = [
    "flash.ps1",
    "test.ps1",
    "test.py",
    "Shared.psm1",
    "get-stm32flash.ps1",
    "get-stm32rtc.ps1",
]

# ---------------------------------------------------------------------------
# PowerShell / PSM1 minifier
# ---------------------------------------------------------------------------

def minify_ps1(src: str) -> str:
    # 1. Remove <# ... #> block comments (including multi-line)
    src = re.sub(r'<#.*?#>', '', src, flags=re.DOTALL)

    result = []
    in_herestring = False
    herestring_end = None

    for line in src.splitlines():
        stripped = line.lstrip()

        # Track here-strings so we never touch their content
        if not in_herestring:
            if stripped.startswith('@"') or stripped.startswith("@'"):
                in_herestring = True
                herestring_end = '"@' if stripped.startswith('@"') else "'@"
                result.append(line.rstrip())
                continue
        else:
            result.append(line.rstrip())
            if line.rstrip() == herestring_end:
                in_herestring = False
            continue

        # Outside here-strings: drop comment-only lines (keep #Requires)
        if stripped.startswith('#') and not stripped.startswith('#Requires'):
            continue

        result.append(line.rstrip())

    return _collapse_blanks(result)


# ---------------------------------------------------------------------------
# Python minifier
# ---------------------------------------------------------------------------

def minify_python(src: str) -> str:
    lines = src.splitlines()
    result = []

    for i, line in enumerate(lines):
        stripped = line.lstrip()

        # Always keep the shebang on line 1
        if i == 0 and stripped.startswith('#!'):
            result.append(line.rstrip())
            continue

        # Drop comment-only lines
        if stripped.startswith('#'):
            continue

        result.append(line.rstrip())

    return _collapse_blanks(result)


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def _collapse_blanks(lines: list[str]) -> str:
    """Remove all blank lines (safe: here-string blanks were already kept)."""
    out = [line for line in lines if line.strip()]
    return '\n'.join(out) + '\n'


def minify_file(src_path: Path, out_path: Path) -> tuple[int, int]:
    """Minify src_path -> out_path.  Returns (original_bytes, minified_bytes)."""
    src = src_path.read_text(encoding='utf-8')
    suffix = src_path.suffix.lower()

    if suffix in ('.ps1', '.psm1', '.psd1'):
        result = minify_ps1(src)
    elif suffix == '.py':
        result = minify_python(src)
    else:
        raise ValueError(f"Unsupported file type: {suffix}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(result, encoding='utf-8')
    return len(src.encode()), len(result.encode())


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('files', nargs='*', help='Files to minify (default: all scripts)')
    parser.add_argument('--outdir', default='dist', help='Output directory (default: dist)')
    args = parser.parse_args()

    scripts_dir = Path(__file__).parent
    out_dir = Path(args.outdir)

    if args.files:
        targets = [Path(f) for f in args.files]
    else:
        targets = [scripts_dir / name for name in _DEFAULT_TARGETS]

    ok = True
    total_orig = total_min = 0

    for src in targets:
        if not src.exists():
            print(f"[SKIP] {src} — not found", file=sys.stderr)
            continue
        out = out_dir / src.name
        try:
            orig, mini = minify_file(src, out)
            total_orig += orig
            total_min  += mini
            saved = orig - mini
            pct   = 100 * saved // orig if orig else 0
            print(f"[OK]  {src.name:30s}  {orig:>6} -> {mini:>6} bytes  (-{pct}%)")
        except Exception as exc:
            print(f"[ERR] {src.name}: {exc}", file=sys.stderr)
            ok = False

    if total_orig:
        pct = 100 * (total_orig - total_min) // total_orig
        print(f"\nTotal: {total_orig} -> {total_min} bytes  (-{pct}%)")
        print(f"Output: {out_dir.resolve()}")

    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
