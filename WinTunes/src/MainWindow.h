#pragma once
#include "Library.h"
#include "AudioPlayer.h"
#include "iPodSync.h"
#include "CDManager.h"
#include "RadioPlayer.h"
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <memory>

class MainWindow {
public:
    MainWindow(Library& library, AudioPlayer& player,
               iPodSync& ipod, CDManager& cd);
    ~MainWindow();

    GtkWidget* window() const { return window_; }
    void show();

private:
    // Core objects
    Library&     library_;
    AudioPlayer& player_;
    iPodSync&    ipod_;
    CDManager&   cd_;
    RadioPlayer  radio_;

    // --- Top-level window ---
    GtkWidget* window_         = nullptr;
    GtkWidget* main_vbox_      = nullptr;

    // --- Menu bar ---
    GtkWidget* menubar_        = nullptr;

    // --- Toolbar ---
    GtkWidget* toolbar_        = nullptr;
    GtkWidget* btn_prev_       = nullptr;
    GtkWidget* btn_play_pause_ = nullptr;
    GtkWidget* btn_next_       = nullptr;
    GtkWidget* btn_shuffle_    = nullptr;
    GtkWidget* btn_repeat_     = nullptr;
    GtkWidget* vol_scale_      = nullptr;
    GtkWidget* seek_scale_     = nullptr;
    GtkWidget* lbl_time_       = nullptr;
    GtkWidget* lbl_track_      = nullptr;

    // --- Source sidebar ---
    GtkWidget* source_tree_    = nullptr;
    GtkWidget* source_store_   = nullptr;

    // --- Track list ---
    GtkWidget* track_list_     = nullptr;
    GtkListStore* track_store_ = nullptr;

    // --- Status bar ---
    GtkWidget* statusbar_      = nullptr;
    guint      status_ctx_     = 0;

    // --- Search ---
    GtkWidget* search_entry_   = nullptr;

    // --- Add Music button ---
    GtkWidget* btn_add_music_  = nullptr;
    GtkWidget* btn_radio_      = nullptr;

    // --- iPod panel ---
    GtkWidget* ipod_panel_     = nullptr;
    GtkWidget* ipod_info_lbl_  = nullptr;

    // --- CD panel ---
    GtkWidget* cd_panel_       = nullptr;

    // --- Empty-state overlay (shown when library is empty) ---
    GtkWidget* content_stack_  = nullptr;   // GtkStack: "list" | "empty"
    GtkWidget* empty_box_      = nullptr;

    // Seeking flag to prevent feedback loop
    bool       seeking_        = false;
    bool       seek_dragging_  = false;

    // Build helpers
    void buildMenuBar();
    void buildToolbar();
    void buildSourceSidebar();
    void buildTrackList();
    void buildIPodPanel();
    void buildCDPanel();
    void buildStatusBar();
    void buildEmptyState();

    // Populate
    void refreshTrackList(const std::vector<Track>& tracks);
    void refreshSourceSidebar();
    void updateNowPlaying(const Track& t);
    void updateSeekBar(int64_t pos_ms, int64_t dur_ms);
    void setStatus(const std::string& msg);
    void updateEmptyState();

    // Unified import helper (handles both files and directories)
    int  importPaths(const std::vector<std::string>& paths);

    // Actions
    void importFiles();
    void importFolder();
    void showAbout();
    void showPreferences();
    void connectIPod();
    void syncToIPod();
    void restoreIPod();
    void resetIPodDatabase();
    void extractFromIPod();
    void ripCD();
    void burnCD();
    void ejectCD();
    void createPlaylist();
    void deletePlaylist();
    void removeSelectedTracks(bool delete_files);
    void editTrackInfo();
    void exportSelectedTracks();
    void searchChanged(const std::string& query);
    void playSelected();
    void showInternetRadio();
    void sourceSelectionChanged();

    // GTK signal stubs (static, dispatch to instance)
    static void onDestroy         (GtkWidget*, gpointer);
    static void onPlayPause       (GtkWidget*, gpointer);
    static void onPrev             (GtkWidget*, gpointer);
    static void onNext             (GtkWidget*, gpointer);
    static void onShuffle         (GtkWidget*, gpointer);
    static void onRepeat          (GtkWidget*, gpointer);
    static void onInternetRadio   (GtkWidget*, gpointer);
    static void onVolumeChanged   (GtkRange*, gpointer);
    static void onSeekPressed     (GtkWidget*, GdkEvent*, gpointer);
    static void onSeekReleased    (GtkWidget*, GdkEvent*, gpointer);
    static void onSeekMoved       (GtkRange*, gpointer);
    static void onTrackRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer);
    static void onSearchChanged   (GtkSearchEntry*, gpointer);
    static void onSourceSelChanged(GtkTreeSelection*, gpointer);
    static gboolean onTrackPopup  (GtkWidget*, GdkEvent*, gpointer);

    // Add Music button / drag-and-drop
    static void     onAddMusicClicked   (GtkWidget*, gpointer);
    static void     onDragDataReceived  (GtkWidget*, GdkDragContext*,
                                         gint x, gint y,
                                         GtkSelectionData*, guint info,
                                         guint time, gpointer);
    static void     onAddFilesActivate  (GtkMenuItem*, gpointer);
    static void     onAddFolderActivate (GtkMenuItem*, gpointer);

    // Menu signal stubs
    static void onMenuImportFiles (GtkMenuItem*, gpointer);
    static void onMenuImportFolder(GtkMenuItem*, gpointer);
    static void onMenuQuit        (GtkMenuItem*, gpointer);
    static void onMenuAbout       (GtkMenuItem*, gpointer);
    static void onMenuNewPlaylist (GtkMenuItem*, gpointer);
    static void onMenuConnectIPod (GtkMenuItem*, gpointer);
    static void onMenuSyncIPod    (GtkMenuItem*, gpointer);
    static void onMenuRestoreIPod (GtkMenuItem*, gpointer);
    static void onMenuResetIPod   (GtkMenuItem*, gpointer);
    static void onMenuRipCD       (GtkMenuItem*, gpointer);
    static void onMenuBurnCD      (GtkMenuItem*, gpointer);
    static void onMenuEjectCD     (GtkMenuItem*, gpointer);

    // Column indices for track_store_
    enum TrackCol {
        COL_ID = 0, COL_TRACK_NUM, COL_TITLE, COL_ARTIST, COL_ALBUM,
        COL_GENRE, COL_YEAR, COL_DURATION, COL_BITRATE, COL_ON_IPOD,
        COL_FILE_PATH, N_TRACK_COLS
    };

    // Source sidebar node types
    enum SourceType { SRC_LIBRARY, SRC_PLAYLIST, SRC_IPOD, SRC_CD };
};
