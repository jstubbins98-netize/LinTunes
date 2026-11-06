#!/usr/bin/env python3
"""Exercise build/install consent using mock commands; never install anything."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASH = shutil.which("bash")

with tempfile.TemporaryDirectory(prefix="lintunes-install-tests-") as temp:
    base = Path(temp)
    commands = base / "commands"
    commands.mkdir()
    log = base / "calls"

    def mock(name, body):
        path = commands / name
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)

    mock("cmake", """printf 'cmake' >> "$MOCK_LOG"
printf '\\t%s' "$@" >> "$MOCK_LOG"
printf '\\n' >> "$MOCK_LOG"
case "$1" in
  --build) exit "${MOCK_BUILD_EXIT:-0}" ;;
  --install) exit "${MOCK_INSTALL_EXIT:-0}" ;;
esac""")
    mock("id", 'printf "%s\\n" "${MOCK_UID:-1000}"')
    mock("nproc", 'echo 2')
    (commands / "dirname").symlink_to(shutil.which("dirname"))
    mock("sudo", """printf 'sudo\\n' >> "$MOCK_LOG"
exec "$@" """)

    def run(answer, **settings):
        log.write_text("")
        result = subprocess.run(
            [BASH, str(ROOT / "buildPod.sh"), "--skip-deps"],
            input=answer, capture_output=True, text=True, timeout=10,
            env={**os.environ, "PATH": str(commands), "MOCK_LOG": str(log),
                 "MOCK_UID": "1000", "MOCK_BUILD_EXIT": "0",
                 "MOCK_INSTALL_EXIT": "0", **settings})
        calls = [line.split("\t") for line in log.read_text().splitlines()]
        return result, calls

    def installs(calls):
        return [call for call in calls if call[:2] == ["cmake", "--install"]]

    for answer in ("\n", "n\n", "no\n", ""):
        result, calls = run(answer)
        assert result.returncode == 0 and not installs(calls), result.stdout + result.stderr
        assert ["sudo"] not in calls
    print("PASS: Enter, no, and EOF never install or request administrator access")

    for answer in ("y\n", "YES\n", "invalid\nyes\n"):
        result, calls = run(answer)
        assert result.returncode == 0, result.stdout + result.stderr
        assert installs(calls) == [["cmake", "--install", str(ROOT / "build"), "--prefix", "/usr/local"]]
        assert ["sudo"] in calls
        build_index = next(i for i, call in enumerate(calls) if call[:2] == ["cmake", "--build"])
        install_index = calls.index(installs(calls)[0])
        assert build_index < install_index
    print("PASS: Explicit consent installs through sudo only after building")

    result, calls = run("y\n", MOCK_UID="0")
    assert result.returncode == 0 and installs(calls) and ["sudo"] not in calls
    print("PASS: Already-root installation does not invoke sudo")

    result, calls = run("y\n", MOCK_BUILD_EXIT="7")
    assert result.returncode != 0 and not installs(calls)
    assert "Install LinTunes system-wide now?" not in result.stdout
    print("PASS: Failed compilation never prompts or installs")

    result, calls = run("y\n", MOCK_INSTALL_EXIT="1")
    assert result.returncode != 0 and "installation failed" in result.stderr
    assert "installed system-wide." not in result.stdout
    print("PASS: Installation/authorization failure is reported without claiming success")

    (commands / "sudo").unlink()
    result, calls = run("y\n")
    assert result.returncode != 0 and not installs(calls) and "sudo is required" in result.stderr
    print("PASS: Missing sudo fails explicitly without attempting installation")
