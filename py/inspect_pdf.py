"""
Inspect PDF structure to debug annotation issues.
"""
import fitz
import sys

def inspect_pdf(path):
    doc = fitz.open(path)
    print(f"PDF: {path}")
    print(f"Page count: {len(doc)}")

    for i, page in enumerate(doc):
        print(f"\n=== Page {i} ===")
        print(f"  MediaBox: {page.mediabox}")
        print(f"  CropBox: {page.cropbox}")
        print(f"  Rect: {page.rect}")

        annots = list(page.annots())
        print(f"  Annotations: {len(annots)}")
        for j, annot in enumerate(annots):
            print(f"    [{j}] Type: {annot.type}")
            print(f"         Rect: {annot.rect}")
            print(f"         Info: {annot.info}")
            try:
                print(f"         Contents: {annot.info.get('content', 'N/A')}")
            except:
                pass

    doc.close()

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else r"D:\dev\MusicReader\bin\test_blank.pdf"
    inspect_pdf(path)
