#define NOMINMAX
#include "document.h"
#include <QGuiApplication>
#include <QScreen>
#include <unordered_set>
#include <qpainter.h>
#include <Windows.h>
#include "logger.h"
#include "fitz_utils.h"
#include "bookmark.h"
#pragma warning(push,1)
#include <mupdf/pdf.h>
#pragma warning(pop)

#if !defined(NDEBUG)
#pragma warning(push)
#pragma warning( push, 1 )
#include <opencv2/imgproc/imgproc.hpp>
#pragma warning(pop)

#define IF_DEBUG(x) x
#else
#define IF_DEBUG(x)
#endif


extern bool delete_all_freetext_annotations(fz_context *ctx, pdf_document *pdf);



}