#ifndef TRANSLATE_PARSER_LISP_PARSER_H
#define TRANSLATE_PARSER_LISP_PARSER_H

#include "sexpr.h"

#include <iosfwd>
#include <string>

namespace translate::parser {
/*
  Tokenize a PDDL source stream into lowercase tokens. Comments (after ';')
  are stripped. Parentheses become standalone tokens, and a '?' is followed
  by the variable name as a separate token character cluster. Throws
  ParseError on non-ASCII content outside of comments.
*/
std::vector<std::string> tokenize(std::istream &input);

/*
  Parse a token sequence into a single nested list (the top-level s-exp).
  The result is the top-level list (the leading '(' must be present and is
  consumed). Throws ParseError on syntax issues.
*/
Sexpr parse_nested_list(std::istream &input);

// Convenience: parse a file containing a PDDL description.
Sexpr parse_pddl_file(const std::string &kind /*"domain" or "task"*/,
                      const std::string &filename);
}

#endif
