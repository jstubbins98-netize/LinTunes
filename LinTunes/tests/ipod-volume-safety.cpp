#include "iPodVolume.h"
#include "iPodSync.h"
#include <sys/statfs.h>
#include <linux/magic.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Simulate filesystem types without mounting, formatting, or opening a disk.
extern "C" int __real_statfs(const char*, struct statfs*);
extern "C" int __real_access(const char*, int);
extern "C" int __wrap_statfs(const char* path, struct statfs* result) {
    if (std::strncmp(path, "/__lintunes_test__/", 19) != 0)
        return __real_statfs(path, result);
    *result = {};
    const std::string p(path);
    if (p.find("missing") != std::string::npos) { errno = ENOENT; return -1; }
    result->f_type = p.find("hfsplus") != std::string::npos ? 0x482b :
                     p.find("hfs") != std::string::npos ? 0x4244 :
                     p.find("fat") != std::string::npos ? MSDOS_SUPER_MAGIC : 0;
    return 0;
}
extern "C" int __wrap_access(const char* path, int mode) {
    if (std::strncmp(path, "/__lintunes_test__/", 19) != 0)
        return __real_access(path, mode);
    if (std::string(path).find("readonly") != std::string::npos) {
        errno = EACCES; return -1;
    }
    return 0;
}
static void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    std::cout << "PASS: " << name << '\n';
}

int main() {
    std::string error, mount;
    check(iPodVolume::filesystemAt("/__lintunes_test__/hfs") == "hfs", "HFS detection");
    check(iPodVolume::filesystemAt("/__lintunes_test__/hfsplus") == "hfsplus", "HFS+ detection");
    check(iPodVolume::filesystemAt("/__lintunes_test__/fat") == "vfat", "Windows FAT detection");
    check(!iPodVolume::windowsWritable("/__lintunes_test__/hfs", error) &&
          error.find("Mac-formatted") != std::string::npos, "Mac format write refusal");
    check(!iPodVolume::windowsWritable("/__lintunes_test__/hfsplus", error), "HFS+ write refusal");
    check(!iPodVolume::windowsWritable("/__lintunes_test__/unknown", error), "Unknown format write refusal");
    check(!iPodVolume::windowsWritable("/__lintunes_test__/missing", error), "Missing mount write refusal");
    check(!iPodVolume::windowsWritable("/__lintunes_test__/fat-readonly", error), "Read-only FAT refusal");
    check(iPodVolume::windowsWritable("/__lintunes_test__/fat", error), "Writable FAT accepted");
    iPodSync sync;
    check(!sync.connect("/__lintunes_test__/hfsplus") &&
          sync.lastError().find("Mac-formatted") != std::string::npos,
          "Connection rejects HFS before libgpod parsing");
    check(!sync.connect("/__lintunes_test__/fat-readonly"), "Connection rejects read-only FAT");
    check(!sync.addTrack(Track{}), "Disconnected track copy rejected");
    check(!sync.removeTrack("1"), "Disconnected track removal rejected");
    check(!sync.syncPlaylist("test", {}), "Disconnected playlist sync rejected");
    check(!sync.writeDatabase(), "Disconnected database write rejected");
    check(!sync.resetMusicDatabase(), "Disconnected destructive reset rejected");
    iPodVolume target;
    check(!target.format(mount, error) && error.find("No verified") != std::string::npos,
          "Unverified formatting rejected before any D-Bus call");
    check(!target.inspect("/__lintunes_test__/missing", error),
          "Missing formatting mount rejected");
}
