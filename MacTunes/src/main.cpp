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
