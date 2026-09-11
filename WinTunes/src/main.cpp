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
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

static std::string getDefaultDbPath() {
#ifdef _WIN32
    PWSTR wide = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                       nullptr, &wide))) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            result.resize(static_cast<size_t>(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), n, nullptr, nullptr);
            result.pop_back();
        }
        CoTaskMemFree(wide);
    }
    if (result.empty()) {
        const char* appdata = std::getenv("LOCALAPPDATA");
        result = appdata ? appdata : ".";
    }
    fs::path dir = fs::u8path(result) / "WinTunes";
#else
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    fs::path dir = fs::path(home) / ".local" / "share" / "wintunes";
#endif
    fs::create_directories(dir);
    return (dir / "library.db").u8string();
}

static std::string getDefaultLibraryDir() {
#ifdef _WIN32
    PWSTR wide = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, KF_FLAG_DEFAULT,
                                       nullptr, &wide))) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(n > 1 ? n : 0), '\0');
        if (n > 1)
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), n, nullptr, nullptr);
        if (!result.empty()) result.pop_back();
        CoTaskMemFree(wide);
        if (!result.empty()) return result;
    }
    const char* music = std::getenv("MUSIC");
    return music ? music : ".";
#else
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return (fs::path(home) / "Music").string();
#endif
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
    std::string cd_device = drives.empty()
#ifdef _WIN32
        ? "D:"
#else
        ? "/dev/cdrom"
#endif
        : drives.front();
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
