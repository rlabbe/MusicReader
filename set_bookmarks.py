import sys
import ast
import os
from PyPDF2 import PdfReader, PdfWriter


def add_bookmarks(writer, bookmark_list, parent=None):
    for item in bookmark_list:
        title = item[0]
        page_num = item[1] - 1
        children = item[2] if len(item) > 2 else []
        current = writer.add_outline_item(title, page_num, parent=parent)
        if children:
            add_bookmarks(writer, children, parent=current)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: set_bookmarks filename 'bookmark_list_as_string'")
        sys.exit(-1)

    fname = sys.argv[1]
    try:
        bookmarks = ast.literal_eval(sys.argv[2])
        reader = PdfReader(fname)
        writer = PdfWriter()

        for page in reader.pages:
            writer.add_page(page)

        add_bookmarks(writer, bookmarks)

        base, name = os.path.split(fname)
        temp_path = os.path.join(base, '_' + name)

        with open(temp_path, 'wb') as temp_file:
            writer.write(temp_file)

        os.replace(temp_path, fname)
        sys.exit(0)

    except Exception as e:
        print(f'Error: {e}')
        sys.exit(-1)
