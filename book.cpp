#include "book.h"

#include <cctype>
#include <fstream>
#include <set>

#include <QProcess>

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#pragma warning(pop)

#include "bookmark.h"
#include "document_info.h"
#include "fitz_utils.h"
#include "logger.h"
#include "utils.h"

namespace {

std::string trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};

    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}


std::string to_lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}


// Splits on the separator without any escaping, which is why the spec uses '|':
// Windows does not allow it in a file name.
std::vector<std::string> split_fields(const std::string& line, char separator)
{
    std::vector<std::string> fields;

    size_t start = 0;
    while (true) {
        const size_t end = line.find(separator, start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            return fields;
        start = end + 1;
    }
}


std::string path_to_utf8(const std::filesystem::path& path)
{
    return wide_to_utf8(path.wstring());
}


bool parse_on_off(const std::string& value, bool& result)
{
    if (value == "on" || value == "true" || value == "1") {
        result = true;
        return true;
    }
    if (value == "off" || value == "false" || value == "0") {
        result = false;
        return true;
    }
    return false;
}


std::string line_error(int line_number, const std::string& text)
{
    return "line " + std::to_string(line_number) + ": " + text;
}


// Resolves the name of a document to a file in directory.
//
// A name written with an extension is taken as written. A bare name matches any
// file with that stem, which is how "one" finds one.mscz, one.musicxml or
// one.pdf. Two files with the same stem are ambiguous and reported as an error.
bool resolve_document_path(const std::filesystem::path& directory,
                           const std::string& name,
                           std::filesystem::path& result,
                           std::string& error)
{
    const std::filesystem::path candidate = directory / name;

    std::error_code exists_error;
    if (!candidate.extension().empty()) {
        if (!std::filesystem::exists(candidate, exists_error)) {
            error = "file not found: " + candidate.string();
            return false;
        }

        result = candidate;
        return true;
    }

    // A file with no extension at all is taken as it is.
    if (std::filesystem::exists(candidate, exists_error)) {
        result = candidate;
        return true;
    }

    const std::string wanted = to_lower(path_to_utf8(candidate.filename()));
    const std::filesystem::path parent = candidate.parent_path();
    const std::filesystem::path search_directory = parent.empty() ? std::filesystem::path(".") : parent;

    // A PDF with the same name is the result of an earlier conversion, so it
    // never makes the name ambiguous. Only two sources with the same name do.
    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> pdfs;

    std::error_code iterate_error;
    for (const auto& entry : std::filesystem::directory_iterator(search_directory, iterate_error)) {
        std::error_code status_error;
        if (!entry.is_regular_file(status_error) || status_error)
            continue;

        if (to_lower(path_to_utf8(entry.path().stem())) != wanted)
            continue;

        if (to_lower(path_to_utf8(entry.path().extension())) == ".pdf")
            pdfs.push_back(entry.path());
        else
            sources.push_back(entry.path());
    }

    if (iterate_error) {
        error = "cannot read " + search_directory.string() + ": " + iterate_error.message();
        return false;
    }

    if (sources.size() == 1) {
        result = sources.front();
        return true;
    }

    if (sources.size() > 1) {
        std::string names;
        for (const auto& source : sources) {
            if (!names.empty())
                names += ", ";
            names += source.filename().string();
        }
        error = "'" + name + "' matches more than one file: " + names;
        return false;
    }

    if (pdfs.size() == 1) {
        result = pdfs.front();
        return true;
    }

    error = "no file matches '" + name + "'";
    return false;
}


// Runs "musescore -o <pdf> <score>" and writes the PDF to pdf_path, which the
// caller owns, so a converted score never overwrites a file next to the score
// itself.
bool export_score_to_pdf(const std::filesystem::path& musescore,
                         const BookEntry& entry,
                         const std::filesystem::path& pdf_path,
                         std::string& error)
{
    if (!std::filesystem::exists(musescore)) {
        error = "MuseScore program not found: " + musescore.string();
        return false;
    }

    const QString program = QString::fromStdU16String(musescore.u16string());
    const QStringList arguments {"-o", QString::fromStdU16String(pdf_path.u16string()),
                                 QString::fromStdU16String(entry.score.u16string())};

    const int exit_code = QProcess::execute(program, arguments);

    if (exit_code != 0 || !std::filesystem::exists(pdf_path)) {
        error = "MuseScore failed with exit code " + std::to_string(exit_code) + " for " + entry.score.string();
        return false;
    }

    return true;
}

} // namespace


bool parse_book_spec(const std::filesystem::path& spec_file, BookSpec& spec, std::string& error)
{
    spec = BookSpec {};
    spec.source = spec_file;

    std::ifstream file(spec_file);
    if (!file.is_open()) {
        error = "Cannot open " + spec_file.string();
        return false;
    }

    const std::filesystem::path directory = spec_file.parent_path();
    std::string output_name;

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;

        const size_t equals = line.find('=');
        if (equals != std::string::npos) {
            const std::string key = trim(line.substr(0, equals));
            const std::string value = trim(line.substr(equals + 1));

            if (key == "output") {
                output_name = value;
            } else if (key == "outline") {
                if (!parse_on_off(to_lower(value), spec.outline)) {
                    error = line_error(line_number, "outline must be on or off, not '" + value + "'");
                    return false;
                }
            } else if (key == "toc") {
                if (!parse_on_off(to_lower(value), spec.toc)) {
                    error = line_error(line_number, "toc must be on or off, not '" + value + "'");
                    return false;
                }
            } else if (key == "toc_title") {
                spec.toc_title = value;
            } else {
                error = line_error(line_number, "unknown setting '" + key + "'");
                return false;
            }
            continue;
        }

        const std::vector<std::string> fields = split_fields(line, '|');

        const std::string name = trim(fields[0]);
        if (name.empty()) {
            error = line_error(line_number, "no file name");
            return false;
        }

        BookEntry entry;
        std::string resolve_error;
        if (!resolve_document_path(directory, name, entry.score, resolve_error)) {
            error = line_error(line_number, resolve_error);
            return false;
        }

        entry.convert = to_lower(path_to_utf8(entry.score.extension())) != ".pdf";

        const std::string stem = path_to_utf8(entry.score.stem());

        if (fields.size() > 1)
            entry.bookmark = trim(fields[1]);
        if (entry.bookmark.empty())
            entry.bookmark = stem;

        if (fields.size() > 2)
            entry.toc = trim(fields[2]);
        if (entry.toc.empty())
            entry.toc = entry.bookmark;

        spec.entries.push_back(entry);
    }

    if (spec.entries.empty()) {
        error = "No documents in " + spec_file.string();
        return false;
    }

    if (output_name.empty())
        output_name = path_to_utf8(spec_file.stem()) + ".pdf";

    spec.output = directory / output_name;
    return true;
}


bool build_book(const BookSpec& spec,
                const std::filesystem::path& musescore,
                bool save_exports,
                const BookConfirmFn& confirm_overwrite,
                const BookProgressFn& progress,
                std::string& error)
{
    const int total = static_cast<int>(spec.entries.size());
    if (total == 0) {
        error = "The book has no documents";
        return false;
    }

    std::vector<BookEntry> entries = spec.entries;

    // Converted scores go to the temp directory, never next to the sources.
    std::error_code directory_error;
    const std::filesystem::path work_directory =
        std::filesystem::temp_directory_path(directory_error) / "MusicReaderBook";
    if (directory_error) {
        error = "Cannot find the temporary directory";
        return false;
    }
    std::filesystem::create_directories(work_directory, directory_error);
    if (directory_error) {
        error = "Cannot create " + work_directory.string() + ": " + directory_error.message();
        return false;
    }

    // With save_exports each converted document keeps its own PDF next to the
    // book. Where every PDF goes is decided before anything is written, so the
    // files that would be replaced can be confirmed up front. The book's own
    // file name is claimed first, so a document with the same name cannot
    // overwrite it.
    const std::filesystem::path output_directory = spec.output.parent_path();
    std::set<std::filesystem::path> export_paths;
    export_paths.insert(spec.output);

    for (int i = 0; i < total; ++i) {
        if (!entries[i].convert) {
            entries[i].pdf = entries[i].score;
            continue;
        }

        const std::string stem = path_to_utf8(entries[i].score.stem());

        if (save_exports) {
            std::filesystem::path export_path = output_directory / (stem + ".pdf");
            for (int suffix = 2; export_paths.count(export_path) != 0; ++suffix)
                export_path = output_directory / (stem + "_" + std::to_string(suffix) + ".pdf");

            export_paths.insert(export_path);
            entries[i].pdf = export_path;
        } else {
            entries[i].pdf = work_directory / (std::to_string(i + 1) + "_" + stem + ".pdf");
        }
    }

    if (confirm_overwrite) {
        std::vector<std::filesystem::path> existing_files;
        std::error_code exists_error;

        if (std::filesystem::exists(spec.output, exists_error))
            existing_files.push_back(spec.output);

        if (save_exports) {
            for (const BookEntry& entry : entries) {
                if (entry.convert && std::filesystem::exists(entry.pdf, exists_error))
                    existing_files.push_back(entry.pdf);
            }
        }

        if (!existing_files.empty() && !confirm_overwrite(existing_files)) {
            error = "Cancelled";
            return false;
        }
    }

    for (int i = 0; i < total; ++i) {
        if (!entries[i].convert) {
            if (progress && !progress(i, total, "Adding " + entries[i].score.filename().string())) {
                error = "Cancelled";
                return false;
            }
            continue;
        }

        if (progress && !progress(i, total, "Exporting " + entries[i].score.filename().string())) {
            error = "Cancelled";
            return false;
        }

        if (!export_score_to_pdf(musescore, entries[i], entries[i].pdf, error))
            return false;
    }

    if (spec.toc)
        logger::warning("The table of contents is not built yet, creating the book without it");

    // Count pages first, so the bookmarks carry final page numbers and the
    // output file can be written in one go at the end.
    std::vector<int> page_counts(entries.size());

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) {
        error = "Cannot create the mupdf context";
        return false;
    }

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);

        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string pdf_path = path_to_utf8(entries[i].pdf);
            fz_document* document = fz_open_document(ctx, pdf_path.c_str());
            page_counts[i] = fz_count_pages(ctx, document);
            fz_drop_document(ctx, document);
        }
    }
    fz_catch(ctx)
    {
        error = std::string("Cannot read an exported PDF: ") + fz_caught_message(ctx);
        fz_drop_context(ctx);
        return false;
    }

    std::vector<Bookmark> bookmarks;
    int page = 1;
    for (size_t i = 0; i < entries.size(); ++i) {
        bookmarks.emplace_back(entries[i].bookmark, page);
        page += page_counts[i];
    }

    if (progress && !progress(total, total, "Merging")) {
        error = "Cancelled";
        fz_drop_context(ctx);
        return false;
    }

    // Written next to the output and renamed over it at the end, so a failure
    // leaves the previous book in place.
    const std::filesystem::path temporary = spec.output.parent_path() / (path_to_utf8(spec.output.stem()) + ".tmp.pdf");

    pdf_document* merged = nullptr;
    fz_try(ctx)
    {
        merged = pdf_create_document(ctx);

        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string pdf_path = path_to_utf8(entries[i].pdf);
            fz_document* source_document = fz_open_document(ctx, pdf_path.c_str());
            pdf_document* source = pdf_specifics(ctx, source_document);

            for (int part_page = 0; part_page < page_counts[i]; ++part_page)
                pdf_graft_page(ctx, merged, -1, source, part_page);

            fz_drop_document(ctx, source_document);
        }

        pdf_write_options options = pdf_default_write_options;
        pdf_save_document(ctx, merged, path_to_utf8(temporary).c_str(), &options);
    }
    fz_catch(ctx)
    {
        error = std::string("Cannot merge the PDFs: ") + fz_caught_message(ctx);
        pdf_drop_document(ctx, merged);
        fz_drop_context(ctx);
        return false;
    }

    pdf_drop_document(ctx, merged);
    fz_drop_context(ctx);

    if (spec.outline) {
        const BookmarkResult result = add_bookmarks_to_pdf(temporary, bookmarks);
        if (result != BookmarkResult::Success) {
            error = "Cannot write the PDF bookmarks, error " + std::to_string(static_cast<int>(result));
            std::filesystem::remove(temporary);
            return false;
        }
    }

    if (!MoveFileExW(temporary.wstring().c_str(), spec.output.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = "Cannot replace " + spec.output.string() + ", error " + std::to_string(GetLastError());
        std::filesystem::remove(temporary);
        return false;
    }

    if (!document_info::write_bookmarks(spec.output, bookmarks))
        logger::error("Cannot write the .mrd file for {}", spec.output.string());

    // The converted scores are no longer needed; a source PDF is never touched.
    // Saved exports stay. What is left after a failure stays too, so the
    // problem can be looked at.
    if (!save_exports) {
        for (const BookEntry& entry : entries) {
            if (entry.convert)
                std::filesystem::remove(entry.pdf, directory_error);
        }
    }

    if (progress)
        progress(total, total, "Done");

    return true;
}
