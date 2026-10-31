#include "iPodVolume.h"
#include <gio/gio.h>
#include <gpod/itdb.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <linux/magic.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <unistd.h>
#include <fcntl.h>

namespace {
namespace fs = std::filesystem;
const char* service = "org.freedesktop.UDisks2";
const char* root = "/org/freedesktop/UDisks2";
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
Variant call(const std::string& path, const char* iface, const char* method,
             GVariant* args, std::string& error, int timeout = 30000) {
    GError* e = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &e);
    if (!bus) {
        error = e ? e->message : "System D-Bus is unavailable.";
        g_clear_error(&e);
        return Variant(nullptr, g_variant_unref);
    }
    GVariant* result = g_dbus_connection_call_sync(bus, service, path.c_str(),
        iface, method, args, nullptr, G_DBUS_CALL_FLAGS_NONE, timeout, nullptr, &e);
    g_object_unref(bus);
    if (!result) {
        error = e ? e->message : "UDisks2 request failed.";
        g_clear_error(&e);
    }
    return Variant(result, g_variant_unref);
}
Variant properties(const std::string& path, const char* iface, std::string& error) {
    auto reply = call(path, "org.freedesktop.DBus.Properties", "GetAll",
                     g_variant_new("(s)", iface), error);
    return Variant(reply ? g_variant_get_child_value(reply.get(), 0) : nullptr,
                   g_variant_unref);
}
std::string text(GVariant* props, const char* key) {
    const gchar* value = nullptr;
    if (props) g_variant_lookup(props, key, "&s", &value);
    if (!value && props) g_variant_lookup(props, key, "&o", &value);
    return value ? value : "";
}
guint64 number(GVariant* props, const char* key) {
    guint64 value = 0;
    if (props) g_variant_lookup(props, key, "t", &value);
    return value;
}
GVariant* options() {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    return g_variant_builder_end(&b);
}
std::string readSmall(const fs::path& path) {
    std::error_code ec;
    auto n = fs::file_size(path, ec);
    if (ec || n > 1024 * 1024) return {};
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}

std::string iPodVolume::filesystemAt(const std::string& mount) {
    struct statfs st {};
    if (statfs(mount.c_str(), &st) != 0) return "unknown";
    if (st.f_type == MSDOS_SUPER_MAGIC) return "vfat";
    // HFS filesystem tags are not exported by all linux/magic.h versions.
    if (st.f_type == 0x4244) return "hfs";
    if (st.f_type == 0x482b) return "hfsplus";
    return "unknown";
}

bool iPodVolume::windowsWritable(const std::string& mount, std::string& error) {
    const auto type = filesystemAt(mount);
    if (type != "vfat") {
        error = (type == "hfs" || type == "hfsplus")
            ? "This iPod is Mac-formatted (HFS/HFS+). Reformat it for Windows before writing."
            : "LinTunes only writes to Windows-formatted FAT iPods. The filesystem is not FAT.";
        return false;
    }
    if (access(mount.c_str(), W_OK) != 0) {
        error = "The Windows-formatted iPod is read-only or not writable.";
        return false;
    }
    return true;
}

bool iPodVolume::inspect(const std::string& mount, std::string& error) {
    *this = iPodVolume();
    struct stat st {};
    if (stat(mount.c_str(), &st) != 0) {
        error = "The selected iPod mount disappeared."; return false;
    }
    mount_ = mount;
    device_ = st.st_dev;
    auto capability = call(std::string(root) + "/Manager",
        "org.freedesktop.UDisks2.Manager", "CanFormat",
        g_variant_new("(s)", "vfat"), error);
    if (!capability) return false;
    gboolean available = FALSE;
    const gchar* missing = nullptr;
    g_variant_get(capability.get(), "(b&s)", &available, &missing);
    if (!available) {
        error = "UDisks2 cannot format FAT. Install dosfstools (mkfs.fat) and try again.";
        return false;
    }
    auto reply = call(root, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                      nullptr, error);
    if (!reply) return false;
    Variant objects(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    GVariantIter iter;
    g_variant_iter_init(&iter, objects.get());
    const gchar* path;
    GVariant* interfaces;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        Variant owner(interfaces, g_variant_unref);
        Variant block(g_variant_lookup_value(interfaces,
            "org.freedesktop.UDisks2.Block", G_VARIANT_TYPE_VARDICT), g_variant_unref);
        if (block && number(block.get(), "DeviceNumber") == device_) {
            block_ = path;
            drive_ = text(block.get(), "Drive");
            uuid_ = text(block.get(), "IdUUID");
            fsType_ = text(block.get(), "IdType");
            size_ = number(block.get(), "Size");
            break;
        }
    }
    if (block_.empty() || uuid_.empty()) {
        error = "Formatting requires a mounted UDisks2 block partition with a filesystem UUID.";
        return false;
    }
    auto partition = properties(block_, "org.freedesktop.UDisks2.Partition", error);
    if (!partition) return false;
    table_ = text(partition.get(), "Table");
    offset_ = number(partition.get(), "Offset");
    auto drive = properties(drive_, "org.freedesktop.UDisks2.Drive", error);
    if (!drive) return false;
    serial_ = text(drive.get(), "Serial");
    std::string hardware = text(drive.get(), "Vendor") + " " + text(drive.get(), "Model");
    std::transform(hardware.begin(), hardware.end(), hardware.begin(), ::tolower);
    if (serial_.empty() || hardware.find("ipod") == std::string::npos ||
        text(drive.get(), "ConnectionBus") != "usb") {
        error = "Formatting requires an identified USB iPod with a stable hardware serial.";
        return false;
    }
    Itdb_Device* dev = itdb_device_new();
    itdb_device_set_mountpoint(dev, mount.c_str());
    const Itdb_IpodInfo* info = itdb_device_get_ipod_info(dev);
    if (info && info->model_number &&
        info->ipod_generation != ITDB_IPOD_GENERATION_UNKNOWN &&
        info->ipod_model != ITDB_IPOD_MODEL_INVALID &&
        info->ipod_generation != ITDB_IPOD_GENERATION_TOUCH_1 &&
        info->ipod_generation != ITDB_IPOD_GENERATION_TOUCH_2 &&
        info->ipod_generation != ITDB_IPOD_GENERATION_TOUCH_3 &&
        info->ipod_generation != ITDB_IPOD_GENERATION_TOUCH_4)
        model_ = info->model_number;
    gchar* control = itdb_get_control_dir(mount.c_str());
    if (control) {
        sysInfo_ = readSmall(fs::path(control) / "Device" / "SysInfo");
        extended_ = readSmall(fs::path(control) / "Device" / "SysInfoExtended");
        g_free(control);
    }
    itdb_device_free(dev);
    if (model_.empty() || sysInfo_.empty()) {
        error = "Cannot verify the iPod model or preserve SysInfo; formatting cancelled.";
        return false;
    }
    return validate(true, error);
}

bool iPodVolume::validate(bool mounted, std::string& error) const {
    if (block_.empty() || drive_.empty() || table_.empty() ||
        serial_.empty() || model_.empty() || sysInfo_.empty()) {
        error = "No verified iPod formatting target has been selected.";
        return false;
    }
    auto block = properties(block_, "org.freedesktop.UDisks2.Block", error);
    auto drive = properties(drive_, "org.freedesktop.UDisks2.Drive", error);
    auto partition = properties(block_, "org.freedesktop.UDisks2.Partition", error);
    auto table = properties(table_, "org.freedesktop.UDisks2.PartitionTable", error);
    if (!block || !drive || !partition || !table) return false;
    guint32 partNumber = 0;
    g_variant_lookup(partition.get(), "Number", "u", &partNumber);
    if (text(table.get(), "Type") != "dos") {
        error = "This iPod uses an Apple/GPT partition map. Safe Windows conversion requires "
                "a model-aware Windows firmware restore. LinTunes will not rewrite its "
                "partition map or risk its firmware.";
        return false;
    }
    if (text(block.get(), "Drive") != drive_ ||
        number(block.get(), "DeviceNumber") != device_ ||
        number(block.get(), "Size") != size_ ||
        text(block.get(), "IdUUID") != uuid_ ||
        text(block.get(), "IdType") != fsType_ ||
        text(drive.get(), "Serial") != serial_ ||
        text(drive.get(), "ConnectionBus") != "usb" ||
        text(partition.get(), "Table") != table_ || partNumber != 2 ||
        number(partition.get(), "Offset") != offset_ ||
        size_ < 64ULL * 1024 * 1024) {
        error = "The device changed or is not a supported iPod data partition (MBR partition 2).";
        return false;
    }
    // Verify a separate firmware partition precedes the music partition.
    // A generic removable disk with an iPod-looking directory is not enough.
    auto reply = call(root, "org.freedesktop.DBus.ObjectManager",
                      "GetManagedObjects", nullptr, error);
    if (!reply) return false;
    Variant objects(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    GVariantIter iter;
    g_variant_iter_init(&iter, objects.get());
    const gchar* path;
    GVariant* interfaces;
    bool firmwareFound = false;
    const guint64 dataStart = number(partition.get(), "Offset");
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        Variant owner(interfaces, g_variant_unref);
        Variant part(g_variant_lookup_value(interfaces,
            "org.freedesktop.UDisks2.Partition", G_VARIANT_TYPE_VARDICT), g_variant_unref);
        if (!part || text(part.get(), "Table") != table_) continue;
        guint32 index = 0;
        g_variant_lookup(part.get(), "Number", "u", &index);
        if (index != 1) continue;
        const auto offset = number(part.get(), "Offset");
        const auto size = number(part.get(), "Size");
        const auto type = text(part.get(), "Type");
        firmwareFound = (type == "0x00" || type == "0") &&
            size >= 1024 * 1024 && offset < dataStart && size <= dataStart - offset;
    }
    if (!firmwareFound) {
        error = "Cannot verify a separate iPod firmware partition. No formatting was performed.";
        return false;
    }
    if (mounted) {
        struct stat st {};
        if (stat(mount_.c_str(), &st) != 0 || static_cast<guint64>(st.st_dev) != device_) {
            error = "The confirmed iPod mount changed. Formatting cancelled."; return false;
        }
        gchar* control = itdb_get_control_dir(mount_.c_str());
        const bool sameMetadata = control &&
            readSmall(fs::path(control) / "Device" / "SysInfo") == sysInfo_ &&
            readSmall(fs::path(control) / "Device" / "SysInfoExtended") == extended_;
        g_free(control);
        if (!sameMetadata) {
            error = "The selected iPod model/device information changed. Formatting cancelled.";
            return false;
        }
    }
    return true;
}

std::string iPodVolume::description() const {
    return "iPod model: " + model_ + "\nMount: " + mount_ +
        "\nSerial: " + serial_ + "\nData partition: " + block_ +
        "\nSize: " + std::to_string(size_ / (1024 * 1024)) + " MiB";
}

bool iPodVolume::format(std::string& newMount, std::string& error) const {
    if (!validate(true, error)) return false;
    if (!call(block_, "org.freedesktop.UDisks2.Filesystem", "Unmount",
              g_variant_new("(@a{sv})", options()), error)) return false;
    if (!validate(false, error)) return false;
    // Refuse if an automounter remounted the partition between authorization
    // and the destructive request; UDisks also rejects formatting busy media.
    auto filesystem = properties(block_, "org.freedesktop.UDisks2.Filesystem", error);
    Variant mounts(filesystem ? g_variant_lookup_value(filesystem.get(),
        "MountPoints", G_VARIANT_TYPE("aay")) : nullptr, g_variant_unref);
    if (!mounts || g_variant_n_children(mounts.get()) != 0) {
        error = "The iPod was remounted or its mount state is unknown. Formatting cancelled.";
        return false;
    }
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "label", g_variant_new_string("IPOD"));
    g_variant_builder_add(&b, "{sv}", "update-partition-type", g_variant_new_boolean(TRUE));
    const char* mkfs[] = {"-F", "32", nullptr};
    g_variant_builder_add(&b, "{sv}", "mkfs-args", g_variant_new_strv(mkfs, -1));
    if (!call(block_, "org.freedesktop.UDisks2.Block", "Format",
              g_variant_new("(s@a{sv})", "vfat", g_variant_builder_end(&b)),
              error, 300000)) return false;
    auto mounted = call(block_, "org.freedesktop.UDisks2.Filesystem", "Mount",
                        g_variant_new("(@a{sv})", options()), error);
    if (!mounted) {
        error = "FAT32 formatting completed, but remount failed: " + error;
        return false;
    }
    const gchar* point = nullptr;
    g_variant_get(mounted.get(), "(&s)", &point);
    newMount = point;
    // Verify the new volume belongs to the same hardware before initializing it.
    auto block = properties(block_, "org.freedesktop.UDisks2.Block", error);
    auto drive = properties(drive_, "org.freedesktop.UDisks2.Drive", error);
    struct stat st {};
    if (!block || !drive || text(drive.get(), "Serial") != serial_ ||
        text(block.get(), "Drive") != drive_ ||
        number(block.get(), "DeviceNumber") != device_ ||
        text(block.get(), "IdType") != "vfat" ||
        text(block.get(), "IdVersion") != "FAT32" ||
        stat(newMount.c_str(), &st) != 0 ||
        static_cast<guint64>(st.st_dev) != device_ ||
        !windowsWritable(newMount, error)) {
        error = "Post-format device/FAT32 verification failed; initialization cancelled.";
        return false;
    }
    const fs::path dir = fs::path(newMount) / "iPod_Control" / "Device";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { error = "Cannot create iPod control directory: " + ec.message(); return false; }
    auto write = [&](const char* name, const std::string& data) {
        if (data.empty()) return true;
        std::ofstream out(dir / name, std::ios::binary);
        out.write(data.data(), data.size());
        out.close();
        return !out.fail();
    };
    if (!write("SysInfo", sysInfo_) || !write("SysInfoExtended", extended_)) {
        error = "FAT32 created, but device information could not be restored."; return false;
    }
    GError* e = nullptr;
    if (!itdb_init_ipod(newMount.c_str(), model_.c_str(), "iPod", &e)) {
        error = e ? e->message : "FAT32 created, but libgpod initialization failed.";
        g_clear_error(&e);
        return false;
    }
    Itdb_iTunesDB* verified = itdb_parse(newMount.c_str(), &e);
    if (!verified) {
        error = e ? e->message : "The formatted iPod database could not be verified.";
        g_clear_error(&e);
        return false;
    }
    itdb_free(verified);
    int fd = open(newMount.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) { error = "Cannot open the formatted iPod for flushing."; return false; }
    const int result = syncfs(fd);
    close(fd);
    if (result != 0) { error = "Could not flush the formatted iPod."; return false; }
    return true;
}
