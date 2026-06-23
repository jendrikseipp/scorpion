#ifndef PARSER_PARSER_H
#define PARSER_PARSER_H

#include "../pddl/task.h"
#include "sexpr.h"

#include <memory>

namespace translate::parser {
/*
  Build a parsed PDDL task from the domain and task s-expressions produced
  by parse_nested_list / parse_pddl_file. Throws ParseError if the input
  is not a valid PDDL description in the fragment supported by Fast
  Downward.
*/
pddl::Task parse_task(const Sexpr &domain, const Sexpr &task);
}

#endif
