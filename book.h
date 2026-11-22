#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Implements merging several musescore/musicXML/PDFs into a single book
//
// The .mrb file is a line oriented text file. "setting = value" lines apply to
// the whole book, every other line names one document, with optional fields
// after "|":
//
//   one.mscz
//   two.mscz | Score Two | Second score for the contents
//
// A document's bookmark defaults to the file name without extension, and its
// table of contents text defaults to the bookmark.
//
// The extension is optional. A bare name matches any source file with that
// stem, so "one" finds one.mscz, one.mscx or one.musicxml. One.pdf is ignored
// while a source with the same name exists, because the PDF is the result of an
// earlier conversion. Two sources with the same name are an error. A PDF is
// copied into the book as it is; anything else is converted by MuseScore first.
struct BookEntry {
    std::filesystem::path score; // source file, as written in the spec
    std::filesystem::path pdf;   // PDF that goes into the book, filled by build_book
    bool convert = true;         // false when score is already a PDF
    std::string bookmark;        // text of the outline entry
    std::string toc;             // text of the table of contents entry
};

struct BookSpec {
    std::filesystem::path source; // the .mrb file
    std::filesystem::path output; // the merged PDF
    bool outline = true;          // write PDF bookmarks
    bool toc = false;             // reserved, not built yet
    std::string toc_title = "Contents";
    std::vector<BookEntry> entries;
};

// Reports the document being processed and how many are done.
using BookProgressFn = std::function<void(int current, int total, const std::string& status)>;

// Called once with every file the build would replace, before anything is
// written. Return false to stop the build without touching any of them.
using BookConfirmFn = std::function<bool(const std::vector<std::filesystem::path>& existing_files)>;

// Reads spec_file into spec and checks that every document exists. Returns
// false and sets error; syntax problems name the line number.
bool parse_book_spec(const std::filesystem::path& spec_file, BookSpec& spec, std::string& error);

// Converts every document that is not already a PDF with MuseScore, merges the
// PDFs into spec.output, then writes the outline and the .mrd sidecar. With
// save_exports the converted PDFs are kept in the output directory, named after
// the source; otherwise they are temporary and removed. confirm_overwrite is
// asked about every existing file before the first one is replaced. Returns
// false and sets error, and leaves the existing files untouched when it fails.
bool build_book(const BookSpec& spec,
                const std::filesystem::path& musescore,
                bool save_exports,
                const BookConfirmFn& confirm_overwrite,
                const BookProgressFn& progress,
                std::string& error);
