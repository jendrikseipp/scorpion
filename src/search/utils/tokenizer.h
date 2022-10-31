#ifndef NOVELTY_TOKENIZER_H
#define NOVELTY_TOKENIZER_H

#include <regex>
#include <iostream>


namespace utils {
template<typename TOKEN_TYPE>
class Tokenizer {
public:
    Tokenizer() { }

    using Token = std::pair<TOKEN_TYPE, std::string>;
    using Tokens = std::deque<Token>;
    using TokenRegex = std::pair<TOKEN_TYPE, std::regex>;
    using TokenRegexes = std::vector<TokenRegex>;

    static std::regex build_regex(const std::string &s, std::regex::flag_type f = std::regex_constants::ECMAScript, std::string prefix="^\\s*(", std::string suffix=")\\s*") {
        return std::regex(prefix + s + suffix, f);
    }

    /**
     * Tokenizes a string.
     */
    Tokens tokenize(const std::string& text, const TokenRegexes token_regexes) const {
        auto start = text.begin();
        const auto end = text.end();
        std::smatch match;
        Tokens tokens;
        while (start != end) {
            bool has_match = false;
            for (const auto& pair : token_regexes) {
                std::regex regex = pair.second;
                if (std::regex_search(start, end, match, regex)) {
                    tokens.emplace_back(pair.first, match[1].str());
                    start += match[0].str().size();
                    has_match = true;
                }
            }
            if (!has_match) {
                throw std::runtime_error("tokenize - unrecognized text: " + std::string(start,end));
            }
        }
        return tokens;
    }
};

}

#endif
