#include "pattern_collection_generator_systematic.h"

#include "utils.h"
#include "validation.h"

#include "../task_proxy.h"

#include "../plugins/plugin.h"
#include "../task_utils/causal_graph.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/timer.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stack>

using namespace std;

namespace pdbs {
struct Timeout : public exception {};

static bool patterns_are_disjoint(
    const Pattern &pattern1, const Pattern &pattern2) {
    size_t i = 0;
    size_t j = 0;
    for (;;) {
        if (i == pattern1.size() || j == pattern2.size())
            return true;
        int val1 = pattern1[i];
        int val2 = pattern2[j];
        if (val1 == val2)
            return false;
        else if (val1 < val2)
            ++i;
        else
            ++j;
    }
}

static void compute_union_pattern(
    const Pattern &pattern1, const Pattern &pattern2, Pattern &result) {
    result.clear();
    result.reserve(pattern1.size() + pattern2.size());
    set_union(pattern1.begin(), pattern1.end(),
              pattern2.begin(), pattern2.end(),
              back_inserter(result));
}


PatternCollectionGeneratorSystematic::PatternCollectionGeneratorSystematic(
    int pattern_max_size, PatternType pattern_type,
    utils::Verbosity verbosity)
    : PatternCollectionGenerator(verbosity),
      max_pattern_size(pattern_max_size),
      pattern_type(pattern_type) {
}

void PatternCollectionGeneratorSystematic::compute_eff_pre_neighbors(
    const causal_graph::CausalGraph &cg, const Pattern &pattern, vector<int> &result) const {
    /*
      Compute all variables that are reachable from pattern by an
      (eff, pre) arc and are not already contained in the pattern.
    */
    unordered_set<int> candidates;

    // Compute neighbors.
    for (int var : pattern) {
        const vector<int> &neighbors = cg.get_eff_to_pre(var);
        candidates.insert(neighbors.begin(), neighbors.end());
    }

    // Remove elements of pattern.
    for (int var : pattern) {
        candidates.erase(var);
    }

    result.assign(candidates.begin(), candidates.end());
}

vector<int> PatternCollectionGeneratorSystematic::compute_variables_with_precondition_path_to_goal(
    const TaskProxy &task_proxy, const causal_graph::CausalGraph &cg) const {
    int num_variables = task_proxy.get_variables().size();
    vector<bool> marked_variables(num_variables, false);
    stack<int> open_list;
    for (FactProxy goal : task_proxy.get_goals()) {
        open_list.push(goal.get_variable().get_id());
    }
    while (!open_list.empty()) {
        int var = open_list.top();
        open_list.pop();
        marked_variables[var] = true;
        for (int predecessor : cg.get_eff_to_pre(var)) {
            if (!marked_variables[predecessor]) {
                open_list.push(predecessor);
            }
        }
    }

    vector<int> goal_reaching_variables;
    for (int var = 0; var < num_variables; ++var) {
        if (marked_variables[var]) {
            goal_reaching_variables.push_back(var);
        }
    }
    return goal_reaching_variables;
}

void PatternCollectionGeneratorSystematic::compute_connection_points(
    const causal_graph::CausalGraph &cg, const Pattern &pattern, vector<int> &result) const {
    /*
      The "connection points" of a pattern are those variables of which
      one must be contained in an SGA pattern that can be attached to this
      pattern to form a larger interesting pattern. (Interesting patterns
      are disjoint unions of SGA patterns.)

      A variable is a connection point if it satisfies the following criteria:
      1. We can get from the pattern to the connection point via
         a (pre, eff) or (eff, eff) arc in the causal graph.
      2. It is not part of pattern.
      3. We *cannot* get from the pattern to the connection point
         via an (eff, pre) arc.

      Condition 1. is the important one. The other conditions are
      optimizations that help reduce the number of candidates to
      consider.
    */
    unordered_set<int> candidates;

    // Handle rule 1.
    for (int var : pattern) {
        const vector<int> &succ = cg.get_successors(var);
        candidates.insert(succ.begin(), succ.end());
    }

    // Handle rules 2 and 3.
    for (int var : pattern) {
        // Rule 2:
        candidates.erase(var);
        // Rule 3:
        const vector<int> &eff_pre = cg.get_eff_to_pre(var);
        for (int pre_var : eff_pre)
            candidates.erase(pre_var);
    }

    result.assign(candidates.begin(), candidates.end());
}

void PatternCollectionGeneratorSystematic::enqueue_pattern_if_new(
    const Pattern &pattern) {
    if (pattern_set.insert(pattern).second) {
        if (handle_pattern) {
            bool done = handle_pattern(pattern);
            if (done) {
                throw Timeout();
            }
        }
        patterns->push_back(pattern);
    }
}

void PatternCollectionGeneratorSystematic::build_sga_patterns(
    const TaskProxy &task_proxy,
    const causal_graph::CausalGraph &cg) {
    assert(max_pattern_size >= 1);
    assert(pattern_set.empty());
    assert(patterns && patterns->empty());

    /*
      SGA patterns are "single-goal ancestor" patterns, i.e., those
      patterns which can be generated by following eff/pre arcs from a
      single goal variable.

      This method must generate all SGA patterns up to size
      "max_pattern_size". They must be generated in order of
      increasing size, and they must be placed in "patterns".

      The overall structure of this is a similar processing queue as
      in the main pattern generation method below, and we reuse
      "patterns" and "pattern_set" between the two methods.
    */

    if (pattern_type == PatternType::INTERESTING_NON_NEGATIVE) {
        // Build goal patterns.
        for (FactProxy goal : task_proxy.get_goals()) {
            int var_id = goal.get_variable().get_id();
            enqueue_pattern_if_new({var_id});
        }
    } else if (pattern_type == PatternType::INTERESTING_GENERAL) {
        // Build atomic patterns for variables with a precondition path to a goal.
        vector<int> goal_reaching_variables =
            compute_variables_with_precondition_path_to_goal(task_proxy, cg);
        for (int var : goal_reaching_variables) {
            enqueue_pattern_if_new({var});
        }
    } else {
        ABORT("unknown pattern type");
    }

    /*
      Grow SGA patterns untill all patterns are processed. Note that
      the patterns vectors grows during the computation.
    */
    for (size_t pattern_no = 0; pattern_no < patterns->size(); ++pattern_no) {
        // We must copy the pattern because references to patterns can be invalidated.
        Pattern pattern = (*patterns)[pattern_no];
        if (pattern.size() == max_pattern_size)
            break;

        vector<int> neighbors;
        compute_eff_pre_neighbors(cg, pattern, neighbors);

        for (int neighbor_var_id : neighbors) {
            Pattern new_pattern(pattern);
            new_pattern.push_back(neighbor_var_id);
            sort(new_pattern.begin(), new_pattern.end());

            enqueue_pattern_if_new(new_pattern);
        }
    }

    pattern_set.clear();
}

void PatternCollectionGeneratorSystematic::build_patterns(
    const TaskProxy &task_proxy,
    const utils::CountdownTimer *timer) {
    int num_variables = task_proxy.get_variables().size();
    const causal_graph::CausalGraph &cg = task_proxy.get_causal_graph();

    // Generate SGA (single-goal-ancestor) patterns.
    // They are generated into the patterns variable,
    // so we swap them from there.
    build_sga_patterns(task_proxy, cg);
    PatternCollection sga_patterns;
    patterns->swap(sga_patterns);

    /* Index the SGA patterns by variable.

       Important: sga_patterns_by_var[var] must be sorted by size.
       This is guaranteed because build_sga_patterns generates
       patterns ordered by size.
    */
    vector<vector<const Pattern *>> sga_patterns_by_var(num_variables);
    for (const Pattern &pattern : sga_patterns) {
        for (int var : pattern) {
            sga_patterns_by_var[var].push_back(&pattern);
        }
    }

    // Enqueue the SGA patterns.
    for (const Pattern &pattern : sga_patterns) {
        pattern_set.insert(pattern);
        patterns->push_back(pattern);
    }
    assert(pattern_set.size() == patterns->size());


    if (log.is_at_least_normal()) {
        log << "Found " << sga_patterns.size() << " SGA patterns." << endl;
    }

    /*
      Combine patterns in the queue with SGA patterns until all
      patterns are processed. Note that the patterns vectors grows
      during the computation.
    */
    for (size_t pattern_no = 0; pattern_no < patterns->size(); ++pattern_no) {
        if (timer && timer->is_expired())
            break;

        // We must copy the pattern because references to patterns can be invalidated.
        Pattern pattern1 = (*patterns)[pattern_no];

        vector<int> neighbors;
        compute_connection_points(cg, pattern1, neighbors);

        for (int neighbor_var : neighbors) {
            const auto &candidates = sga_patterns_by_var[neighbor_var];
            for (const Pattern *p_pattern2 : candidates) {
                const Pattern &pattern2 = *p_pattern2;
                if (pattern1.size() + pattern2.size() > max_pattern_size)
                    break;  // All remaining candidates are too large.
                if (patterns_are_disjoint(pattern1, pattern2)) {
                    Pattern new_pattern;
                    compute_union_pattern(pattern1, pattern2, new_pattern);
                    enqueue_pattern_if_new(new_pattern);
                }
            }
        }
    }

    pattern_set.clear();
    if (log.is_at_least_normal()) {
        log << "Found " << patterns->size() << " interesting patterns." << endl;
    }
}

void PatternCollectionGeneratorSystematic::build_patterns_naive(
    const TaskProxy &task_proxy,
    const utils::CountdownTimer *) {
    int num_variables = task_proxy.get_variables().size();
    PatternCollection current_patterns(1);
    PatternCollection next_patterns;
    for (size_t i = 0; i < max_pattern_size; ++i) {
        if (log.is_at_least_normal()) {
            log << "Generating patterns of size " << i + 1 << endl;
        }
        for (const Pattern &current_pattern : current_patterns) {
            int max_var = -1;
            if (i > 0)
                max_var = current_pattern.back();
            for (int var = max_var + 1; var < num_variables; ++var) {
                Pattern pattern = current_pattern;
                pattern.push_back(var);
                next_patterns.push_back(pattern);
                if (handle_pattern) {
                    bool done = handle_pattern(pattern);
                    if (done) {
                        throw Timeout();
                    }
                }
                patterns->push_back(pattern);
            }
        }
        next_patterns.swap(current_patterns);
        next_patterns.clear();
    }

    if (log.is_at_least_normal()) {
        log << "Found " << patterns->size() << " patterns." << endl;
    }
}

string PatternCollectionGeneratorSystematic::name() const {
    return "systematic pattern collection generator";
}

PatternCollectionInformation PatternCollectionGeneratorSystematic::compute_patterns(
    const shared_ptr<AbstractTask> &task) {
    TaskProxy task_proxy(*task);
    patterns = make_shared<PatternCollection>();
    pattern_set.clear();
    if (pattern_type == PatternType::NAIVE) {
        build_patterns_naive(task_proxy);
    } else {
        build_patterns(task_proxy);
    }
    return PatternCollectionInformation(task_proxy, patterns, log);
}

void PatternCollectionGeneratorSystematic::generate(
    const shared_ptr<AbstractTask> &task,
    const PatternHandler &handle_pattern,
    const utils::CountdownTimer &timer) {
    this->handle_pattern = handle_pattern;
    TaskProxy task_proxy(*task);
    patterns = make_shared<PatternCollection>();
    pattern_set.clear();
    try {
        if (pattern_type == PatternType::NAIVE) {
            build_patterns_naive(task_proxy, &timer);
        } else {
            build_patterns(task_proxy, &timer);
        }
    } catch (const Timeout &) {
        cout << "Reached time limit while generating systematic patterns." << endl;
    }
    // Release memory.
    PatternSet().swap(pattern_set);
    patterns = nullptr;
}

void add_pattern_type_option(plugins::Feature &feature) {
    feature.add_option<PatternType>(
        "pattern_type",
        "type of patterns",
        "interesting_non_negative");
}

class PatternCollectionGeneratorSystematicFeature
    : public plugins::TypedFeature<PatternCollectionGenerator, PatternCollectionGeneratorSystematic> {
public:
    PatternCollectionGeneratorSystematicFeature() : TypedFeature("systematic") {
        document_title("Systematically generated patterns");
        document_synopsis(
            "Generates all (interesting) patterns with up to pattern_max_size "
            "variables. "
            "For details, see" + utils::format_conference_reference(
                {"Florian Pommerening", "Gabriele Roeger", "Malte Helmert"},
                "Getting the Most Out of Pattern Databases for Classical Planning",
                "https://ai.dmi.unibas.ch/papers/pommerening-et-al-ijcai2013.pdf",
                "Proceedings of the Twenty-Third International Joint"
                " Conference on Artificial Intelligence (IJCAI 2013)",
                "2357-2364",
                "AAAI Press",
                "2013") +
            "The pattern_type=interesting_general setting was introduced in" +
            utils::format_conference_reference(
                {"Florian Pommerening", "Thomas Keller", "Valentina Halasi",
                 "Jendrik Seipp", "Silvan Sievers", "Malte Helmert"},
                "Dantzig-Wolfe Decomposition for Cost Partitioning",
                "https://ai.dmi.unibas.ch/papers/pommerening-et-al-icaps2021.pdf",
                "Proceedings of the 31st International Conference on Automated "
                "Planning and Scheduling (ICAPS 2021)",
                "271-280",
                "AAAI Press",
                "2021"));

        add_option<int>(
            "pattern_max_size",
            "max number of variables per pattern",
            "1",
            plugins::Bounds("1", "infinity"));
        add_pattern_type_option(*this);
        add_generator_options_to_feature(*this);
    }

    virtual shared_ptr<PatternCollectionGeneratorSystematic>
    create_component(
        const plugins::Options &opts,
        const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<PatternCollectionGeneratorSystematic>(
            opts.get<int>("pattern_max_size"),
            opts.get<PatternType>("pattern_type"),
            get_generator_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<PatternCollectionGeneratorSystematicFeature> _plugin;

static plugins::TypedEnumPlugin<PatternType> _enum_plugin({
        {"naive", "all patterns up to the given size"},
        {"interesting_general",
         "only consider the union of two disjoint patterns if the union has "
         "more information than the individual patterns under a general cost "
         "partitioning"},
        {"interesting_non_negative", "like interesting_general, but considering non-negative cost partitioning"},
    });
}
