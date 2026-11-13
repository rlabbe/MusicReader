This is built against a static Qt build to reduce the number of files that need to
be copied with the exe. However, mupdf uses the same harfbuzz text-shaping library
that Qt bundles internally. Packaging mupdf as a DLL seals its harfbuzz inside the
DLL where it cannot collide with Qt's copy at link time. All other libraries
(PolyMetronome, QtWakeDpiFixer, mupdf's own dependencies) are linked statically.

## Two solutions

MusicReader has two solutions that share MusicReader.vcxproj. MusicReader.sln
builds against the extern/ submodules and is used for CI and release builds.
MusicReaderDev.sln builds against sibling directories (..\metronome\,
..\QtWakeDpiFixer\) so that local changes in those repos are picked up without
pushing and pulling submodules.

The vcxproj uses the solution name to decide which paths to use — "MusicReader"
resolves to extern\..., "MusicReaderDev" resolves to ..\... The solution project
entries and the vcxproj paths must agree. If a project with the same GUID appears
at two different paths, MSBuild builds both and the output files collide.

## extern/ is read-only

The extern/ directory contains three git submodules:

  mupdf            read-only mirror of the MuPDF source
  PolyMetronome    upstream is the metronome repo (D:\dev\metronome)
  QtWakeDpiFixer   upstream is the DPI fixer repo (D:\dev\QtWakeDpiFixer)

Change nothing under extern/, including project files and platform toolsets. The
next submodule update either refuses to run or overwrites the edit.

A change to PolyMetronome or QtWakeDpiFixer is made in its own directory, committed
and pushed there, then this repository moves its pointer. All commands run in cmd,
in the directory given before each block.

## Upgrading the platform toolset

All instrctions assume the projects are in d:\dev\<subfolder>. Revise instructions
accordingly if you code is in a different location.

The application is built from four project files and all four must use the same
toolset:

    MusicReader.vcxproj
    mupdf_dll.vcxproj
    ..\metronome\PolyMetronomeLib\PolyMetronomeLib.vcxproj
    ..\QtWakeDpiFixer\QtWakeDpiFixer.vcxproj

Each repository is opened and retargeted in its own solution.

1. Open D:\dev\QtWakeDpiFixer\QtWakeDpiFixer.sln in the new Visual Studio.
2. Right-click the solution in Solution Explorer and choose Retarget solution.
   Set Platform toolset to the new toolset, for example Visual Studio 2026 (v145),
   and click OK.
3. Check the project: right-click it, Properties, Configuration Properties,
   General, Platform Toolset. If it still names the old toolset, set the new one
   there with Configuration set to All Configurations and Platform set to x64,
   then click OK.
4. Do the same in D:\dev\metronome\PolyMetronome.sln for PolyMetronomeLib and
   PolyMetronome.
5. For the two projects in this repository, open MusicReader.vcxproj and
   mupdf_dll.vcxproj with File, Open, Project/Solution and set the Platform
   Toolset the same way.
6. Commit and push the two retargeted repositories. The metronome repository also
   moves its own copy of the dpi fixer to the new commit:

    cd /d D:\dev\QtWakeDpiFixer
    git add QtWakeDpiFixer.vcxproj
    git commit -m "Retargeted to v145"
    git push

    cd /d D:\dev\metronome
    git submodule update --remote extern\QtWakeDpiFixer
    git add PolyMetronomeLib\PolyMetronomeLib.vcxproj PolyMetronome\PolyMetronome.vcxproj extern\QtWakeDpiFixer
    git commit -m "Retargeted to v145"
    git push

7. Move this repository's pointers to those commits, then commit and push this
   repository:

    cd /d D:\dev\MusicReader
    git submodule update --remote extern\PolyMetronome extern\QtWakeDpiFixer
    git add MusicReader.vcxproj mupdf_dll.vcxproj extern\PolyMetronome extern\QtWakeDpiFixer
    git commit -m "Retargeted to v145 and updated submodules"
    git push

## Building

1. Open D:\dev\MusicReader\MusicReader.sln for a release build, or
   MusicReaderDev.sln to build against local changes in the sibling directories.
2. In the toolbars select Release and x64.
3. Build, Build Solution.
4. mupdf_dll builds first, through the project reference. If mupdf does not
   compile, see the next section.
5. MusicReader.exe is copied into bin\, and mupdf.dll into bin\ and into
   MusicReader_out\x64\Release\.

## When mupdf is not rebuilt

The libmupdf target compares the mupdf sources against libmupdf.lib and skips when
the library is newer. Build, Clean removes the mupdf_dll outputs, not this library.
It lives at:

    Release and ReleaseDbgInfo   extern\mupdf\platform\win32\x64\Release\libmupdf.lib
    Debug                        extern\mupdf\platform\win32\x64\Debug\libmupdf.lib

To force a compile:

1. Delete the library for the configuration being built:

    cd /d D:\dev\MusicReader
    del "extern\mupdf\platform\win32\x64\Release\libmupdf.lib"

2. Build the mupdf_dll project in Visual Studio.
3. The file gets a new timestamp, and mupdf_dll_bin\x64\Release\mupdf.dll is
   relinked from it.

The MuPDF DLL project (mupdf_dll.vcxproj) controls which symbols are exported via
mupdf_exports.def.
