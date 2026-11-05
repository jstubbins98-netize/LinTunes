#include "iPodFirmware.h"
#include <gio/gio.h>
#include <gpod/itdb.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <algorithm>
#include <cstdint>

namespace {
constexpr std::size_t maxImage = 8 * 1024 * 1024;
struct Model { const char* tag; int number; };
Model model(int generation) {
    switch (generation) {
        case ITDB_IPOD_GENERATION_FIRST:
        case ITDB_IPOD_GENERATION_SECOND: return {"1g2g", 19};
        case ITDB_IPOD_GENERATION_THIRD: return {"ip3g", 7};
        case ITDB_IPOD_GENERATION_FOURTH: return {"ip4g", 8};
        case ITDB_IPOD_GENERATION_PHOTO: return {"ipco", 3};
        case ITDB_IPOD_GENERATION_MINI_1: return {"mini", 9};
        case ITDB_IPOD_GENERATION_MINI_2: return {"mn2g", 11};
        case ITDB_IPOD_GENERATION_NANO_1: return {"nano", 4};
        case ITDB_IPOD_GENERATION_VIDEO_1:
        case ITDB_IPOD_GENERATION_VIDEO_2: return {"ipvd", 5};
        default: return {nullptr, 0};
    }
}
std::string sha256(const std::vector<unsigned char>& bytes) {
    gchar* hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes.data(), bytes.size());
    std::string result = hash ? hash : "";
    g_free(hash);
    return result;
}
bool readFd(int fd, std::vector<unsigned char>& bytes, std::string& error) {
    struct stat st {};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 8 + 1024 || st.st_size > static_cast<off_t>(maxImage)) {
        error = "Firmware must be a regular, model-tagged ipodpatcher image between 1 KiB and 8 MiB.";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(st.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t count = pread(fd, bytes.data() + offset, bytes.size() - offset, offset);
        if (count <= 0) { error = "Could not read the complete firmware image."; return false; }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}
struct Descriptor {
    int fd = -1;
    ~Descriptor() { if (fd >= 0) close(fd); }
};
std::string fdPath(int fd) {
    return "/proc/" + std::to_string(getpid()) + "/fd/" + std::to_string(fd);
}
std::string trustedTool(const char* name, std::string& error) {
    gchar* found = g_find_program_in_path(name);
    std::error_code ec;
    auto path = found ? std::filesystem::canonical(found, ec) : std::filesystem::path();
    g_free(found);
    if (path.empty() || ec) {
        error = std::string(name) + " is required. Install it system-wide before restoring firmware.";
        return {};
    }
    // pkexec must not elevate a user-writable executable or directory.
    for (auto part = path; !part.empty(); part = part.parent_path()) {
        struct stat st {};
        if (stat(part.c_str(), &st) != 0 || st.st_uid != 0 || (st.st_mode & 0022)) {
            error = std::string(name) + " and its parent directories must be root-owned and not group/world writable.";
            return {};
        }
        if (part == part.root_path()) break;
    }
    return path.string();
}
bool run(const std::string& patcher, const std::string& pkexec,
         const std::string& device, const char* action, int imageFd,
         const std::function<bool(std::string&)>& revalidate, std::string& error) {
    if (!revalidate(error)) return false;
    const std::string image = fdPath(imageFd);
    // Explicit device is essential: never invoke auto-scan/write or a shell.
    const gchar* argv[] = {pkexec.c_str(), patcher.c_str(), device.c_str(),
                           action, image.c_str(), nullptr};
    gchar* out = nullptr;
    gchar* err = nullptr;
    gint status = 0;
    GError* failure = nullptr;
    const bool started = g_spawn_sync(nullptr, const_cast<gchar**>(argv), nullptr,
        G_SPAWN_DEFAULT, nullptr, nullptr, &out, &err, &status, &failure);
    const bool successful = started && g_spawn_check_wait_status(status, &failure);
    std::string output = std::string(out ? out : "") + (err ? err : "");
    g_free(out);
    g_free(err);
    // Some historical builds report an [ERR] line despite a zero exit status.
    if (!successful || output.find("[ERR]") != std::string::npos) {
        error = std::string("ipodpatcher ") + action + " failed or authorization was cancelled.\n" +
            (failure ? failure->message : "") + "\n" + output.substr(0, 8192);
        g_clear_error(&failure);
        return false;
    }
    g_clear_error(&failure);
    return true;
}
}

iPodFirmware::~iPodFirmware() { if (fd_ >= 0) close(fd_); }
bool iPodFirmware::supported(int generation) { return model(generation).tag != nullptr; }

bool iPodFirmware::toolsAvailable(std::string& error) {
    return !trustedTool("ipodpatcher", error).empty() && !trustedTool("pkexec", error).empty();
}

bool iPodFirmware::validate(const std::vector<unsigned char>& bytes, std::string& error) const {
    if (bytes.size() < 8 + 1024 || bytes.size() > maxImage ||
        std::string(reinterpret_cast<const char*>(bytes.data() + 4), 4) != modelTag_) {
        error = "The firmware model tag does not match this iPod. Use a model-tagged .ipod image "
                "exported by ipodpatcher --read-firmware for the exact generation. Raw .bin, "
                "Apple .ipsw archives and encrypted images are not accepted. "
                "Do not supply a bootloader instead of full firmware.";
        return false;
    }
    const std::uint32_t expected = (std::uint32_t(bytes[0]) << 24) |
        (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) | bytes[3];
    std::uint32_t checksum = modelNumber_;
    for (std::size_t i = 8; i < bytes.size(); ++i) checksum += bytes[i];
    if (checksum != expected) {
        error = "Firmware checksum verification failed. The iPod has not been erased.";
        return false;
    }
    return true;
}

std::shared_ptr<iPodFirmware> iPodFirmware::load(const std::string& filename,
                                                int generation, std::string& error) {
    const auto identity = model(generation);
    if (!identity.tag) {
        error = "Firmware restoration is not supported for this generation. This path supports "
                "older iPods (1st–5th generation), mini 1/2, and nano 1 only. Modern classic, "
                "shuffle, later nano, and touch require different restore procedures.";
        return {};
    }
    auto image = std::shared_ptr<iPodFirmware>(new iPodFirmware);
    image->modelTag_ = identity.tag;
    image->modelNumber_ = identity.number;
    Descriptor input{open(filename.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    if (input.fd < 0 || !readFd(input.fd, image->bytes_, error)) {
        if (input.fd < 0) error = "Cannot open the selected firmware file (symlinks are not accepted).";
        return {};
    }
    if (!image->validate(image->bytes_, error)) return {};
    image->fd_ = memfd_create("lintunes-firmware", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (image->fd_ < 0) { error = "Cannot create an immutable firmware snapshot."; return {}; }
    std::size_t offset = 0;
    while (offset < image->bytes_.size()) {
        ssize_t count = write(image->fd_, image->bytes_.data() + offset, image->bytes_.size() - offset);
        if (count <= 0) { error = "Cannot snapshot the firmware image."; return {}; }
        offset += static_cast<std::size_t>(count);
    }
    if (fcntl(image->fd_, F_ADD_SEALS,
              F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0) {
        error = "Cannot seal the firmware snapshot against changes."; return {};
    }
    image->digest_ = sha256(image->bytes_);
    return image;
}

std::string iPodFirmware::description() const {
    return "Firmware model tag: " + modelTag_ + "\nFirmware SHA-256: " + digest_;
}

bool iPodFirmware::flash(const std::string& stableDevice, unsigned long long partitionSize,
                        const std::function<bool(std::string&)>& revalidate, std::string& error) const {
    if (std::filesystem::path(stableDevice).parent_path() != "/dev/disk/by-id" ||
        !validate(bytes_, error)) {
        if (error.empty()) error = "Firmware restore requires the verified iPod's stable by-id device.";
        return false;
    }
    const auto patcher = trustedTool("ipodpatcher", error);
    const auto pkexec = trustedTool("pkexec", error);
    if (patcher.empty() || pkexec.empty()) return false;
    Descriptor scratch{memfd_create("lintunes-firmware-readback", MFD_CLOEXEC)};
    if (scratch.fd < 0) { error = "Cannot create the firmware verification buffer."; return false; }
    std::vector<unsigned char> previous;
    if (!run(patcher, pkexec, stableDevice, "--read-firmware", scratch.fd, revalidate, error) ||
        !readFd(scratch.fd, previous, error) || !validate(previous, error)) return false;
    // Upstream ipodpatcher does not fully check partition capacity for a single
    // image. Conservatively refuse growth beyond the existing verified image.
    if (bytes_.size() > previous.size() || previous.size() > partitionSize) {
        error = "The replacement exceeds the existing firmware image/partition capacity. "
                "No firmware was written and FAT32 formatting was not started.";
        return false;
    }
    if (!run(patcher, pkexec, stableDevice, "--write-firmware", fd_, revalidate, error)) return false;
    std::vector<unsigned char> readback;
    if (!run(patcher, pkexec, stableDevice, "--read-firmware", scratch.fd, revalidate, error) ||
        !readFd(scratch.fd, readback, error) || readback != bytes_) {
        error = "Firmware may have been written, but read-back verification failed. "
                "FAT32 formatting was not started. Keep the iPod connected for recovery.\n" + error;
        return false;
    }
    return true;
}
