#pragma once

#include <string>
#include <filesystem>




#pragma once

#include <string>

namespace BookmarkSetter {

bool startup();
bool send(const std::filesystem::path &filename, const std::string &bookmark_data);
void shutdown();

};



