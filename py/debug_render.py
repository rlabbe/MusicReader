"""
Debug rendering to see what MuPDF thinks the page bounds are.
"""
import fitz
import sys

def debug_render(path, dpi=360):
    doc = fitz.open(path)
    page = doc[0]

    print(f"PDF: {path}")
    print(f"Page rect: {page.rect}")
    print(f"MediaBox: {page.mediabox}")
    print(f"CropBox: {page.cropbox}")

    # Render at specified DPI
    mat = fitz.Matrix(dpi/72, dpi/72)
    pix = page.get_pixmap(matrix=mat)

    print(f"Rendered pixmap size: {pix.width}x{pix.height}")
    print(f"Expected size at {dpi} DPI: {page.rect.width * dpi/72:.0f}x{page.rect.height * dpi/72:.0f}")

    doc.close()

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else r"D:\dev\MusicReader\bin\test_blank.pdf"
    debug_render(path)
