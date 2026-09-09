#include "MainWindow.h"
#include "iPodRestore.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;

// ── Helper: ms → "mm:ss" ─────────────────────────────────────────────────────
static std::string formatDuration(int64_t ms) {
    int secs = static_cast<int>(ms / 1000);
    int m    = secs / 60;
    int s    = secs % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

// ── Construction ──────────────────────────────────────────────────────────────
MainWindow::MainWindow(Library& library, AudioPlayer& player,
                       iPodSync& ipod, CDManager& cd)
    : library_(library), player_(player), ipod_(ipod), cd_(cd) {

    // Top-level window
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "LinTunes");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1100, 700);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);

    g_signal_connect(window_, "destroy", G_CALLBACK(onDestroy), this);

    // Main container
    main_vbox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), main_vbox_);

    buildMenuBar();
    buildToolbar();

    // Horizontal pane: sidebar + track list
    GtkWidget* hpane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(hpane, TRUE);
    gtk_box_pack_start(GTK_BOX(main_vbox_), hpane, TRUE, TRUE, 0);

    buildSourceSidebar();
    gtk_paned_add1(GTK_PANED(hpane), source_tree_);
    gtk_paned_set_position(GTK_PANED(hpane), 200);

    // Right side: search bar + stack (track list | empty state)
    GtkWidget* right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // Search bar
    search_entry_ = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry_), "Search library…");
    gtk_widget_set_margin_start(search_entry_, 6);
    gtk_widget_set_margin_end(search_entry_, 6);
    gtk_widget_set_margin_top(search_entry_, 4);
    gtk_widget_set_margin_bottom(search_entry_, 4);
    gtk_widget_set_hexpand(search_entry_, TRUE);
    g_signal_connect(search_entry_, "search-changed", G_CALLBACK(onSearchChanged), this);
    gtk_box_pack_start(GTK_BOX(right_box), search_entry_, FALSE, FALSE, 0);

    // Stack: switches between track list and empty-state overlay
    content_stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(content_stack_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(content_stack_, TRUE);
    gtk_widget_set_hexpand(content_stack_, TRUE);

    buildTrackList();
    gtk_stack_add_named(GTK_STACK(content_stack_), track_list_, "list");

    buildEmptyState();
    gtk_stack_add_named(GTK_STACK(content_stack_), empty_box_, "empty");

    gtk_box_pack_start(GTK_BOX(right_box), content_stack_, TRUE, TRUE, 0);
    gtk_paned_add2(GTK_PANED(hpane), right_box);

    buildStatusBar();

    // Wire up player callbacks
    player_.onStateChanged([this](PlayerState s) {
        if (!GTK_IS_WIDGET(window_)) return;
        gdk_threads_add_idle([](gpointer d) -> gboolean {
            auto* self = static_cast<MainWindow*>(d);
            bool playing = (self->player_.state() == PlayerState::Playing);
            gtk_button_set_label(GTK_BUTTON(self->btn_play_pause_),
                                 playing ? "⏸" : "▶");
            return FALSE;
        }, this);
    });

    player_.onPositionChanged([this](int64_t pos, int64_t dur) {
        if (!GTK_IS_WIDGET(window_)) return;
        struct PosDur { int64_t pos, dur; MainWindow* self; };
        auto* pd = new PosDur{pos, dur, this};
        gdk_threads_add_idle([](gpointer d) -> gboolean {
            auto* pd = static_cast<PosDur*>(d);
            pd->self->updateSeekBar(pd->pos, pd->dur);
            delete pd;
            return FALSE;
        }, pd);
    });

    player_.onTrackChanged([this](const Track& t) {
        if (!GTK_IS_WIDGET(window_)) return;
        struct TrackPtr { Track t; MainWindow* self; };
        auto* tp = new TrackPtr{t, this};
        gdk_threads_add_idle([](gpointer d) -> gboolean {
            auto* tp = static_cast<TrackPtr*>(d);
            tp->self->updateNowPlaying(tp->t);
            delete tp;
            return FALSE;
        }, tp);
    });

    // Populate initial library view
    refreshTrackList(library_.getAllTracks());
    refreshSourceSidebar();
    updateEmptyState();
    setStatus("Library loaded — " + std::to_string(library_.db().getTotalTrackCount()) + " tracks");
}

MainWindow::~MainWindow() {}

void MainWindow::show() {
    gtk_widget_show_all(window_);
}

void MainWindow::showInternetRadio() {
    struct RadioPreset {
        const char* name;
        const char* url;
    };
    static const RadioPreset presets[] = {
        {"KQED Public Radio", "https://streams.kqed.org/kqedradio"},
        {"KEXP 90.3 FM", "https://kexp.streamguys1.com/kexp160.aac"},
        {"SomaFM Groove Salad", "https://ice1.somafm.com/groovesalad-128-mp3"}
    };

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Internet Radio", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Stop Radio", GTK_RESPONSE_REJECT,
        "_Play", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    GtkWidget* help = gtk_label_new(
        "Choose a saved station or enter another direct stream URL.\n"
        "HTTP, HTTPS, Icecast, Shoutcast, and playlist URLs supported by VLC can be used.");
    gtk_label_set_xalign(GTK_LABEL(help), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
    gtk_box_pack_start(GTK_BOX(box), help, FALSE, FALSE, 0);

    GtkWidget* station_label = gtk_label_new("Saved stations");
    gtk_label_set_xalign(GTK_LABEL(station_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), station_label, FALSE, FALSE, 0);

    GtkWidget* station_combo = gtk_combo_box_text_new();
    for (const auto& preset : presets)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(station_combo), preset.name);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(station_combo), "Custom stream URL");
    gtk_combo_box_set_active(GTK_COMBO_BOX(station_combo), 0);
    gtk_box_pack_start(GTK_BOX(box), station_combo, FALSE, FALSE, 0);

    GtkWidget* url_label = gtk_label_new("Stream URL");
    gtk_label_set_xalign(GTK_LABEL(url_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), url_label, FALSE, FALSE, 0);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry),
        "https://radio.example.org/live.mp3");
    gtk_entry_set_text(GTK_ENTRY(entry), presets[0].url);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

    g_signal_connect(station_combo, "changed",
        G_CALLBACK(+[](GtkComboBox* combo, gpointer data) {
            auto* url_entry = GTK_ENTRY(data);
            const gint selected = gtk_combo_box_get_active(combo);
            if (selected >= 0 &&
                selected < static_cast<gint>(sizeof(presets) / sizeof(presets[0]))) {
                gtk_entry_set_text(url_entry, presets[selected].url);
            } else {
                gtk_entry_set_text(url_entry, "");
                gtk_widget_grab_focus(GTK_WIDGET(url_entry));
            }
        }), entry);

    gtk_widget_show_all(dialog);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    std::string url = gtk_entry_get_text(GTK_ENTRY(entry));
    gchar* selected_station =
        gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(station_combo));
    std::string station_name = selected_station ? selected_station : "Internet Radio";
    g_free(selected_station);
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_REJECT) {
        radio_.stop();
        gtk_label_set_text(GTK_LABEL(lbl_track_), "No track playing");
        setStatus("Internet radio stopped");
        return;
    }
    if (response != GTK_RESPONSE_ACCEPT) return;

    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        GtkWidget* error = gtk_message_dialog_new(
            GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK, "%s", "Enter a valid HTTP or HTTPS radio stream URL.");
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
        return;
    }

    player_.stop();
    if (!radio_.play(url)) {
        GtkWidget* error = gtk_message_dialog_new(
            GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK, "%s", radio_.lastError().c_str());
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
        setStatus("Could not start internet radio");
        return;
    }

    radio_.setVolume(gtk_range_get_value(GTK_RANGE(vol_scale_)));
    if (station_name == "Custom stream URL") station_name = "Internet Radio";
    gtk_label_set_text(GTK_LABEL(lbl_track_), station_name.c_str());
    gtk_label_set_text(GTK_LABEL(lbl_time_), "LIVE");
    gtk_range_set_value(GTK_RANGE(seek_scale_), 0);
    setStatus("Streaming " + station_name + " through libVLC");
}

// ── Menu bar ──────────────────────────────────────────────────────────────────
void MainWindow::buildMenuBar() {
    menubar_ = gtk_menu_bar_new();

    auto make_item = [](const char* label) {
        return gtk_menu_item_new_with_mnemonic(label);
    };
    auto make_menu = []() { return gtk_menu_new(); };

    // File menu
    {
        GtkWidget* menu = make_menu();
        GtkWidget* item = make_item("_File");
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);

        GtkWidget* import_files  = make_item("_Add Files to Library…");
        GtkWidget* import_folder = make_item("Add _Folder to Library…");
        GtkWidget* sep           = gtk_separator_menu_item_new();
        GtkWidget* quit          = make_item("_Quit");

        g_signal_connect(import_files,  "activate", G_CALLBACK(onMenuImportFiles),  this);
        g_signal_connect(import_folder, "activate", G_CALLBACK(onMenuImportFolder), this);
        g_signal_connect(quit,          "activate", G_CALLBACK(onMenuQuit),          this);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), import_files);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), import_folder);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar_), item);
    }

    // Library menu
    {
        GtkWidget* menu = make_menu();
        GtkWidget* item = make_item("_Library");
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);

        GtkWidget* new_pl  = make_item("_New Playlist");
        g_signal_connect(new_pl, "activate", G_CALLBACK(onMenuNewPlaylist), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_pl);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar_), item);
    }

    // Device menu
    {
        GtkWidget* menu = make_menu();
        GtkWidget* item = make_item("_Device");
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);

        GtkWidget* connect_ipod = make_item("_Connect iPod…");
        GtkWidget* sync_ipod    = make_item("_Sync to iPod");
        GtkWidget* restore_ipod = make_item("_Restore iPod from Firmware…");
        GtkWidget* reset_ipod   = make_item("Erase and _Reset Music Database…");
        GtkWidget* sep1         = gtk_separator_menu_item_new();
        GtkWidget* rip_cd       = make_item("_Rip CD…");
        GtkWidget* burn_cd      = make_item("_Burn Disc…");
        GtkWidget* eject_cd     = make_item("_Eject Disc");

        g_signal_connect(connect_ipod, "activate", G_CALLBACK(onMenuConnectIPod), this);
        g_signal_connect(sync_ipod,    "activate", G_CALLBACK(onMenuSyncIPod),    this);
        g_signal_connect(restore_ipod, "activate", G_CALLBACK(onMenuRestoreIPod), this);
        g_signal_connect(reset_ipod,   "activate", G_CALLBACK(onMenuResetIPod),   this);
        g_signal_connect(rip_cd,       "activate", G_CALLBACK(onMenuRipCD),       this);
        g_signal_connect(burn_cd,      "activate", G_CALLBACK(onMenuBurnCD),      this);
        g_signal_connect(eject_cd,     "activate", G_CALLBACK(onMenuEjectCD),     this);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), connect_ipod);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sync_ipod);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), restore_ipod);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), reset_ipod);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep1);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), rip_cd);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), burn_cd);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), eject_cd);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar_), item);
    }

    // Help menu
    {
        GtkWidget* menu = make_menu();
        GtkWidget* item = make_item("_Help");
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
        GtkWidget* about = make_item("_About");
        g_signal_connect(about, "activate", G_CALLBACK(onMenuAbout), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar_), item);
    }

    gtk_box_pack_start(GTK_BOX(main_vbox_), menubar_, FALSE, FALSE, 0);
}

// ── Toolbar ───────────────────────────────────────────────────────────────────
void MainWindow::buildToolbar() {
    GtkWidget* toolbar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar_box, 8);
    gtk_widget_set_margin_end(toolbar_box, 8);
    gtk_widget_set_margin_top(toolbar_box, 4);
    gtk_widget_set_margin_bottom(toolbar_box, 4);

    // Playback buttons
    btn_prev_       = gtk_button_new_with_label("⏮");
    btn_play_pause_ = gtk_button_new_with_label("▶");
    btn_next_       = gtk_button_new_with_label("⏭");
    btn_shuffle_    = gtk_toggle_button_new_with_label("⇄");
    btn_repeat_     = gtk_toggle_button_new_with_label("↺");

    g_signal_connect(btn_prev_,       "clicked", G_CALLBACK(onPrev),      this);
    g_signal_connect(btn_play_pause_, "clicked", G_CALLBACK(onPlayPause), this);
    g_signal_connect(btn_next_,       "clicked", G_CALLBACK(onNext),      this);
    g_signal_connect(btn_shuffle_,    "clicked", G_CALLBACK(onShuffle),   this);
    g_signal_connect(btn_repeat_,     "clicked", G_CALLBACK(onRepeat),    this);

    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_prev_,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_play_pause_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_next_,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_shuffle_,    FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_repeat_,     FALSE, FALSE, 2);

    // ── Add Music button ────────────────────────────────────────────────────
    gtk_box_pack_start(GTK_BOX(toolbar_box),
                       gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 6);

    btn_add_music_ = gtk_button_new_with_label("+ Add Music");
    {
        // Style it with a distinct colour so it's immediately obvious
        GtkStyleContext* ctx = gtk_widget_get_style_context(btn_add_music_);
        gtk_style_context_add_class(ctx, "suggested-action");
    }
    gtk_widget_set_tooltip_text(btn_add_music_,
        "Add songs or a folder to your library  (you can also drag files onto the track list)");
    g_signal_connect(btn_add_music_, "clicked", G_CALLBACK(onAddMusicClicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_add_music_, FALSE, FALSE, 2);

    btn_radio_ = gtk_button_new_with_label("📻 Internet Radio");
    gtk_widget_set_tooltip_text(btn_radio_,
        "Play an internet radio stream using libVLC");
    g_signal_connect(btn_radio_, "clicked", G_CALLBACK(onInternetRadio), this);
    gtk_box_pack_start(GTK_BOX(toolbar_box), btn_radio_, FALSE, FALSE, 2);
    // ───────────────────────────────────────────────────────────────────────

    // Track info label
    lbl_track_ = gtk_label_new("No track playing");
    gtk_label_set_ellipsize(GTK_LABEL(lbl_track_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(lbl_track_, TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl_track_), 0.5f);
    gtk_box_pack_start(GTK_BOX(toolbar_box), lbl_track_, TRUE, TRUE, 8);

    // Seek bar
    seek_scale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(seek_scale_), FALSE);
    gtk_widget_set_size_request(seek_scale_, 200, -1);
    g_signal_connect(seek_scale_, "button-press-event",   G_CALLBACK(onSeekPressed),  this);
    g_signal_connect(seek_scale_, "button-release-event", G_CALLBACK(onSeekReleased), this);
    g_signal_connect(seek_scale_, "value-changed",        G_CALLBACK(onSeekMoved),    this);
    gtk_box_pack_start(GTK_BOX(toolbar_box), seek_scale_, FALSE, FALSE, 4);

    // Time label
    lbl_time_ = gtk_label_new("0:00 / 0:00");
    gtk_widget_set_size_request(lbl_time_, 90, -1);
    gtk_box_pack_start(GTK_BOX(toolbar_box), lbl_time_, FALSE, FALSE, 0);

    // Volume
    GtkWidget* vol_icon = gtk_label_new("🔊");
    gtk_box_pack_start(GTK_BOX(toolbar_box), vol_icon, FALSE, FALSE, 4);

    vol_scale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.01);
    gtk_scale_set_draw_value(GTK_SCALE(vol_scale_), FALSE);
    gtk_range_set_value(GTK_RANGE(vol_scale_), 1.0);
    gtk_widget_set_size_request(vol_scale_, 90, -1);
    g_signal_connect(vol_scale_, "value-changed", G_CALLBACK(onVolumeChanged), this);
    gtk_box_pack_start(GTK_BOX(toolbar_box), vol_scale_, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(main_vbox_), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox_), toolbar_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox_), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
}

// ── Source sidebar ────────────────────────────────────────────────────────────
void MainWindow::buildSourceSidebar() {
    GtkTreeStore* store = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT64);
    source_store_ = GTK_WIDGET(store);

    source_tree_ = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(source_tree_),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(source_tree_, 180, -1);

    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    gtk_container_add(GTK_CONTAINER(source_tree_), view);

    GtkCellRenderer*   renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* col      = gtk_tree_view_column_new_with_attributes(
        "Source", renderer, "text", 0, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    g_signal_connect(sel, "changed", G_CALLBACK(onSourceSelChanged), this);

    // Populate nodes
    GtkTreeIter library_iter, playlists_iter, devices_iter;
    gtk_tree_store_append(store, &library_iter, nullptr);
    gtk_tree_store_set(store, &library_iter, 0, "Music Library", 1, SRC_LIBRARY, 2, (gint64)0, -1);

    gtk_tree_store_append(store, &playlists_iter, nullptr);
    gtk_tree_store_set(store, &playlists_iter, 0, "Playlists", 1, SRC_PLAYLIST, 2, (gint64)-1, -1);

    // Add playlists as children
    for (const auto& [id, name] : library_.getAllPlaylists()) {
        GtkTreeIter child;
        gtk_tree_store_append(store, &child, &playlists_iter);
        gtk_tree_store_set(store, &child, 0, name.c_str(), 1, SRC_PLAYLIST, 2, (gint64)id, -1);
    }

    gtk_tree_store_append(store, &devices_iter, nullptr);
    gtk_tree_store_set(store, &devices_iter, 0, "Devices", 1, SRC_IPOD, 2, (gint64)0, -1);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(view));

    // Default selection = Library
    gtk_tree_selection_select_iter(sel, &library_iter);
}

// ── Track list ────────────────────────────────────────────────────────────────
void MainWindow::buildTrackList() {
    track_store_ = gtk_list_store_new(N_TRACK_COLS,
        G_TYPE_INT64,   // COL_ID
        G_TYPE_INT,     // COL_TRACK_NUM
        G_TYPE_STRING,  // COL_TITLE
        G_TYPE_STRING,  // COL_ARTIST
        G_TYPE_STRING,  // COL_ALBUM
        G_TYPE_STRING,  // COL_GENRE
        G_TYPE_INT,     // COL_YEAR
        G_TYPE_STRING,  // COL_DURATION
        G_TYPE_INT,     // COL_BITRATE
        G_TYPE_BOOLEAN, // COL_ON_IPOD
        G_TYPE_STRING   // COL_FILE_PATH
    );

    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(track_store_));
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
    gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(view), TRUE);

    // Enable multi-select
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);

    auto add_col = [&](const char* title, int col_id, int width = -1) {
        GtkCellRenderer*   r   = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            title, r, "text", col_id, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_sort_column_id(col, col_id);
        if (width > 0) gtk_tree_view_column_set_fixed_width(col, width);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    };

    add_col("#",       COL_TRACK_NUM,  40);
    add_col("Title",   COL_TITLE,     260);
    add_col("Artist",  COL_ARTIST,    180);
    add_col("Album",   COL_ALBUM,     160);
    add_col("Genre",   COL_GENRE,     100);
    add_col("Year",    COL_YEAR,       60);
    add_col("Time",    COL_DURATION,   70);
    add_col("Bitrate", COL_BITRATE,    70);

    // On iPod indicator (boolean → toggle cell)
    {
        GtkCellRenderer*   r   = gtk_cell_renderer_toggle_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            "On iPod", r, "active", COL_ON_IPOD, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_fixed_width(col, 70);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    }

    g_signal_connect(view, "row-activated",    G_CALLBACK(onTrackRowActivated), this);
    g_signal_connect(view, "button-press-event", G_CALLBACK(onTrackPopup),      this);

    // ── Drag-and-drop: accept files and directories dropped onto the list ──
    static const GtkTargetEntry dnd_targets[] = {
        { const_cast<gchar*>("text/uri-list"), 0, 0 }
    };
    gtk_drag_dest_set(view,
                      GTK_DEST_DEFAULT_ALL,
                      dnd_targets, G_N_ELEMENTS(dnd_targets),
                      GDK_ACTION_COPY);
    g_signal_connect(view, "drag-data-received", G_CALLBACK(onDragDataReceived), this);
    // ───────────────────────────────────────────────────────────────────────

    track_list_ = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(track_list_),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(track_list_, TRUE);
    gtk_container_add(GTK_CONTAINER(track_list_), view);
}

// ── Status bar ────────────────────────────────────────────────────────────────
void MainWindow::buildStatusBar() {
    statusbar_ = gtk_statusbar_new();
    status_ctx_ = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar_), "main");
    gtk_box_pack_end(GTK_BOX(main_vbox_), statusbar_, FALSE, FALSE, 0);
}

// ── Populate track list ───────────────────────────────────────────────────────
void MainWindow::refreshTrackList(const std::vector<Track>& tracks) {
    gtk_list_store_clear(track_store_);
    for (const auto& t : tracks) {
        GtkTreeIter iter;
        gtk_list_store_append(track_store_, &iter);
        gtk_list_store_set(track_store_, &iter,
            COL_ID,        (gint64)t.id,
            COL_TRACK_NUM, t.track_number,
            COL_TITLE,     t.title.c_str(),
            COL_ARTIST,    t.artist.c_str(),
            COL_ALBUM,     t.album.c_str(),
            COL_GENRE,     t.genre.c_str(),
            COL_YEAR,      t.year,
            COL_DURATION,  formatDuration(t.duration_ms).c_str(),
            COL_BITRATE,   t.bitrate,
            COL_ON_IPOD,   t.on_ipod,
            COL_FILE_PATH, t.file_path.c_str(),
            -1);
    }
}

void MainWindow::refreshSourceSidebar() {
    // Rebuild to reflect playlists — simplest approach for now
    // In production this would use a smarter incremental update
}

void MainWindow::updateNowPlaying(const Track& t) {
    std::string label = t.title + "  –  " + t.artist;
    if (!t.album.empty()) label += "  [" + t.album + "]";
    gtk_label_set_text(GTK_LABEL(lbl_track_), label.c_str());
    std::string title = "LinTunes – " + t.title;
    gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
}

void MainWindow::updateSeekBar(int64_t pos_ms, int64_t dur_ms) {
    if (seek_dragging_) return;
    seeking_ = true;
    double frac = dur_ms > 0 ? (double)pos_ms / dur_ms * 100.0 : 0;
    gtk_range_set_value(GTK_RANGE(seek_scale_), frac);
    seeking_ = false;

    std::string time_str = formatDuration(pos_ms) + " / " + formatDuration(dur_ms);
    gtk_label_set_text(GTK_LABEL(lbl_time_), time_str.c_str());
}

void MainWindow::setStatus(const std::string& msg) {
    gtk_statusbar_pop(GTK_STATUSBAR(statusbar_), status_ctx_);
    gtk_statusbar_push(GTK_STATUSBAR(statusbar_), status_ctx_, msg.c_str());
}

// ── Empty-state overlay ───────────────────────────────────────────────────────
void MainWindow::buildEmptyState() {
    empty_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_valign(empty_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(empty_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top   (empty_box_, 40);
    gtk_widget_set_margin_bottom(empty_box_, 40);

    // Icon-like big label
    GtkWidget* icon = gtk_label_new("♫");
    {
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(4.0));
        gtk_label_set_attributes(GTK_LABEL(icon), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_box_pack_start(GTK_BOX(empty_box_), icon, FALSE, FALSE, 0);

    GtkWidget* heading = gtk_label_new("Your library is empty");
    {
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(1.5));
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(heading), attrs);
        pango_attr_list_unref(attrs);
    }
    gtk_box_pack_start(GTK_BOX(empty_box_), heading, FALSE, FALSE, 0);

    GtkWidget* hint = gtk_label_new(
        "Click \"+ Add Music\" to add files or a folder,\n"
        "or drag and drop audio files directly onto this window.");
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(empty_box_), hint, FALSE, FALSE, 0);

    // Large "Add Music" button in the empty state too
    GtkWidget* big_btn = gtk_button_new_with_label("+ Add Music");
    gtk_style_context_add_class(gtk_widget_get_style_context(big_btn), "suggested-action");
    gtk_widget_set_halign(big_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(big_btn, 180, 44);
    g_signal_connect(big_btn, "clicked", G_CALLBACK(onAddMusicClicked), this);
    gtk_box_pack_start(GTK_BOX(empty_box_), big_btn, FALSE, FALSE, 8);
}

void MainWindow::updateEmptyState() {
    if (!content_stack_) return;
    bool empty = (library_.db().getTotalTrackCount() == 0);
    gtk_stack_set_visible_child_name(GTK_STACK(content_stack_), empty ? "empty" : "list");
}

// ── Unified import helper ─────────────────────────────────────────────────────
// Accepts a mix of file paths and directory paths; directories are scanned
// recursively. Runs on a background thread so the UI stays responsive.
int MainWindow::importPaths(const std::vector<std::string>& paths) {
    // Separate files from directories up front
    std::vector<std::string> files;
    std::vector<std::string> dirs;
    for (const auto& p : paths) {
        if (fs::is_directory(p))   dirs.push_back(p);
        else if (fs::is_regular_file(p)) files.push_back(p);
    }

    int total_imported = 0;

    // Import individual files immediately (fast, no recursion needed)
    for (const auto& f : files) {
        Track t;
        if (library_.importFile(f, t)) ++total_imported;
    }

    // Import directories
    for (const auto& d : dirs) {
        total_imported += library_.importDirectory(d, /*recursive=*/true,
            [this](int cur, int tot, const std::string&) {
                if (cur % 10 != 0) return;
                std::string msg = "Importing " + std::to_string(cur) +
                                  " / " + std::to_string(tot) + "…";
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* p = static_cast<std::pair<MainWindow*, std::string>*>(data);
                    p->first->setStatus(p->second);
                    delete p;
                    return FALSE;
                }, new std::pair<MainWindow*, std::string>(this, msg));
            });
    }

    return total_imported;
}

// ── Actions ───────────────────────────────────────────────────────────────────
void MainWindow::importFiles() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Add Files to Library", GTK_WINDOW(window_),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    // Audio file filter
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio files");
    const char* patterns[] = {
        "*.mp3","*.m4a","*.aac","*.flac","*.ogg","*.wav",
        "*.aiff","*.aif","*.wma","*.opus","*.ape", nullptr
    };
    for (int i = 0; patterns[i]; ++i) gtk_file_filter_add_pattern(filter, patterns[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    // "All files" filter so users can still override
    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* chosen = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        std::vector<std::string> paths;
        for (GSList* l = chosen; l; l = l->next) {
            paths.emplace_back(static_cast<char*>(l->data));
            g_free(l->data);
        }
        g_slist_free(chosen);
        gtk_widget_destroy(dialog);

        setStatus("Importing files…");
        std::thread([this, paths]() {
            int n = importPaths(paths);
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* p = static_cast<std::pair<MainWindow*, int>*>(data);
                p->first->refreshTrackList(p->first->library_.getAllTracks());
                p->first->updateEmptyState();
                p->first->setStatus("Added " + std::to_string(p->second) + " track(s) to library");
                delete p;
                return FALSE;
            }, new std::pair<MainWindow*, int>(this, n));
        }).detach();
        return;
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::importFolder() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Add Folder to Library", GTK_WINDOW(window_),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add", GTK_RESPONSE_ACCEPT, nullptr);

    // Allow picking multiple folders at once
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* chosen = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        std::vector<std::string> paths;
        for (GSList* l = chosen; l; l = l->next) {
            paths.emplace_back(static_cast<char*>(l->data));
            g_free(l->data);
        }
        g_slist_free(chosen);
        gtk_widget_destroy(dialog);

        setStatus("Scanning folder(s)…");
        std::thread([this, paths]() {
            int n = importPaths(paths);
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* p = static_cast<std::pair<MainWindow*, int>*>(data);
                p->first->refreshTrackList(p->first->library_.getAllTracks());
                p->first->updateEmptyState();
                p->first->setStatus("Added " + std::to_string(p->second) + " track(s) to library");
                delete p;
                return FALSE;
            }, new std::pair<MainWindow*, int>(this, n));
        }).detach();
        return;
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::connectIPod() {
    // Scan for mounted iPods — wrap in try/catch for any filesystem errors
    std::vector<std::string> mounts;
    try {
        mounts = iPodSync::detectMountPoints();
    } catch (const std::exception& e) {
        std::string msg = std::string("Error scanning for iPod: ") + e.what();
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg.c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    if (mounts.empty()) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
            "No iPod detected.\n\nMake sure your iPod is connected and mounted.\n"
            "Check that the iPod shows up under /media or /run/media.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    // If multiple iPods, let user choose
    std::string mount = mounts[0];
    if (mounts.size() > 1) {
        GtkWidget* dlg = gtk_dialog_new_with_buttons("Select iPod", GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
        GtkWidget* combo = gtk_combo_box_text_new();
        for (const auto& m : mounts)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), m.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                           combo, FALSE, FALSE, 8);
        gtk_widget_show_all(dlg);
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
            gchar* sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
            if (sel) { mount = sel; g_free(sel); }
        }
        gtk_widget_destroy(dlg);
    }

    setStatus("Connecting to iPod at " + mount + "…");

    // itdb_parse does filesystem I/O — run it synchronously; it's fast on a
    // mounted iPod (reads a single binary DB file, typically < 1 second).
    // Showing a dialog from inside gtk_main() is safe; no extra thread needed.
    if (!ipod_.connect(mount)) {
        // Build message before passing to GTK — never feed user strings as printf format
        std::string msg = "Failed to connect to iPod at " + mount +
                          ":\n" + ipod_.lastError();
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "%s", msg.c_str());
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        setStatus("iPod connection failed.");
        return;
    }

    iPodInfo info    = ipod_.getInfo();
    double   used_gb = (info.capacity_bytes - info.free_bytes) / 1e9;
    double   cap_gb  =  info.capacity_bytes / 1e9;
    char     buf[256];
    snprintf(buf, sizeof(buf),
             "iPod connected at %s — %d tracks, %.1f / %.1f GB used",
             mount.c_str(), info.track_count, used_gb, cap_gb);
    setStatus(buf);
}

void MainWindow::syncToIPod() {
    // If not connected yet, wait for connection — but since connectIPod is now
    // async we can only proceed if the iPod is already connected.
    if (!ipod_.isConnected()) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
            "No iPod connected.\n\nUse Device → Connect iPod… first,\n"
            "then try syncing again once it shows as connected in the status bar.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    // Collect selected tracks on the main thread (GTK model access must stay here)
    GtkWidget* view = gtk_bin_get_child(GTK_BIN(track_list_));
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    GList* rows = gtk_tree_selection_get_selected_rows(sel, nullptr);

    if (!rows) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
            "Select tracks to sync to the iPod first.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    std::vector<Track> to_sync;
    for (GList* l = rows; l; l = l->next) {
        GtkTreePath* path = static_cast<GtkTreePath*>(l->data);
        GtkTreeIter  iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(track_store_), &iter, path)) {
            gint64 id;
            gtk_tree_model_get(GTK_TREE_MODEL(track_store_), &iter, COL_ID, &id, -1);
            Track t = library_.db().getTrack(id);
            if (!t.file_path.empty())   // skip tracks with no local file
                to_sync.push_back(std::move(t));
        }
        gtk_tree_path_free(path);
    }
    g_list_free(rows);

    if (to_sync.empty()) {
        setStatus("No local tracks selected to sync.");
        return;
    }

    int total = static_cast<int>(to_sync.size());
    setStatus("Syncing " + std::to_string(total) + " track(s) to iPod…");

    // Heavy I/O runs on a background thread — NEVER call gtk_main_iteration()
    // inside a loop; that allows re-entrant GTK calls which crash.
    std::thread([this, to_sync = std::move(to_sync), total]() {
        int synced = 0;
        for (const auto& t : to_sync) {
            // Post status update safely to the GTK main thread
            std::string msg = "Syncing: " + t.title +
                              " (" + std::to_string(synced + 1) +
                              "/" + std::to_string(total) + ")";
            gdk_threads_add_idle([](gpointer d) -> gboolean {
                auto* p = static_cast<std::pair<MainWindow*, std::string>*>(d);
                p->first->setStatus(p->second);
                delete p;
                return FALSE;
            }, new std::pair<MainWindow*, std::string>(this, msg));

            if (ipod_.addTrack(t))
                ++synced;
        }

        bool db_ok = ipod_.writeDatabase();
        std::string final_msg = db_ok
            ? "Synced " + std::to_string(synced) + " / " +
              std::to_string(total) + " tracks to iPod"
            : "Sync error writing iPod database: " + ipod_.lastError();

        gdk_threads_add_idle([](gpointer d) -> gboolean {
            auto* p = static_cast<std::pair<MainWindow*, std::string>*>(d);
            p->first->setStatus(p->second);
            p->first->refreshTrackList(p->first->library_.getAllTracks());
            delete p;
            return FALSE;
        }, new std::pair<MainWindow*, std::string>(this, final_msg));
    }).detach();
}

void MainWindow::restoreIPod() {
    GtkWidget* type_dlg = gtk_dialog_new_with_buttons(
        "Restore iPod", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Continue", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(type_dlg));
    GtkWidget* warning = gtk_label_new(
        "Restoring permanently erases the iPod.\n"
        "Choose the exact device family. Firmware for another model can make the iPod unusable.");
    gtk_label_set_line_wrap(GTK_LABEL(warning), TRUE);
    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "iPod touch (.ipsw, recovery/DFU mode)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Classic / nano / mini / shuffle (disk mode)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(content), warning, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(content), combo, FALSE, FALSE, 10);
    gtk_widget_show_all(type_dlg);
    if (gtk_dialog_run(GTK_DIALOG(type_dlg)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(type_dlg);
        return;
    }
    int family = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    gtk_widget_destroy(type_dlg);
    iPodRestoreKind kind = family == 0
        ? iPodRestoreKind::TouchIPSW : iPodRestoreKind::ClassicFirmware;
    iPodRestoreTarget target;
    if (kind == iPodRestoreKind::TouchIPSW) {
        std::string discoveryError;
        auto ids = iPodRestore::discoverTouchDeviceIds(discoveryError);
        if (ids.size() != 1) {
            std::string message = ids.size() > 1
                ? "More than one iPod touch is connected. Disconnect every device except the one to restore."
                : discoveryError;
            GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message.c_str());
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
            return;
        }
        target.identifier = ids.front();
    } else {
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s",
            "Disk-mode firmware restore is disabled because ipodpatcher cannot "
            "guarantee that it will write to the iPod selected in LinTunes.");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }

    GtkWidget* chooser = gtk_file_chooser_dialog_new(
        "Select iPod Firmware", GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, family == 0 ? "Apple IPSW firmware" : "Disk-mode iPod firmware");
    if (family == 0) gtk_file_filter_add_pattern(filter, "*.ipsw");
    else {
        gtk_file_filter_add_pattern(filter, "*.bin");
        gtk_file_filter_add_pattern(filter, "*.ipod");
        gtk_file_filter_add_pattern(filter, "*.img");
    }
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    if (gtk_dialog_run(GTK_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(chooser);
        return;
    }
    gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    std::string firmware = selected ? selected : "";
    g_free(selected);
    gtk_widget_destroy(chooser);

    std::string validation_error;
    if (!iPodRestore::validFirmwareFile(kind, firmware, validation_error)) {
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", validation_error.c_str());
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }

    GtkWidget* confirm = gtk_dialog_new_with_buttons(
        "Final Restore Confirmation", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Restore and Erase", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(confirm));
    std::string prompt = "This will erase only iPod " + target.identifier +
                          " using:\n" + firmware +
                         "\n\nType ERASE IPOD to continue:";
    GtkWidget* label = gtk_label_new(prompt.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    GtkWidget* entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 10);
    gtk_widget_show_all(confirm);
    bool approved = gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_OK &&
        std::string(gtk_entry_get_text(GTK_ENTRY(entry))) == "ERASE IPOD";
    gtk_widget_destroy(confirm);
    if (!approved) {
        setStatus("iPod restore cancelled.");
        return;
    }

    ipod_.disconnect();
    setStatus("Restoring iPod — do not disconnect it…");
    std::thread([this, kind, firmware, target]() {
        auto result = iPodRestore::restore(kind, firmware, target);
        auto* payload = new std::pair<MainWindow*, iPodRestoreResult>(this, std::move(result));
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* p = static_cast<std::pair<MainWindow*, iPodRestoreResult>*>(data);
            p->first->setStatus(p->second.success ? "iPod restore complete." : "iPod restore failed.");
            GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(p->first->window_),
                GTK_DIALOG_MODAL,
                p->second.success ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK, "%s", p->second.message.c_str());
            gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);
            delete p;
            return FALSE;
        }, payload);
    }).detach();
}

void MainWindow::resetIPodDatabase() {
    if (!ipod_.isConnected()) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
            "Connect a mounted disk-mode iPod before resetting its music database.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }
    iPodInfo info = ipod_.getInfo();
    std::string prompt = "This permanently deletes all music and playlists from:\n" +
        info.mount_point + "\n\nIt does not flash firmware or repartition the disk.\n"
        "Type ERASE MUSIC to continue:";
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Erase iPod Music Database", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Erase", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* label = gtk_label_new(prompt.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    GtkWidget* entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 10);
    gtk_widget_show_all(dlg);
    bool approved = gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK &&
        std::string(gtk_entry_get_text(GTK_ENTRY(entry))) == "ERASE MUSIC";
    gtk_widget_destroy(dlg);
    if (!approved) return;

    setStatus("Erasing and rebuilding the iPod music database…");
    if (ipod_.resetMusicDatabase()) {
        setStatus("iPod music database reset complete.");
    } else {
        std::string message = "Could not reset the iPod:\n" + ipod_.lastError();
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message.c_str());
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        setStatus("iPod reset failed.");
    }
}

void MainWindow::ripCD() {
    if (!cd_.hasAudioCD()) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "No audio CD detected in drive %s.\n"
            "Insert a CD and try again.", cd_.device().c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    // Fetch metadata
    setStatus("Reading CD metadata…");
    while (gtk_events_pending()) gtk_main_iteration();
    CDInfo info = cd_.fetchMetadata();

    // Show rip dialog
    GtkWidget* dlg = gtk_dialog_new_with_buttons("Rip CD", GTK_WINDOW(window_),
        GTK_DIALOG_MODAL, "_Rip", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    auto add_row = [&](int row, const char* label, GtkWidget* widget) {
        GtkWidget* lbl = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    };

    // Output folder
    GtkWidget* folder_btn = gtk_file_chooser_button_new("Output folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    const char* music_dir = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
    if (music_dir) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(folder_btn), music_dir);
    add_row(0, "Output folder:", folder_btn);

    // Format
    GtkWidget* format_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo), "FLAC (lossless)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo), "MP3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo), "OGG Vorbis");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo), "AAC");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(format_combo), "WAV (uncompressed)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(format_combo), 0);
    add_row(1, "Format:", format_combo);

    // Album info
    GtkWidget* album_lbl = gtk_label_new(
        info.metadata_ok
        ? (info.album_title + " — " + info.artist + "  (" + std::to_string((int)info.tracks.size()) + " tracks)").c_str()
        : ("No metadata — " + std::to_string((int)info.tracks.size()) + " tracks").c_str());
    add_row(2, "Disc:", album_lbl);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        char* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(folder_btn));
        int   fmt_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(format_combo));
        gtk_widget_destroy(dlg);

        const char* formats[] = {"flac","mp3","ogg","aac","wav"};
        std::string fmt = formats[std::max(0, std::min(fmt_idx, 4))];
        std::string out_dir = folder ? folder : "/tmp";
        g_free(folder);

        setStatus("Ripping CD…");
        std::thread([this, out_dir, fmt, info]() {
            int n = cd_.ripAllTracks(out_dir, fmt, 8, nullptr, &info);
            // Import ripped tracks
            int imported = library_.importDirectory(out_dir, true);
            gdk_threads_add_idle([](gpointer d) -> gboolean {
                auto* p = static_cast<std::pair<MainWindow*, int>*>(d);
                p->first->refreshTrackList(p->first->library_.getAllTracks());
                p->first->setStatus("Ripped " + std::to_string(p->second) + " track(s) and imported to library");
                delete p;
                return FALSE;
            }, new std::pair<MainWindow*, int>(this, imported));
        }).detach();
        return;
    }
    gtk_widget_destroy(dlg);
}

void MainWindow::burnCD() {
    GtkWidget* view = gtk_bin_get_child(GTK_BIN(track_list_));
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    GList* rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    if (!rows) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Select tracks to burn to CD first.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    std::vector<std::string> files;
    for (GList* l = rows; l; l = l->next) {
        GtkTreePath* path = static_cast<GtkTreePath*>(l->data);
        GtkTreeIter  iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(track_store_), &iter, path)) {
            gchar* fp;
            gtk_tree_model_get(GTK_TREE_MODEL(track_store_), &iter, COL_FILE_PATH, &fp, -1);
            if (fp) { files.emplace_back(fp); g_free(fp); }
        }
        gtk_tree_path_free(path);
    }
    g_list_free(rows);

    // Confirm — build the message string first; never pass user data as printf format
    std::string burn_msg = "Burn " + std::to_string(files.size()) +
                           " track(s) to CD in drive " + cd_.device() +
                           "?\n\nMake sure a blank disc is inserted.";
    GtkWidget* confirm = gtk_message_dialog_new(GTK_WINDOW(window_),
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "%s", burn_msg.c_str());
    int resp = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (resp != GTK_RESPONSE_YES) return;

    setStatus("Burning CD…");
    std::thread([this, files_copy = files]() {
        bool ok = cd_.burnCD(files_copy);
        std::string msg = ok ? "Burn complete!" : "Burn failed: " + cd_.lastError();
        gdk_threads_add_idle([](gpointer d) -> gboolean {
            auto* p = static_cast<std::pair<MainWindow*, std::string>*>(d);
            p->first->setStatus(p->second);
            delete p;
            return FALSE;
        }, new std::pair<MainWindow*, std::string>(this, msg));
    }).detach();
}

void MainWindow::createPlaylist() {
    GtkWidget* dlg = gtk_dialog_new_with_buttons("New Playlist", GTK_WINDOW(window_),
        GTK_DIALOG_MODAL, "_Create", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Playlist name");
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), entry);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const char* name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && *name) {
            int64_t id;
            library_.createPlaylist(name, id);
            refreshSourceSidebar();
            setStatus(std::string("Playlist created: ") + name);
        }
    }
    gtk_widget_destroy(dlg);
}

void MainWindow::playSelected() {
    GtkWidget* view = gtk_bin_get_child(GTK_BIN(track_list_));
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    GList* rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    if (!rows) return;

    std::vector<Track> tracks;
    for (GList* l = rows; l; l = l->next) {
        GtkTreePath* path = static_cast<GtkTreePath*>(l->data);
        GtkTreeIter  iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(track_store_), &iter, path)) {
            gint64 id;
            gtk_tree_model_get(GTK_TREE_MODEL(track_store_), &iter, COL_ID, &id, -1);
            tracks.push_back(library_.db().getTrack(id));
        }
        gtk_tree_path_free(path);
    }
    g_list_free(rows);

    if (!tracks.empty()) {
        radio_.stop();
        player_.setQueue(tracks, 0);
        player_.play();
    }
}

void MainWindow::searchChanged(const std::string& query) {
    if (query.empty()) refreshTrackList(library_.getAllTracks());
    else               refreshTrackList(library_.searchTracks(query));
}

void MainWindow::sourceSelectionChanged() {
    // Placeholder — implement based on which source node is selected
}

void MainWindow::showAbout() {
    GtkWidget* dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "LinTunes");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), "1.0.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),
        "An iTunes-equivalent music manager for Linux.\n"
        "Supports iPod sync via libgpod, CD ripping and burning,\n"
        "and Apple SuperDrive compatibility.");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dlg), GTK_LICENSE_GPL_3_0);
    const char* authors[] = {"LinTunes Contributors", nullptr};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dlg), authors);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ── Signal handlers ───────────────────────────────────────────────────────────
void MainWindow::onDestroy         (GtkWidget*, gpointer) { gtk_main_quit(); }
void MainWindow::onPlayPause       (GtkWidget*, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    if (self->player_.state() == PlayerState::Playing) self->player_.pause();
    else                                                 self->player_.play();
}
void MainWindow::onPrev            (GtkWidget*, gpointer d) { static_cast<MainWindow*>(d)->player_.previous(); }
void MainWindow::onNext            (GtkWidget*, gpointer d) { static_cast<MainWindow*>(d)->player_.next(); }
void MainWindow::onShuffle         (GtkWidget* w, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    self->player_.setShuffleMode(active ? ShuffleMode::On : ShuffleMode::Off);
}
void MainWindow::onRepeat          (GtkWidget* w, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    self->player_.setRepeatMode(active ? RepeatMode::All : RepeatMode::Off);
}
void MainWindow::onInternetRadio   (GtkWidget*, gpointer d) {
    static_cast<MainWindow*>(d)->showInternetRadio();
}
void MainWindow::onVolumeChanged   (GtkRange* r, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    const double volume = gtk_range_get_value(r);
    self->player_.setVolume(volume);
    self->radio_.setVolume(volume);
}
void MainWindow::onSeekPressed     (GtkWidget*, GdkEvent*, gpointer d) {
    static_cast<MainWindow*>(d)->seek_dragging_ = true;
}
void MainWindow::onSeekReleased    (GtkWidget*, GdkEvent*, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    self->seek_dragging_ = false;
    double frac = gtk_range_get_value(GTK_RANGE(self->seek_scale_)) / 100.0;
    int64_t dur = self->player_.duration();
    self->player_.seekTo(static_cast<int64_t>(frac * dur));
}
void MainWindow::onSeekMoved       (GtkRange*, gpointer) {}

void MainWindow::onTrackRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer d) {
    static_cast<MainWindow*>(d)->playSelected();
}
void MainWindow::onSearchChanged   (GtkSearchEntry* e, gpointer d) {
    static_cast<MainWindow*>(d)->searchChanged(gtk_entry_get_text(GTK_ENTRY(e)));
}
void MainWindow::onSourceSelChanged(GtkTreeSelection*, gpointer d) {
    static_cast<MainWindow*>(d)->sourceSelectionChanged();
}

gboolean MainWindow::onTrackPopup(GtkWidget* widget, GdkEvent* event, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    auto* ev   = reinterpret_cast<GdkEventButton*>(event);
    if (event->type == GDK_BUTTON_PRESS && ev->button == 3) {
        GtkWidget* menu = gtk_menu_new();
        auto add_item = [&](const char* label, GCallback cb) {
            GtkWidget* item = gtk_menu_item_new_with_label(label);
            g_signal_connect(item, "activate", cb, d);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        };
        add_item("Play",           G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
            static_cast<MainWindow*>(d)->playSelected();
        }));
        add_item("Add to iPod",    G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
            static_cast<MainWindow*>(d)->syncToIPod();
        }));
        add_item("Remove from Library", G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
            static_cast<MainWindow*>(d)->removeSelectedTracks(false);
        }));
        add_item("Delete File",    G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
            static_cast<MainWindow*>(d)->removeSelectedTracks(true);
        }));
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
        return TRUE;
    }
    return FALSE;
}

void MainWindow::removeSelectedTracks(bool delete_files) {
    GtkWidget* view = gtk_bin_get_child(GTK_BIN(track_list_));
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    GList* rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    if (!rows) return;

    std::vector<int64_t> ids;
    for (GList* l = rows; l; l = l->next) {
        GtkTreePath* path = static_cast<GtkTreePath*>(l->data);
        GtkTreeIter  iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(track_store_), &iter, path)) {
            gint64 id;
            gtk_tree_model_get(GTK_TREE_MODEL(track_store_), &iter, COL_ID, &id, -1);
            ids.push_back(id);
        }
        gtk_tree_path_free(path);
    }
    g_list_free(rows);

    for (int64_t id : ids) library_.removeTrack(id, delete_files);
    refreshTrackList(library_.getAllTracks());
    setStatus("Removed " + std::to_string(ids.size()) + " track(s)");
}

// Menu stubs
void MainWindow::onMenuImportFiles (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->importFiles();   }
void MainWindow::onMenuImportFolder(GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->importFolder();  }
void MainWindow::onMenuQuit        (GtkMenuItem*, gpointer)   { gtk_main_quit(); }
void MainWindow::onMenuAbout       (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->showAbout();     }
void MainWindow::onMenuNewPlaylist (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->createPlaylist();}
void MainWindow::onMenuConnectIPod (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->connectIPod();   }
void MainWindow::onMenuSyncIPod    (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->syncToIPod();    }
void MainWindow::onMenuRestoreIPod (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->restoreIPod(); }
void MainWindow::onMenuResetIPod   (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->resetIPodDatabase(); }
void MainWindow::onMenuRipCD       (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->ripCD();         }
void MainWindow::onMenuBurnCD      (GtkMenuItem*, gpointer d) { static_cast<MainWindow*>(d)->burnCD();        }
void MainWindow::onMenuEjectCD     (GtkMenuItem*, gpointer d) {
    auto* self = static_cast<MainWindow*>(d);
    self->cd_.eject();
    self->setStatus("Disc ejected");
}

// ── Add Music button — pops a small menu: "Add Files…" / "Add Folder…" ────────
void MainWindow::onAddMusicClicked(GtkWidget* btn, gpointer d) {
    GtkWidget* menu = gtk_menu_new();

    GtkWidget* item_files  = gtk_menu_item_new_with_label("Add Files…");
    GtkWidget* item_folder = gtk_menu_item_new_with_label("Add Folder…");

    g_signal_connect(item_files,  "activate", G_CALLBACK(onAddFilesActivate),  d);
    g_signal_connect(item_folder, "activate", G_CALLBACK(onAddFolderActivate), d);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_files);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_folder);
    gtk_widget_show_all(menu);

    gtk_menu_popup_at_widget(GTK_MENU(menu), btn,
                              GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST,
                              nullptr);
}

void MainWindow::onAddFilesActivate (GtkMenuItem*, gpointer d) {
    static_cast<MainWindow*>(d)->importFiles();
}
void MainWindow::onAddFolderActivate(GtkMenuItem*, gpointer d) {
    static_cast<MainWindow*>(d)->importFolder();
}

// ── Drag-and-drop handler ─────────────────────────────────────────────────────
// Called when the user drops files/folders onto the track list view.
// GTK passes a newline-separated list of file:// URIs in the selection data.
void MainWindow::onDragDataReceived(GtkWidget*,
                                     GdkDragContext* ctx,
                                     gint, gint,
                                     GtkSelectionData* data,
                                     guint, guint time,
                                     gpointer d) {
    auto* self = static_cast<MainWindow*>(d);

    gchar** uris = gtk_selection_data_get_uris(data);
    if (!uris) {
        gtk_drag_finish(ctx, FALSE, FALSE, time);
        return;
    }

    std::vector<std::string> paths;
    for (int i = 0; uris[i]; ++i) {
        // Convert file:// URI → local filesystem path
        gchar* local = g_filename_from_uri(uris[i], nullptr, nullptr);
        if (local) {
            paths.emplace_back(local);
            g_free(local);
        }
    }
    g_strfreev(uris);
    gtk_drag_finish(ctx, !paths.empty(), FALSE, time);

    if (paths.empty()) return;

    self->setStatus("Importing dropped files…");
    std::thread([self, paths]() {
        int n = self->importPaths(paths);
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* p = static_cast<std::pair<MainWindow*, int>*>(data);
            p->first->refreshTrackList(p->first->library_.getAllTracks());
            p->first->updateEmptyState();
            p->first->setStatus("Added " + std::to_string(p->second) + " track(s) to library");
            delete p;
            return FALSE;
        }, new std::pair<MainWindow*, int>(self, n));
    }).detach();
}
