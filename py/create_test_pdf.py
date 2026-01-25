"""
Create a multi-page PDF with blank white pages for annotation testing.
Uses PyMuPDF (fitz) to create a simple A4 PDF.
"""
import fitz  # PyMuPDF

# A4 size in points (72 points per inch)
# A4 = 210mm x 297mm = 8.27" x 11.69" = 595.28 x 841.89 points
PAGE_WIDTH = 595
PAGE_HEIGHT = 842

page_count = 3

doc = fitz.open()  # new empty PDF

for i in range(page_count):
    page = doc.new_page(width=PAGE_WIDTH, height=PAGE_HEIGHT)

    # Fill with white
    page.draw_rect(page.rect, color=(1, 1, 1), fill=(1, 1, 1))

    # Draw a simple box for click testing
    box_rect = fitz.Rect(100, 100, 300, 200)
    page.draw_rect(box_rect, color=(0, 0, 0), width=0.5)

# Save with no compression, no garbage collection - keep it simple and readable
doc.save(
    "D:\\dev\\MusicReader\\bin\\test_blank.pdf",
    garbage=0,
    deflate=False,
    clean=False,
    pretty=True,
)
doc.close()

print(f"Created test_blank.pdf: {page_count} page(s), {PAGE_WIDTH}x{PAGE_HEIGHT} points (A4)")
print("File saved to D:\\dev\\MusicReader\\bin\\test_blank.pdf")
