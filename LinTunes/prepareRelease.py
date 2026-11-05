#!/usr/bin/env python3
"""Prepare source releases and native publishing recipes; never upload anything."""
import argparse
import datetime
import gzip
import hashlib
import io
import os
from pathlib import Path
import re
import tarfile
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent


def https_url(value):
    parsed = urlparse(value)
    # Values enter shell recipes and RPM macros.
    if (parsed.scheme != "https" or not parsed.netloc or parsed.username
            or not re.fullmatch(r"https://[A-Za-z0-9./_~:+?=&-]+", value)
            or any(part.endswith(".invalid") for part in [parsed.hostname or ""])):
        raise argparse.ArgumentTypeError("Use a public HTTPS URL without credentials or recipe metacharacters.")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--homepage", required=True, type=https_url)
    parser.add_argument("--source-url", required=True, type=https_url,
                        help="Final public download URL for the generated .tar.gz")
    parser.add_argument("--maintainer", required=True, help="Release maintainer: Name <email>")
    parser.add_argument("--copyright", required=True, help="Verified copyright years and holder(s)")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "generated-artifacts/releases")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9 ._-]+ <[A-Za-z0-9._+-]+@[A-Za-z0-9.-]+>", args.maintainer):
        parser.error("--maintainer must be Name <email> without recipe metacharacters")
    if any(c in args.copyright for c in "\r\n@%") or not args.copyright.strip():
        parser.error("--copyright must be a nonempty single line without @ or %")
    match = re.search(r"project\(lintunes VERSION (\d+\.\d+\.\d+)", (ROOT / "CMakeLists.txt").read_text())
    if not match:
        parser.error("Cannot read release version from CMakeLists.txt")
    version = match[1]
    if urlparse(args.source_url).path.rsplit("/", 1)[-1] != f"lintunes-{version}.tar.gz":
        parser.error(f"--source-url must end in lintunes-{version}.tar.gz")
    try:
        epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
        date = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    except (ValueError, OverflowError, OSError):
        parser.error("SOURCE_DATE_EPOCH must be a valid Unix timestamp")
    out = args.output_dir.resolve() / version
    if out.exists():
        parser.error(f"Output already exists: {out}; choose a fresh --output-dir")
    # Only an explicit allowlist is shipped: no iPod data, builds, secrets, or other editions.
    sources = [ROOT / name for name in (
        "CMakeLists.txt", "LICENSE", "README.md", "manual.md", "buildPod.sh",
        "packageLinux.sh", "prepareRelease.py", "PUBLISHING.md")]
    for directory in ("src", "resources", "cmake", "packaging", "tests"):
        sources.extend(p for p in sorted((ROOT / directory).rglob("*")) if p.is_file())
    for source in sources:
        if source.is_symlink() or not source.is_file() or ROOT not in source.resolve().parents:
            parser.error(f"Missing or unsafe release input: {source}")
    buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode="wb", filename="", mtime=epoch) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as archive:
            for path in sources:
                data = path.read_bytes()
                info = tarfile.TarInfo(f"lintunes-{version}/{path.relative_to(ROOT)}")
                info.size = len(data)
                info.mtime = epoch
                info.mode = 0o755 if path.name.endswith(".sh") else 0o644
                archive.addfile(info, io.BytesIO(data))
    payload = buffer.getvalue()
    digest = hashlib.sha256(payload).hexdigest()
    values = {
        "VERSION": version, "HOMEPAGE": args.homepage, "SOURCE_URL": args.source_url,
        "MAINTAINER": args.maintainer, "COPYRIGHT": args.copyright, "SHA256": digest,
        "RPM_DATE": date.strftime("%a %b %d %Y"),
    }
    out.mkdir(parents=True)
    archive_name = f"lintunes-{version}.tar.gz"
    (out / archive_name).write_bytes(payload)
    (out / "SHA256SUMS").write_text(f"{digest}  {archive_name}\n")
    templates = ROOT / "packaging/release"
    for template in templates.rglob("*"):
        if not template.is_file():
            continue
        target = out / template.relative_to(templates)
        if target.suffix == ".in":
            target = target.with_suffix("")
        target.parent.mkdir(parents=True, exist_ok=True)
        content = template.read_text()
        for key, value in values.items():
            content = content.replace(f"@{key}@", value)
        target.write_text(content)
    print(f"Prepared {out}\nSHA256: {digest}")
    print("Not uploaded. Follow PUBLISHING.md for native builds, signing, and repository submission.")


if __name__ == "__main__":
    main()
