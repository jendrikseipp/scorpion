#ifndef TRANSLATE_OPTIONS_H
#define TRANSLATE_OPTIONS_H

#include <string>
#include <vector>

namespace translate {
/*
  How to normalize complex PDDL conditions. Mirrors the Python translator's
  --condition-normalization-strategy (fast_downward/translate/normalize.py).
*/
enum class ConditionNormalizationStrategy {
    // Convert conditions to disjunctive normal form.
    DNF,
    // Replace every disjunction by a derived predicate.
    AXIOMATIZE_DISJUNCTIONS,
    // Additionally replace existential quantifiers in action conditions and
    // goals by derived predicates.
    AXIOMATIZE_DISJUNCTIONS_EXISTENTIALS,
};

/*
  Translator CLI options. Defaults mirror the Python translator
  (src/translate/fast_downward/translate/options.py).
*/
struct Options {
    std::string domain;
    std::string problem;

    // Output.
    std::string sas_file = "output.sas";

    // Relaxation.
    bool generate_relaxed_task = false;

    // Fact representation.
    bool use_partial_encoding = true; // --full-encoding flips to false

    // Invariant generation.
    int invariant_generation_max_candidates = 100000;
    int invariant_generation_max_time = 300;
    // Use the CPython-compatible RNG (utils/cpython_random.h) for the
    // invariant balance checker so the C++ translator draws the same
    // sequence as the Python translator and matches its output byte-for-byte.
    // --no-cpython-rng falls back to std::mt19937 (the legacy behaviour).
    bool cpython_rng = true;

    // Preconditions.
    bool add_implied_preconditions = false;

    // Filtering.
    bool filter_unreachable_facts = true; // --keep-unreachable-facts -> false
    bool filter_unimportant_vars =
        true; // --keep-unimportant-variables -> false

    // Variable ordering.
    bool reorder_variables = true; // --skip-variable-reordering -> false

    // Operators.
    bool keep_no_ops = false;
    // --keep-duplicate-operators disables removing operators with identical
    // prevail, pre_post and cost (removal is on by default, matching Python).
    bool keep_duplicate_operators = false;

    // Debug.
    bool dump_task = false;
    // Write predicate names and arity to predicates.txt.
    bool dump_predicates = false;
    // Write static atoms to static-atoms.txt.
    bool dump_static_atoms = false;
    // Exit after parsing the PDDL files (PDDL linting mode).
    bool stop_after_parsing_pddl = false;

    // "min" or "max"
    std::string layer_strategy = "min";

    // Condition normalization.
    ConditionNormalizationStrategy condition_normalization_strategy =
        ConditionNormalizationStrategy::DNF;
};

/*
  Parse argv into the singleton Options. argv[0] is the program name;
  argv[1] and argv[2] are the domain and problem file. Throws
  ParseError-compatible std::runtime_error on misuse.
*/
void parse_options(int argc, const char *const *argv);

Options &get_options();
}

#endif
