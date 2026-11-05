#!/usr/bin/env python3
"""Non-destructive release generation checks; no native package tools required."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.search(r"project\(lintunes VERSION (\d+\.\d+\.\d+)",
                    (ROOT / "CMakeLists.txt").read_text())[1]


def generate(output, *extra):
    return subprocess.run([
        sys.executable, str(ROOT / "prepareRelease.py"),
        "--homepage", "https://example.org/lintunes",
        "--source-url", f"https://example.org/lintunes-{VERSION}.tar.gz",
        "--maintainer", "Test Maintainer <test@example.org>",
        "--copyright", "2026 Test Contributors",
        "--output-dir", str(output), *extra,
    ], capture_output=True, text=True, env={**os.environ, "SOURCE_DATE_EPOCH": "1793404800"})


with tempfile.TemporaryDirectory() as temp:
    base = Path(temp)
    for name in ("one", "two"):
        result = generate(base / name)
        assert result.returncode == 0, result.stderr
    out = base / "one" / VERSION
    archive = out / f"lintunes-{VERSION}.tar.gz"
    assert archive.read_bytes() == (base / "two" / VERSION / archive.name).read_bytes()
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    assert (out / "SHA256SUMS").read_text() == f"{digest}  {archive.name}\n"
    assert digest in (out / "PKGBUILD").read_text()
    assert generate(base / "one").returncode != 0
    assert generate(base / "bad", "--maintainer", "Name $(bad) <x@example.org>").returncode != 0
    assert generate(base / "bad", "--homepage", "https://example.org/%bad").returncode != 0
    assert generate(base / "bad", "--source-url", "https://example.org/wrong.tar.gz").returncode != 0
    with tarfile.open(archive) as source:
        names = source.getnames()
        assert f"lintunes-{VERSION}/LICENSE" in names
        assert f"lintunes-{VERSION}/src/iPodVolume.cpp" in names
        assert all(n.startswith(f"lintunes-{VERSION}/") for n in names)
        assert not any("/generated-artifacts/" in n or "/build/" in n or "MacTunes" in n for n in names)
    for recipe in out.rglob("*"):
        if recipe.is_file() and recipe.suffix != ".gz":
            assert "@VERSION@" not in recipe.read_text()
    assert not (out / "debian").exists()
    assert not any("/debian/" in name for name in names)
    assert (out / "lintunes.spec").is_file()
    subprocess.run(["bash", "-n", str(out / "PKGBUILD")], check=True)
print("PASS: deterministic source archive, recipes, checksums, layout, and invalid-input/overwrite guards.")
