#ifndef TRANSLATE_OPTIONS_H
#define TRANSLATE_OPTIONS_H

#include <string>

namespace translate {
/*
  Translator CLI options. Defaults mirror the Python translator.
  Populated by parse_options(); a singleton Options is then accessible via
  get_options().
*/
struct Options {
    std::string domain;
    std::string task;
    std::string sas_file = "output.sas";
    std::string invariant_generation_max_candidates_str = "100000";
    int invariant_generation_max_candidates = 100000;
    int invariant_generation_max_time = 300;
    bool add_implied_preconditions = false;
    bool keep_unreachable_facts = false;
    bool keep_unimportant_variables = false;
    bool skip_variable_reordering = false;
    bool keep_no_ops = false;
    bool dump_task = false;
    std::string layer_strategy = "min"; // "min" or "max"
};

Options &get_options();
}

#endif
