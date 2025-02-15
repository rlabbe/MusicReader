#pragma once
#include <vector>
#include <string>
#include "bookmark.h"


bool add_bookmarks_to_pdf(const std::string &filename,
                          const std::vector<Bookmark> &bookmarks);