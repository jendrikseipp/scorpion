#include "lisp_parser.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace translate::parser {
void write_lispified(std::ostream &os, const Sexpr &expr) {
    if (expr.is_atom()) {
        os << expr.atom();
        return;
    }
    os << "(";
    bool first = true;
    for (const auto &child : expr.list()) {
        if (!first) os << " ";
        first = false;
        write_lispified(os, child);
    }
    os << ")";
}

std::string lispified(const Sexpr &expr) {
    std::ostringstream os;
    write_lispified(os, expr);
    return os.str();
}

namespace {
void to_lower_inplace(std::string &s) {
    for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}
}

std::vector<std::string> tokenize(std::istream &input) {
    std::vector<std::string> tokens;
    std::string line;
    while (std::getline(input, line)) {
        // Strip comments at the first ';'.
        auto sc = line.find(';');
        if (sc != std::string::npos)
            line.resize(sc);
        // Validate ASCII outside of comments. The PDDL parser accepts any
        // bytes in comments (we already stripped them), so we only check
        // the remaining bytes.
        for (unsigned char c : line) {
            if (c >= 0x80) {
                throw ParseError("Non-ASCII character outside comment.");
            }
        }
        // Pad parens and '?' so they tokenize cleanly. The original Python
        // replaces "(" with " ( ", ")" with " ) ", and "?" with " ?".
        std::string padded;
        padded.reserve(line.size() * 2);
        for (char c : line) {
            if (c == '(' || c == ')') {
                padded.push_back(' ');
                padded.push_back(c);
                padded.push_back(' ');
            } else if (c == '?') {
                padded.push_back(' ');
                padded.push_back('?');
            } else {
                padded.push_back(c);
            }
        }
        // Split on whitespace, lowercasing each token.
        std::istringstream iss(padded);
        std::string tok;
        while (iss >> tok) {
            to_lower_inplace(tok);
            tokens.push_back(std::move(tok));
        }
    }
    return tokens;
}

namespace {
Sexpr parse_list_aux(const std::vector<std::string> &tokens, std::size_t &i) {
    // Leading '(' has already been consumed.
    SexprList result;
    while (true) {
        if (i >= tokens.size())
            throw ParseError("Missing ')'");
        const std::string &t = tokens[i++];
        if (t == ")") return Sexpr(std::move(result));
        if (t == "(") {
            result.emplace_back(parse_list_aux(tokens, i));
        } else {
            result.emplace_back(t);
        }
    }
}
}

Sexpr parse_nested_list(std::istream &input) {
    auto tokens = tokenize(input);
    std::size_t i = 0;
    if (i >= tokens.size() || tokens[i] != "(")
        throw ParseError("Expected '('");
    ++i; // consume opening paren
    Sexpr result = parse_list_aux(tokens, i);
    if (i < tokens.size()) {
        std::string remaining;
        for (std::size_t k = i; k < tokens.size(); ++k) {
            if (k > i) remaining += " ";
            remaining += tokens[k];
        }
        throw ParseError("Tokens remaining after parsing: " + remaining);
    }
    return result;
}

Sexpr parse_pddl_file(const std::string &kind, const std::string &filename) {
    std::ifstream input(filename);
    if (!input)
        throw ParseError("Could not open " + kind + " file: " + filename);
    try {
        return parse_nested_list(input);
    } catch (const ParseError &e) {
        throw ParseError("Error: Could not parse " + kind + " file: " +
                         filename + "\nReason: " + e.what());
    }
}
}
