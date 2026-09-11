#include "MainWindow.h"
#include "Library.h"
#include "Database.h"
#include "AudioPlayer.h"
#include "iPodSync.h"
#include "CDManager.h"
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

#ifdef __APPLE__
static void prependEnvironmentPath(const char* name, const std::string& path) {
    if (path.empty() || !fs::exists(path)) return;
    const char* current = std::getenv(name);
    std::string value = path;
    if (current && *current) value += ":" + std::string(current);
    setenv(name, value.c_str(), 1);
}

static void configureHomebrewRuntime() {
#ifdef MACTUNES_HOMEBREW_PREFIX
    const fs::path prefix(MACTUNES_HOMEBREW_PREFIX);
    prependEnvironmentPath("PATH", (prefix / "bin").string());
    prependEnvironmentPath("DYLD_FALLBACK_LIBRARY_PATH", (prefix / "lib").string());
    prependEnvironmentPath("GI_TYPELIB_PATH",
                           (prefix / "lib" / "girepository-1.0").string());
    prependEnvironmentPath("GST_PLUGIN_SYSTEM_PATH_1_0",
                           (prefix / "lib" / "gstreamer-1.0").string());
    const fs::path scanner = prefix / "libexec" / "gstreamer-1.0" /
                             "gst-plugin-scanner";
    if (fs::exists(scanner)) {
        setenv("GST_PLUGIN_SCANNER", scanner.string().c_str(), 1);
    }
#endif
}
#endif

static std::string getDefaultDbPath() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
#ifdef __APPLE__
    fs::path dir = fs::path(home) / "Library" / "Application Support" / "MacTunes";
#else
    fs::path dir = fs::path(home) / ".local" / "share" / "mactunes";
#endif
    fs::create_directories(dir);
    return (dir / "library.db").string();
}

static std::string getDefaultLibraryDir() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return (fs::path(home) / "Music").string();
}

int main(int argc, char* argv[]) {
#ifdef __APPLE__
    // Homebrew's GStreamer scanner is a child process and does not reliably
    // inherit enough search information when MacTunes is launched by path.
    // Configure it before either GStreamer or GTK loads shared libraries.
    configureHomebrewRuntime();
#endif

    // Initialise GStreamer
    gst_init(&argc, &argv);

    // Initialise GTK
    gtk_init(&argc, &argv);

    // Set application theme preferences
    GtkSettings* settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, nullptr);

    // Database and library
    std::string db_path = getDefaultDbPath();
    Database db(db_path);
    if (!db.open()) {
        std::cerr << "Failed to open database at " << db_path
                  << ": " << db.lastError() << "\n";
        return 1;
    }

    Library library(db);

    // Audio player
    AudioPlayer player;
    if (!player.init()) {
        std::cerr << "Warning: GStreamer playbin failed — audio playback unavailable\n";
    }

    // iPod sync
    iPodSync ipod;

    // CD manager — auto-detect first drive
    auto drives = CDManager::detectDrives();
    std::string cd_device = drives.empty() ?
#ifdef __APPLE__
        "/dev/disk2" :
#else
        "/dev/cdrom" :
#endif
        drives.front();
    CDManager cd(cd_device);

    // Main window
    MainWindow window(library, player, ipod, cd);
    window.show();

    // Run GTK main loop
    gtk_main();

    // Clean up
    player.shutdown();
    if (ipod.isConnected()) {
        ipod.writeDatabase();
        ipod.disconnect();
    }
    db.close();

    return 0;
}
