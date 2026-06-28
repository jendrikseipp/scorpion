#ifndef UTILS_STRINGS_H
#define UTILS_STRINGS_H

#include <string>
#include <string_view>
#include <vector>

namespace translate::utils {
std::string to_lower(std::string_view s);
std::string strip(std::string_view s);
std::vector<std::string> split(std::string_view s, char delim);
bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);
}

#endif
