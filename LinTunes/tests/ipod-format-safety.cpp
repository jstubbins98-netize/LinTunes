#include "iPodVolume.h"
#include <gio/gio.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

// Linker wrappers replace *all* system-bus requests. This test cannot contact
// UDisks or format a disk. Its only writes are temporary SysInfo fixture files.
namespace {
std::string serial = "MOCK-IPOD-SERIAL", layout = "dos", firmwareType = "0x00";
bool mounted = true;
guint64 deviceNumber = 0;
int formats = 0, unmounts = 0;
const char* dataPath = "/mock/data";
const char* tablePath = "/mock/disk";
const char* drivePath = "/mock/drive";

GVariant* props(const std::string& path, const std::string& iface) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    auto s = [&](const char* key, const std::string& value) {
        g_variant_builder_add(&b, "{sv}", key, g_variant_new_string(value.c_str()));
    };
    auto o = [&](const char* key, const char* value) {
        g_variant_builder_add(&b, "{sv}", key, g_variant_new_object_path(value));
    };
    auto t = [&](const char* key, guint64 value) {
        g_variant_builder_add(&b, "{sv}", key, g_variant_new_uint64(value));
    };
    if (iface == "org.freedesktop.UDisks2.Block") {
        o("Drive", drivePath);
        s("IdUUID", "MOCK-FILESYSTEM-UUID");
        s("IdType", "hfsplus");
        t("DeviceNumber", deviceNumber);
        t("Size", 1024ULL * 1024 * 1024);
    } else if (iface == "org.freedesktop.UDisks2.Drive") {
        s("Serial", serial);
        s("Vendor", "Apple");
        s("Model", "iPod");
        s("ConnectionBus", "usb");
    } else if (iface == "org.freedesktop.UDisks2.Partition") {
        o("Table", tablePath);
        const bool data = path == dataPath;
        g_variant_builder_add(&b, "{sv}", "Number", g_variant_new_uint32(data ? 2 : 1));
        s("Type", data ? "0xaf" : firmwareType);
        t("Offset", data ? 64ULL * 1024 * 1024 : 512);
        t("Size", data ? 1024ULL * 1024 * 1024 : 32ULL * 1024 * 1024);
    } else if (iface == "org.freedesktop.UDisks2.PartitionTable") {
        s("Type", layout);
    } else if (iface == "org.freedesktop.UDisks2.Filesystem") {
        GVariantBuilder points;
        g_variant_builder_init(&points, G_VARIANT_TYPE("aay"));
        if (mounted) g_variant_builder_add_value(&points, g_variant_new_bytestring("/mock/mount"));
        g_variant_builder_add(&b, "{sv}", "MountPoints", g_variant_builder_end(&points));
    }
    return g_variant_builder_end(&b);
}
GVariant* objects() {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    for (const char* path : {dataPath, "/mock/firmware"}) {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        for (const char* iface : {"org.freedesktop.UDisks2.Block",
                                 "org.freedesktop.UDisks2.Partition"})
            g_variant_builder_add(&ifaces, "{s@a{sv}}", iface, props(path, iface));
        g_variant_builder_add(&b, "{o@a{sa{sv}}}", path, g_variant_builder_end(&ifaces));
    }
    return g_variant_new("(@a{oa{sa{sv}}})", g_variant_builder_end(&b));
}
void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    std::cout << "PASS: " << name << '\n';
}
}

extern "C" GDBusConnection* __wrap_g_bus_get_sync(GBusType, GCancellable*, GError**) {
    return reinterpret_cast<GDBusConnection*>(g_object_new(G_TYPE_OBJECT, nullptr));
}
extern "C" GVariant* __wrap_g_dbus_connection_call_sync(
    GDBusConnection*, const gchar*, const gchar* path, const gchar*,
    const gchar* method, GVariant* args, const GVariantType*,
    GDBusCallFlags, gint, GCancellable*, GError** error) {
    const std::string name(method);
    if (name == "CanFormat") return g_variant_ref_sink(g_variant_new("(bs)", TRUE, ""));
    if (name == "GetManagedObjects") return g_variant_ref_sink(objects());
    if (name == "GetAll") {
        const gchar* iface;
        g_variant_get(args, "(&s)", &iface);
        return g_variant_ref_sink(g_variant_new("(@a{sv})", props(path, iface)));
    }
    if (name == "Unmount") {
        ++unmounts;
        mounted = false;
        return g_variant_ref_sink(g_variant_new("()"));
    }
    if (name == "Format") {
        ++formats;
        const gchar* type;
        GVariant* options;
        g_variant_get(args, "(&s@a{sv})", &type, &options);
        check(std::string(path) == dataPath && std::string(type) == "vfat",
              "Only the selected data partition receives the FAT request");
        GVariant* mkfs = g_variant_lookup_value(options, "mkfs-args", G_VARIANT_TYPE("as"));
        check(mkfs && g_variant_n_children(mkfs) == 2,
              "Explicit FAT32 mkfs arguments supplied");
        gchar** values = g_variant_dup_strv(mkfs, nullptr);
        check(std::string(values[0]) == "-F" && std::string(values[1]) == "32",
              "FAT32, not an unspecified FAT variant");
        g_strfreev(values);
        g_variant_unref(mkfs);
        g_variant_unref(options);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Mock authorization refusal (no formatting performed)");
        return nullptr;
    }
    throw std::runtime_error("Unexpected mock system-bus method: " + name);
}

int main() {
    gchar* temp = g_dir_make_tmp("lintunes-format-test-XXXXXX", nullptr);
    check(temp != nullptr, "Temporary test directory created");
    const std::filesystem::path mount(temp);
    g_free(temp);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::filesystem::remove_all(p); }
    } cleanup{mount};
    std::filesystem::create_directories(mount / "iPod_Control" / "Device");
    std::ofstream(mount / "iPod_Control" / "Device" / "SysInfo") << "ModelNumStr: M8513\n";
    struct stat st {};
    stat(mount.c_str(), &st);
    deviceNumber = st.st_dev;
    iPodVolume target;
    std::string error, newMount;

    layout = "apm";
    check(!target.inspect(mount.string(), error) &&
          error.find("partition map") != std::string::npos && formats == 0,
          "Apple partition map refused without erasing");
    layout = "dos";
    firmwareType = "0x83";
    check(!target.inspect(mount.string(), error) &&
          error.find("firmware partition") != std::string::npos && formats == 0,
          "Unverified firmware partition refused");
    firmwareType = "0x00";
    check(target.inspect(mount.string(), error), "Known USB iPod MBR layout accepted");
    serial = "DIFFERENT-DEVICE";
    check(!target.format(newMount, error) && formats == 0 && unmounts == 0,
          "Changed hardware serial refused before unmount or format");
    serial = "MOCK-IPOD-SERIAL";
    check(target.inspect(mount.string(), error), "Fresh identity snapshot accepted");
    std::ofstream(mount / "iPod_Control" / "Device" / "SysInfo") << "changed model";
    check(!target.format(newMount, error) && formats == 0,
          "Changed model metadata refused before format");
    std::ofstream(mount / "iPod_Control" / "Device" / "SysInfo") << "ModelNumStr: M8513\n";
    check(target.inspect(mount.string(), error), "Restored fixture accepted");
    check(!target.format(newMount, error) && formats == 1 && unmounts == 1 &&
          error.find("Mock authorization refusal") != std::string::npos,
          "Format authorization failure reported without initializing anything");
}
