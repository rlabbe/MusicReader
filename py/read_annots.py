import fitz
import sys


if len(sys.argv) == 2:
    filename = sys.argv[1]
else:
    filename = r"D:\dev\MusicReader\bin\test_blank.pdf"

doc = fitz.open(filename)

for page_num in range(len(doc)):
    page = doc[page_num]
    annots = list(page.annots())
    if annots:
        print(f"Page {page_num + 1}: {len(annots)} annotation(s)")
        for i, annot in enumerate(annots):
            print(f"  [{i}] type={annot.type[1]}, rect={annot.rect}, contents='{annot.info.get('content', '')}'")
    else:
        print(f"Page {page_num + 1}: no annotations")

doc.close()
