#ifndef TRANSLATE_OPTIONS_H
#define TRANSLATE_OPTIONS_H

#include <string>
#include <vector>

namespace translate {
/*
  Translator CLI options. Defaults mirror the Python translator
  (src/translate/options.py).
*/
struct Options {
    std::string domain;
    std::string task;

    // Output.
    std::string sas_file = "output.sas";

    // Relaxation.
    bool generate_relaxed_task = false;

    // Fact representation.
    bool use_partial_encoding = true; // --full-encoding flips to false

    // Invariant generation.
    int invariant_generation_max_candidates = 100000;
    int invariant_generation_max_time = 300;

    // Preconditions.
    bool add_implied_preconditions = false;

    // Filtering.
    bool filter_unreachable_facts = true; // --keep-unreachable-facts -> false
    bool filter_unimportant_vars = true;  // --keep-unimportant-variables -> false

    // Variable ordering.
    bool reorder_variables = true; // --skip-variable-reordering -> false

    // Operators.
    bool keep_no_ops = false;

    // Debug.
    bool dump_task = false;

    // "min" or "max"
    std::string layer_strategy = "min";
};

/*
  Parse argv into the singleton Options. argv[0] is the program name;
  argv[1] and argv[2] are domain and task. Throws ParseError-compatible
  std::runtime_error on misuse.
*/
void parse_options(int argc, const char *const *argv);

Options &get_options();
}

#endif
