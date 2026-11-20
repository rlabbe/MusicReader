# MuseScore PDF export

## NAME

MuseScore4 - convert a MuseScore score to PDF

## SYNOPSIS

MuseScore4 -o file.pdf file.mscz

## DESCRIPTION

MuseScore Studio exports a score to PDF from the command line. The `-o` option switches the application to converter mode and suppresses the graphical user interface. The output format follows the extension of the output file name. This page lists the options that affect PDF export in MuseScore 4.7.

## OPTIONS

**-o, --export-to file** — Export the input score to file. The format follows the file name extension. Switches to converter mode, no window.

**--page n** — Use with `-o`. Export only page n, counted from 1. Works for PDF, PNG, SVG, MSCZ.

**-j, --job file.json** — Process a conversion job. The file holds a JSON array of objects with keys `in` and `out`. An `out` element of the form `[ "prefix", "suffix" ]` writes one file per part, with the part name between prefix and suffix.

**-S, --style file** — Load the style file before conversion.

**--transpose options** — Transpose the score before conversion. Options are JSON, for example `{"mode":"by_interval","direction":"up","transposeInterval":12}`.

**-P, --export-score-parts** — Use with `-o file.pdf`. Writes each part as a separate PDF, with the part name replacing an asterisk in the `-o` file name. Without an asterisk every part overwrites the same output file.

**-f, --force** — Ignore warnings about a corrupted score or a score from another version.

**-F, --factory-settings** — Use built-in default settings and delete user preferences.

## EXAMPLES

`MuseScore4 -o out.pdf in.mscz` — One score to PDF, all pages.

`MuseScore4 -o page3.pdf --page 3 in.mscz` — Page 3 only.

`MuseScore4 -o out-*.pdf in.mscz` — One PDF per part, named `out-<part>.pdf`.

`MuseScore4 -j job.json` — Batch job.

## EXIT STATUS

0 on success, greater than 0 on error.

## NOTES

The options above were checked against MuseScore source tag `v4.7.7`, files `src/app/internal/commandlineparser.cpp` and `src/converter/internal/convertercontroller.cpp`.
