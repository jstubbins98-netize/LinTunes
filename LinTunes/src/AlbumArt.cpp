#include "AlbumArt.h"

#include <taglib/fileref.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/vorbisfile.h>
#include <taglib/opusfile.h>
#include <taglib/xiphcomment.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
constexpr std::size_t maxBytes = 16 * 1024 * 1024;
constexpr int artSize = 72;

GdkPixbuf* decode(const char* data, std::size_t length, int size) {
    if (!data || length == 0 || length > maxBytes) return nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    struct Limits { int size; bool rejected = false; } limits{size};
    g_signal_connect(loader, "size-prepared",
        G_CALLBACK(+[](GdkPixbufLoader* loader, int width, int height, gpointer p) {
            auto* limits = static_cast<Limits*>(p);
            if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
                limits->rejected = true;
                gdk_pixbuf_loader_set_size(loader, 1, 1);
                return;
            }
            const double scale = std::min(
                static_cast<double>(limits->size) / width,
                static_cast<double>(limits->size) / height);
            gdk_pixbuf_loader_set_size(loader, std::max(1, static_cast<int>(width * scale)),
                                      std::max(1, static_cast<int>(height * scale)));
        }), &limits);
    GError* error = nullptr;
    const bool written = gdk_pixbuf_loader_write(
        loader, reinterpret_cast<const guchar*>(data), length, &error);
    g_clear_error(&error);
    const bool closed = gdk_pixbuf_loader_close(loader, &error);
    g_clear_error(&error);
    GdkPixbuf* result = nullptr;
    if (written && closed && !limits.rejected) {
        result = gdk_pixbuf_loader_get_pixbuf(loader);
        if (result) g_object_ref(result);
    }
    g_object_unref(loader);
    return result;
}

GdkPixbuf* decode(const TagLib::ByteVector& bytes, int size) {
    return decode(bytes.data(), bytes.size(), size);
}

template <typename Pictures>
GdkPixbuf* flacPictures(const Pictures& pictures, int size) {
    // Prefer the front cover, then try another usable embedded picture.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto* picture : pictures)
            if ((picture->type() == TagLib::FLAC::Picture::FrontCover) == (pass == 0))
                if (auto* pixbuf = decode(picture->data(), size)) return pixbuf;
    return nullptr;
}

GdkPixbuf* embedded(TagLib::File* file, int size) {
    if (auto* mp3 = dynamic_cast<TagLib::MPEG::File*>(file)) {
        if (auto* tag = mp3->ID3v2Tag()) {
            const auto& frames = tag->frameList("APIC");
            for (int pass = 0; pass < 2; ++pass)
                for (auto* frame : frames)
                    if (auto* picture = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame))
                        if ((picture->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) == (pass == 0))
                            if (auto* pixbuf = decode(picture->picture(), size)) return pixbuf;
        }
    } else if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(file)) {
        return flacPictures(flac->pictureList(), size);
    } else if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
        if (auto* tag = mp4->tag()) {
            const auto& items = tag->itemMap();
            auto cover = items.find("covr");
            if (cover != items.end())
                for (const auto& picture : cover->second.toCoverArtList())
                    if (auto* pixbuf = decode(picture.data(), size)) return pixbuf;
        }
    } else if (auto* vorbis = dynamic_cast<TagLib::Ogg::Vorbis::File*>(file)) {
        if (vorbis->tag()) return flacPictures(vorbis->tag()->pictureList(), size);
    } else if (auto* opus = dynamic_cast<TagLib::Ogg::Opus::File*>(file)) {
        if (opus->tag()) return flacPictures(opus->tag()->pictureList(), size);
    }
    return nullptr;
}

struct Request {
    std::string path;
    guint serial;
};
}

GdkPixbuf* AlbumArt::load(const std::string& audioPath, int size) {
    if (audioPath.empty()) return nullptr;
    size = std::clamp(size, 1, 512);
    std::error_code error;
    const std::filesystem::path audio(audioPath);
    if (!std::filesystem::is_regular_file(audio, error) || error) return nullptr;
    TagLib::FileRef file(audioPath.c_str(), false);
    if (!file.isNull() && file.file() && file.file()->isValid())
        if (auto* pixbuf = embedded(file.file(), size)) return pixbuf;

    // Local sidecars only. No remote lookup or changes to the music file.
    for (const auto* name : {"cover.jpg", "cover.jpeg", "cover.png",
                             "folder.jpg", "folder.jpeg", "folder.png",
                             "front.jpg", "front.png",
                             "Cover.jpg", "Cover.png", "Folder.jpg", "Folder.png"}) {
        auto path = audio.parent_path() / name;
        error.clear();
        if (!std::filesystem::is_regular_file(path, error) || error) continue;
        auto bytes = std::filesystem::file_size(path, error);
        if (error || bytes == 0 || bytes > maxBytes) continue;
        std::vector<char> data(static_cast<std::size_t>(bytes));
        std::ifstream input(path, std::ios::binary);
        if (!input.read(data.data(), static_cast<std::streamsize>(data.size()))) continue;
        if (auto* pixbuf = decode(data.data(), data.size(), size)) return pixbuf;
    }
    return nullptr;
}

void AlbumArt::display(GtkImage* image, const std::string& audioPath) {
    if (auto* previous = static_cast<GCancellable*>(
            g_object_get_data(G_OBJECT(image), "album-art-cancel")))
        g_cancellable_cancel(previous);
    guint serial = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(image), "album-art-serial")) + 1;
    g_object_set_data(G_OBJECT(image), "album-art-serial", GUINT_TO_POINTER(serial));
    gtk_image_set_from_icon_name(image, "audio-x-generic", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(image, artSize);
    gtk_widget_set_tooltip_text(GTK_WIDGET(image),
        audioPath.empty() ? "No album artwork" : "Album artwork — loading");
    if (audioPath.empty()) return;

    auto* cancel = g_cancellable_new();
    g_object_set_data_full(G_OBJECT(image), "album-art-cancel", cancel, g_object_unref);
    auto* task = g_task_new(image, cancel,
        +[](GObject* source, GAsyncResult* result, gpointer) {
            auto* request = static_cast<Request*>(g_task_get_task_data(G_TASK(result)));
            GError* error = nullptr;
            auto* pixbuf = static_cast<GdkPixbuf*>(g_task_propagate_pointer(G_TASK(result), &error));
            if (!error && request->serial ==
                    GPOINTER_TO_UINT(g_object_get_data(source, "album-art-serial")) &&
                    gtk_widget_get_parent(GTK_WIDGET(source))) {
                if (pixbuf) gtk_image_set_from_pixbuf(GTK_IMAGE(source), pixbuf);
                gtk_widget_set_tooltip_text(GTK_WIDGET(source),
                    pixbuf ? "Album artwork for the current track" : "No album artwork available");
            }
            g_clear_object(&pixbuf);
            g_clear_error(&error);
        }, nullptr);
    g_task_set_task_data(task, new Request{audioPath, serial},
        +[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable*) {
        if (g_task_return_error_if_cancelled(task)) return;
        auto* request = static_cast<Request*>(data);
        GdkPixbuf* pixbuf = AlbumArt::load(request->path);
        g_task_return_pointer(task, pixbuf,
            +[](gpointer image) { if (image) g_object_unref(image); });
    });
    g_object_unref(task);
}
