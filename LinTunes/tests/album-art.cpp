#include "AlbumArt.h"
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static TagLib::ByteVector png(guint32 color) {
    GdkPixbuf* image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 120, 60);
    gdk_pixbuf_fill(image, color);
    gchar* bytes = nullptr;
    gsize length = 0;
    require(gdk_pixbuf_save_to_buffer(image, &bytes, &length, "png", nullptr, nullptr),
            "PNG fixture creation failed");
    TagLib::ByteVector result(bytes, static_cast<unsigned int>(length));
    g_free(bytes);
    g_object_unref(image);
    return result;
}
static void write(const fs::path& path, const TagLib::ByteVector& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), bytes.size());
}
static void check(const fs::path& path, bool red) {
    GdkPixbuf* image = AlbumArt::load(path.string());
    require(image != nullptr, "Expected artwork was not loaded");
    require(gdk_pixbuf_get_width(image) == 72 && gdk_pixbuf_get_height(image) == 36,
            "Artwork scaling did not preserve aspect ratio");
    const auto* pixel = gdk_pixbuf_read_pixels(image);
    require((red && pixel[0] > 200 && pixel[1] < 50) ||
            (!red && pixel[0] < 50 && pixel[1] > 200), "Wrong cover selected");
    g_object_unref(image);
}
static TagLib::FLAC::Picture* picture(const TagLib::ByteVector& bytes) {
    auto* result = new TagLib::FLAC::Picture;
    result->setType(TagLib::FLAC::Picture::FrontCover);
    result->setMimeType("image/png");
    result->setData(bytes);
    return result;
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Fixture directory required");
        const fs::path root(argv[1]);
        const auto red = png(0xff0000ff), green = png(0x00ff00ff);
        require(!AlbumArt::load(""), "Empty path should not load artwork");
        require(!AlbumArt::load((root/"missing.mp3").string()), "Missing audio should not load artwork");
        require(!AlbumArt::load((root/"sample.wav").string()), "Missing art should return null");
        write(root/"cover.png", red);
        check(root/"sample.wav", true);
        { std::ofstream invalid(root/"cover.jpg"); invalid << "not an image"; }
        check(root/"sample.wav", true);
        {
            TagLib::MPEG::File file((root/"sample.mp3").c_str());
            auto* back = new TagLib::ID3v2::AttachedPictureFrame;
            back->setType(TagLib::ID3v2::AttachedPictureFrame::BackCover);
            back->setMimeType("image/png"); back->setPicture(red);
            file.ID3v2Tag(true)->addFrame(back);
            auto* front = new TagLib::ID3v2::AttachedPictureFrame;
            front->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
            front->setMimeType("image/png"); front->setPicture(green);
            file.ID3v2Tag(true)->addFrame(front);
            require(file.save(), "MP3 fixture save failed");
        }
        {
            TagLib::FLAC::File file((root/"sample.flac").c_str());
            file.addPicture(picture(green));
            require(file.save(), "FLAC fixture save failed");
        }
        {
            TagLib::MP4::File file((root/"sample.m4a").c_str());
            TagLib::MP4::CoverArtList pictures;
            pictures.append(TagLib::MP4::CoverArt(TagLib::MP4::CoverArt::PNG, green));
            file.tag()->setItem("covr", TagLib::MP4::Item(pictures));
            require(file.save(), "MP4 fixture save failed");
        }
        {
            TagLib::Ogg::Vorbis::File file((root/"sample.ogg").c_str());
            file.tag()->addPicture(picture(green));
            require(file.save(), "Vorbis fixture save failed");
        }
        {
            TagLib::Ogg::Opus::File file((root/"sample.opus").c_str());
            file.tag()->addPicture(picture(green));
            require(file.save(), "Opus fixture save failed");
        }
        for (const auto* name : {"sample.mp3", "sample.flac", "sample.m4a", "sample.ogg", "sample.opus"})
            check(root/name, false); // Embedded front art wins over red folder cover.
        fs::remove(root/"cover.png");
        require(!AlbumArt::load((root/"sample.wav").string()), "Corrupt sidecar must be ignored");
        fs::remove(root/"cover.jpg");
        write(root/"Folder.png", red);
        check(root/"sample.wav", true);
        std::cout << "PASS: embedded MP3/FLAC/MP4/Vorbis/Opus, front-cover preference, sidecars, scaling, missing and corrupt images.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
