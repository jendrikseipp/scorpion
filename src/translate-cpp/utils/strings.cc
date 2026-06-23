#include "strings.h"

#include <algorithm>
#include <cctype>

using namespace std;
namespace translate::utils {
string to_lower(string_view s) {
    string out(s);
    transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return tolower(c); });
    return out;
}

string strip(string_view s) {
    auto is_space = [](unsigned char c) { return isspace(c); };
    auto begin = find_if_not(s.begin(), s.end(), is_space);
    auto end = find_if_not(s.rbegin(), s.rend(), is_space).base();
    if (begin >= end)
        return {};
    return string(begin, end);
}

vector<string> split(string_view s, char delim) {
    vector<string> parts;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            parts.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.emplace_back(s.substr(start));
    return parts;
}

bool starts_with(string_view s, string_view prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(string_view s, string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}
