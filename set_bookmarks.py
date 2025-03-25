# -*- coding: utf-8 -*-
"""
Created on Mon Mar 24 22:12:39 2025

@author: rlabbe
"""

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


def process_command(line):
    try:
        parts = line.strip().split('\t', 1)
        if len(parts) != 2:
            return "ERROR: invalid input format"

        fname, bookmark_str = parts
        bookmarks = ast.literal_eval(bookmark_str)

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
        return "OK"
    except Exception as e:
        return f"ERROR: {e}"


if __name__ == "__main__":
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        result = process_command(line)
        print(result, flush=True)
