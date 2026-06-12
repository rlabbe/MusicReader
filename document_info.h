#pragma once

#include <filesystem>
#include <vector>
#include "performance_data.h"
#include "bookmark.h"

// A document's info file holds data that is kept alongside a PDF but is not
// part of the PDF itself. It is a single JSON file (<pdf-stem>.mrd) with one
// top-level section per kind of data, so new data types can be added later
// without disturbing the existing ones.
//
// Sections currently used:
//   "performance" - page breaks, paper crops, metronome state
//   "bookmarks"   - an exported copy of the bookmark tree
//   "favorite"    - whether the user marked this document a favorite
//
// Each writer reads the whole file first and replaces only its own section,
// so independent sections never overwrite each other.
namespace document_info {

// The info file path for a PDF: same name, ".mrd" extension.
std::filesystem::path path_for(const std::filesystem::path& pdf_path);

// Performance section. read_performance returns default-constructed data
// when the file or section is absent.
PerformanceData read_performance(const std::filesystem::path& pdf_path);
bool write_performance(const std::filesystem::path& pdf_path, const PerformanceData& performance);

// Bookmark section. read_bookmarks returns an empty vector when the file or
// section is absent. has_bookmarks reports whether a non-empty section exists.
std::vector<Bookmark> read_bookmarks(const std::filesystem::path& pdf_path);
bool write_bookmarks(const std::filesystem::path& pdf_path, const std::vector<Bookmark>& bookmarks);
bool has_bookmarks(const std::filesystem::path& pdf_path);

// Favorite flag. is_favorite reports whether the document is marked a
// favorite; set_favorite stores or clears it.
bool is_favorite(const std::filesystem::path& pdf_path);
bool set_favorite(const std::filesystem::path& pdf_path, bool favorite);

// TEMPORARY one-time migration. If a legacy .perf and/or bookmark .txt file
// sits next to the PDF, fold its contents into the .mrd file and rename the
// original to <name>.bak. Remove once every document has been migrated.
void migrate_legacy_files(const std::filesystem::path& pdf_path);

} // namespace document_info
