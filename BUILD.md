This is built against a static Qt build to reduce the number of files that need to
be copied with the exe. However, mupdf uses the same harfbuzz text-shaping library
that Qt bundles internally. Packaging mupdf as a DLL seals its harfbuzz inside the
DLL where it cannot collide with Qt's copy at link time. All other libraries
(PolyMetronome, QtWakeDpiFixer, mupdf's own dependencies) are linked statically.

MusicReader has two solutions that share MusicReader.vcxproj. MusicReader.sln
builds against the extern/ submodules and is used for CI and release builds.
MusicReaderDev.sln builds against sibling directories (..\metronome\,
..\QtWakeDpiFixer\) so that local changes in those repos are picked up without
pushing and pulling submodules.

The vcxproj uses the solution name to decide which paths to use — "MusicReader"
resolves to extern\..., "MusicReaderDev" resolves to ..\... The solution project
entries and the vcxproj paths must agree. If a project with the same GUID appears
at two different paths, MSBuild builds both and the output files collide.

The extern/ directory contains three git submodules:

  mupdf            read-only mirror of the MuPDF source
  PolyMetronome    upstream is the metronome repo (D:\dev\metronome)
  QtWakeDpiFixer   upstream is the DPI fixer repo (D:\dev\QtWakeDpiFixer)

The MuPDF DLL project (mupdf_dll.vcxproj) builds the DLL and controls which
symbols are exported via mupdf_exports.def.
