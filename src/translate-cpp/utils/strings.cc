#include "strings.h"

#include <algorithm>
#include <cctype>

namespace translate::utils {
std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string strip(std::string_view s) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto begin = std::find_if_not(s.begin(), s.end(), is_space);
    auto end = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            parts.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.emplace_back(s.substr(start));
    return parts;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}
