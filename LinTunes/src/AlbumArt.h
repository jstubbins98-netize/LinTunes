#pragma once

#include <gtk/gtk.h>
#include <string>

namespace AlbumArt {
// Caller owns the returned pixbuf. Returns nullptr when no usable art exists.
GdkPixbuf* load(const std::string& audioPath, int size = 72);

// Main-thread UI entry point. Loading runs in a worker; newer requests win.
// An empty path clears artwork (for example when switching to internet radio).
void display(GtkImage* image, const std::string& audioPath);
}
