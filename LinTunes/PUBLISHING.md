# Publishing LinTunes for Linux

LinTunes has two packaging paths:

* `packageLinux.sh` packages an existing **native local build** for installation
  with apt, dnf/zypper, or pacman.
* `prepareRelease.py` prepares a **source release** and Fedora RPM and
  AUR recipes for public distribution. It never uploads, signs, installs, or
  submits anything.

These recipes do not mean LinTunes is already in any public repository.
MacTunes and WinTunes are not included.

## 1. Release prerequisites

1. Choose a public project homepage and an HTTPS download host. This workspace
   has no configured public upstream URL; do not use a development preview URL.
2. Verify the declared `GPL-3.0-or-later` licensing, copyright holders, contributor
   permissions, and compatibility of dependencies before redistribution.
   `LICENSE` contains GPL version 3; README permits version 3 or later.
3. Set the release version in `CMakeLists.txt`. Never replace an already published
   source archive with different content under the same version.
4. Review the source and run native build/safety tests. No real iPod is needed
   for the mocked safety checks. Physical-device validation must use backed-up
   test hardware and explicit erase consent.
5. Select a stable source timestamp, such as the release commit's Unix timestamp.
   The helper defaults to epoch zero for deterministic output; set
   `SOURCE_DATE_EPOCH` for meaningful changelog dates.

## 2. Generate the release

From `LinTunes/`, substitute your actual public details below:

```sh
export SOURCE_DATE_EPOCH="$(git log -1 --format=%ct)"
python3 prepareRelease.py \
  --homepage https://YOUR-PUBLIC-HOST/lintunes \
  --source-url https://YOUR-PUBLIC-HOST/releases/lintunes-1.0.0.tar.gz \
  --maintainer 'YOUR NAME <YOUR-EMAIL@YOUR-DOMAIN>' \
  --copyright 'YEARS COPYRIGHT HOLDER'
```

Output is `generated-artifacts/releases/1.0.0/`:

* `lintunes-1.0.0.tar.gz`: source only, including GPL text and build instructions.
* `SHA256SUMS`: source checksum.
* `lintunes.spec`: Fedora-oriented source RPM recipe.
* `PKGBUILD`: source-building AUR recipe with the actual SHA-256 checksum.

An explicit source allowlist excludes other editions, build outputs, and
workspace configuration. Existing output directories are refused rather than
overwritten. Identical inputs and timestamp produce identical source archives.
Checksums provide integrity, not publisher authentication.

Upload the archive to the exact URL supplied only after review. Optionally sign
`SHA256SUMS` with your release signing key (`gpg --armor --detach-sign SHA256SUMS`)
and publish the signature and public-key fingerprint through a trusted channel.
The helper does not create or handle signing keys.

## 3. Fedora: dnf / yum

Use a native Fedora machine with `rpm-build`, `rpmdevtools`, CMake/macros and
the spec's BuildRequires available. VLC and some codecs may need additional
repositories depending on the distribution/version and its codec policies.

```sh
rpmdev-setuptree
cp lintunes-1.0.0.tar.gz ~/rpmbuild/SOURCES/
cp lintunes.spec ~/rpmbuild/SPECS/
rpmbuild -bs ~/rpmbuild/SPECS/lintunes.spec
rpmbuild -ba ~/rpmbuild/SPECS/lintunes.spec
rpmlint ~/rpmbuild/SPECS/lintunes.spec ~/rpmbuild/RPMS/*/lintunes-*.rpm
```

For release validation, rebuild the source RPM with `mock` for each target
Fedora release and architecture, then test the resulting RPM on that target.
Do not assume RHEL or openSUSE has identical package names/macros or available
libgpod/VLC versions. The local CPack RPM path supports openSUSE plugin names,
but this source spec targets Fedora and needs a separate port/review elsewhere.

For a private/public dnf repository, sign RPMs with `rpmsign --addsign`,
generate metadata with `createrepo_c`, and optionally sign `repodata/repomd.xml`.
Publish over HTTPS with `gpgcheck=1`, and `repo_gpgcheck=1` if metadata is signed;
provide the verified public key. COPR is another source-build hosting option,
subject to its policies. Fedora inclusion requires formal package review.

## 4. Arch: AUR / pacman

The generated release `PKGBUILD` builds from the public source archive. The
older `build/packaging/PKGBUILD` is **local-binary-only** and must not go to AUR.

As a normal user on Arch, with `base-devel`, build dependencies and `namcap`:

```sh
makepkg --verifysource
makepkg --syncdeps --cleanbuild
namcap PKGBUILD
namcap lintunes-*.pkg.tar.zst
makepkg --printsrcinfo > .SRCINFO
```

Review/test in a clean Arch build environment before publishing. Declared
`x86_64` and `aarch64` targets must each be validated on appropriate builders;
declarations are not claims of successful hardware testing. Submit only the
reviewed `PKGBUILD` and `.SRCINFO` (and any necessary packaging patches) to an
AUR repository after checking name availability and AUR submission rules.
Do not upload binary packages to AUR. AUR availability does not put a package
in Arch's official pacman repositories.

A separately hosted binary pacman repository uses signed packages and a
repository database generated by `repo-add`; distribute its signing key through
a trusted channel. Official Arch inclusion is a separate maintainer process.

## 5. Release checklist

* Check copyright/dependency licenses, valid upstream URLs and archive checksum.
* Native source builds pass on **every** advertised distro/release/architecture.
* Run `bash tests/run-ipod-safety.sh`,
  `bash tests/run-packaging-checks.sh /path/to/native/build`, and
  `python3 tests/run-release-checks.py`.
* Run `rpmlint` or `namcap` on the corresponding target.
* Install/upgrade/remove on clean targets; user library data must remain intact.
* Confirm packaged VLC plugins, GStreamer playback and external CD tools work.
* Confirm Linux still refuses Mac/HFS writes, warns before FAT32 erasure,
  defaults to Cancel, and requires typed consent. Unsupported partition layouts
  must remain blocked. Packages must never install setuid helpers or run the
  application as root; UDisks/Polkit handles formatting authorization.
* Sign and publish immutable source and per-target packages; verify signatures
  and installation from the repository before announcing it.

Flatpak/Snap are not provided: sandboxed raw/removable-device operations need a
separate security design before LinTunes can promise full iPod functionality.

### Official references

* https://docs.fedoraproject.org/en-US/packaging-guidelines/
* https://wiki.archlinux.org/title/Creating_packages
* https://wiki.archlinux.org/title/AUR_submission_guidelines
