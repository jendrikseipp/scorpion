#include "lisp_parser.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;
namespace translate::parser {
void write_lispified(ostream &os, const Sexpr &expr) {
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

string lispified(const Sexpr &expr) {
    ostringstream os;
    write_lispified(os, expr);
    return os.str();
}

namespace {
void to_lower_inplace(string &s) {
    for (auto &c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
}
}

vector<string> tokenize(istream &input) {
    vector<string> tokens;
    string line;
    while (getline(input, line)) {
        // Strip comments at the first ';'.
        auto sc = line.find(';');
        if (sc != string::npos)
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
        string padded;
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
        istringstream iss(padded);
        string tok;
        while (iss >> tok) {
            to_lower_inplace(tok);
            tokens.push_back(move(tok));
        }
    }
    return tokens;
}

namespace {
Sexpr parse_list_aux(const vector<string> &tokens, size_t &i) {
    // Leading '(' has already been consumed.
    SexprList result;
    while (true) {
        if (i >= tokens.size())
            throw ParseError("Missing ')'");
        const string &t = tokens[i++];
        if (t == ")") return Sexpr(move(result));
        if (t == "(") {
            result.emplace_back(parse_list_aux(tokens, i));
        } else {
            result.emplace_back(t);
        }
    }
}
}

Sexpr parse_nested_list(istream &input) {
    auto tokens = tokenize(input);
    size_t i = 0;
    if (i >= tokens.size() || tokens[i] != "(")
        throw ParseError("Expected '('");
    ++i; // consume opening paren
    Sexpr result = parse_list_aux(tokens, i);
    if (i < tokens.size()) {
        string remaining;
        for (size_t k = i; k < tokens.size(); ++k) {
            if (k > i) remaining += " ";
            remaining += tokens[k];
        }
        throw ParseError("Tokens remaining after parsing: " + remaining);
    }
    return result;
}

Sexpr parse_pddl_file(const string &kind, const string &filename) {
    ifstream input(filename);
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
