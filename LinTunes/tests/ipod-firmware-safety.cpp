#include "iPodFirmware.h"
#include <gio/gio.h>
#include <gpod/itdb.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

// Every child-process launch is intercepted. No ipodpatcher, pkexec, block
// device, or privileged command can be invoked by this test.
namespace {
std::vector<unsigned char> firmware, previous;
std::vector<std::string> commands;
bool corruptReadback = false, denied = false, reportError = false, tools = true;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS: " << message << '\n';
}
std::vector<unsigned char> image(const char* tag, int model, std::size_t length = 4096) {
    std::vector<unsigned char> bytes(length, 0x42);
    std::uint32_t checksum = model;
    for (std::size_t i = 8; i < bytes.size(); ++i) checksum += bytes[i];
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(checksum >> (24 - i * 8));
    std::copy(tag, tag + 4, bytes.begin() + 4);
    return bytes;
}
void write(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!out) throw std::runtime_error("Test fixture write failed");
}
}
extern "C" int __real_stat(const char*, struct stat*);
extern "C" int __wrap_stat(const char* path, struct stat* st) {
    const int result = __real_stat(path, st);
    if (result == 0) { st->st_uid = 0; st->st_mode &= ~0022; }
    return result;
}
extern "C" gchar* __wrap_g_find_program_in_path(const gchar*) {
    return tools ? g_strdup("/bin/sh") : nullptr; // Real harmless path, never executed.
}
extern "C" gboolean __wrap_g_spawn_sync(
    const gchar*, gchar** argv, gchar**, GSpawnFlags, GSpawnChildSetupFunc,
    gpointer, gchar** output, gchar** errors, gint* status, GError**) {
    check(std::string(argv[2]) == "/dev/disk/by-id/usb-test-ipod" && argv[5] == nullptr,
          "ipodpatcher gets the explicit stable device and no extra options");
    const std::string action(argv[3]);
    commands.push_back(action);
    *output = g_strdup("");
    *errors = g_strdup(reportError ? "[ERR] Mock failure" : "");
    *status = denied ? 126 << 8 : 0;
    if (denied || reportError) return TRUE;
    if (action == "--read-firmware") {
        auto bytes = commands.size() == 1 ? previous : firmware;
        if (corruptReadback && commands.size() > 1) bytes.back() ^= 1;
        write(argv[4], bytes);
    } else if (action == "--write-firmware") {
        std::ifstream in(argv[4], std::ios::binary);
        std::vector<unsigned char> actual{std::istreambuf_iterator<char>(in), {}};
        check(actual == firmware, "Flasher receives the immutable validated image, not a changed original file");
    } else throw std::runtime_error("Unexpected destructive action");
    return TRUE;
}
int main() {
    gchar* temp = g_dir_make_tmp("lintunes-firmware-test-XXXXXX", nullptr);
    if (!temp) return 1;
    const std::filesystem::path dir(temp);
    g_free(temp);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    try {
        const auto path = (dir/"firmware.ipod").string();
        std::string error;
        firmware = image("mini", 9);
        previous = firmware;
        write(path, firmware);
        auto selected = iPodFirmware::load(path, ITDB_IPOD_GENERATION_MINI_1, error);
        check(bool(selected), "Valid mini image accepted");
        check(!iPodFirmware::load(path, ITDB_IPOD_GENERATION_MINI_2, error),
              "Wrong model tag refused before any process launch");
        check(!iPodFirmware::load(path, ITDB_IPOD_GENERATION_CLASSIC_1, error),
              "Modern classic generation refused");
        check(!iPodFirmware::load(path, ITDB_IPOD_GENERATION_SHUFFLE_1, error),
              "Shuffle generation refused");
        check(!iPodFirmware::load(path, ITDB_IPOD_GENERATION_NANO_2, error),
              "Encrypted nano generation refused");
        auto damaged = firmware; damaged.back() ^= 1; write(path, damaged);
        check(!iPodFirmware::load(path, ITDB_IPOD_GENERATION_MINI_1, error),
              "Corrupt checksum refused");
        write(path, std::vector<unsigned char>(100, 0));
        check(!iPodFirmware::load(path, ITDB_IPOD_GENERATION_MINI_1, error), "Truncated image refused");
        write(path, firmware);
        std::filesystem::create_symlink(path, dir/"linked.ipod");
        check(!iPodFirmware::load((dir/"linked.ipod").string(), ITDB_IPOD_GENERATION_MINI_1, error),
              "Symlink firmware refused");
        write(path, damaged); // Changing the source cannot change the selected snapshot.
        auto identity = [](std::string&) { return true; };
        error.clear();
        check(!selected->flash("/dev/sda", 1024 * 1024, identity, error) && commands.empty(),
              "Free-form device path refused without launching anything");
        commands.clear(); error.clear();
        check(selected->flash("/dev/disk/by-id/usb-test-ipod", 1024 * 1024, identity, error) &&
              commands == std::vector<std::string>{"--read-firmware","--write-firmware","--read-firmware"},
              "Read-before-write and exact readback verification succeed");
        commands.clear(); corruptReadback = true;
        check(!selected->flash("/dev/disk/by-id/usb-test-ipod", 1024 * 1024, identity, error) &&
              error.find("read-back") != std::string::npos, "Readback mismatch fails restoration");
        commands.clear(); corruptReadback = false; denied = true;
        check(!selected->flash("/dev/disk/by-id/usb-test-ipod", 1024 * 1024, identity, error) &&
              commands.size() == 1, "Authorization denial prevents firmware write");
        commands.clear(); denied = false; reportError = true;
        check(!selected->flash("/dev/disk/by-id/usb-test-ipod", 1024 * 1024, identity, error) &&
              commands.size() == 1, "Historical zero-exit ERR output is treated as failure");
        commands.clear(); reportError = false;
        int validations = 0;
        check(!selected->flash("/dev/disk/by-id/usb-test-ipod", 1024 * 1024,
              [&](std::string& failure) { if (++validations == 2) { failure = "Changed device"; return false; } return true; },
              error) && commands.size() == 1, "Device change immediately before write prevents flashing");
        commands.clear(); previous = image("mini", 9, 2048);
        check(!selected->flash("/dev/disk/by-id/usb-test-ipod", 1024 * 1024, identity, error) &&
              commands.size() == 1, "Replacement larger than existing image refused");
        commands.clear(); tools = false;
        check(!iPodFirmware::toolsAvailable(error), "Missing system tool reported explicitly");
        check(commands.empty(), "All tests used mocks; no real commands or drives touched");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
