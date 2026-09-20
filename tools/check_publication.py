#!/usr/bin/env python3
"""Check the source release boundary and local Markdown links; no network I/O."""
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {"README.md", "LICENSE", "CONTRIBUTING.md", ".gitignore", "pyproject.toml"}
DIRECTORIES = {"ks37", "tests", "examples", "device", "map", "docs", "tools", ".github"}
SUFFIXES = {".py", ".md", ".json", ".mmd", ".c", ".h", ".S", ".ld", ".yml", ".toml"}
IGNORED = {".git", "__pycache__", ".venv", "build", "local", "dist"}


def selected_files():
    for path in sorted(ROOT.rglob("*")):
        relative = path.relative_to(ROOT)
        if any(part in IGNORED for part in relative.parts):
            continue
        if path.is_symlink():
            yield path
        elif path.is_file():
            yield path


def check():
    errors = []
    files = list(selected_files())
    for path in files:
        relative = path.relative_to(ROOT)
        if path.is_symlink():
            errors.append(f"symlink is outside release policy: {relative}")
            continue
        if len(relative.parts) == 1:
            if relative.name not in ROOT_FILES:
                errors.append(f"unlisted root file: {relative}")
        elif relative.parts[0] not in DIRECTORIES:
            errors.append(f"unlisted directory: {relative}")
        if relative.name not in ROOT_FILES | {"Makefile"} and path.suffix not in SUFFIXES:
            errors.append(f"non-source file: {relative}")
        data = path.read_bytes()
        if len(data) > 250_000:
            errors.append(f"oversized source file; review before inclusion: {relative}")
        try:
            content = data.decode("utf-8")
        except UnicodeDecodeError:
            errors.append(f"non-UTF8 content: {relative}")
            continue
        if "\0" in content:
            errors.append(f"binary content: {relative}")
        # Individual short instruction anchors are not complete executable dumps.
        if re.search(r"(?im)^[0-9a-f]{1024,}$", content):
            errors.append(f"possible embedded firmware hex: {relative}")
        if path.suffix == ".md":
            for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", content):
                target = target.strip("<>").split("#", 1)[0]
                if not target or re.match(r"[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                    continue
                resolved = (path.parent / unquote(target)).resolve()
                if not resolved.is_relative_to(ROOT) or not resolved.exists():
                    errors.append(f"missing/nonportable link: {relative}: {target}")
    # An ignored file accidentally staged with -f must not escape the audit.
    if (ROOT / ".git").exists():
        result = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                                capture_output=True, check=True)
        public = {str(p.relative_to(ROOT)) for p in files}
        for name in result.stdout.decode().split("\0"):
            if name and name not in public:
                errors.append(f"tracked file outside source release: {name}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"PASS: {len(files)} source files; local links and release boundary checked")
    print("Source packaging only; no firmware or device validation.")
    return 0


if __name__ == "__main__":
    raise SystemExit(check())
