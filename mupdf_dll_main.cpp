// This translation unit exists only so the linker produces mupdf.dll.
//
// Every exported symbol comes from libmupdf.lib, which is pulled in whole via
// /WHOLEARCHIVE. mupdf's fz_*/pdf_* API is compiled with FZ_DLL, so it carries
// __declspec(dllexport) and lands in the DLL's export table. The bundled
// harfbuzz/freetype/zlib/etc. are not decorated, so they stay private to this
// DLL -- which is the whole point: their symbols can no longer collide with
// Qt's statically linked harfbuzz.
extern "C" int mupdf_dll_anchor = 0;
