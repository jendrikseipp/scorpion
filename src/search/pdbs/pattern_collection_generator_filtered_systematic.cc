#include "pattern_collection_generator_filtered_systematic.h"

#include "pattern_collection_generator_systematic.h"
#include "pattern_database.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../cost_saturation/projection.h"
#include "../cost_saturation/utils.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

using namespace std;

namespace pdbs {
PatternCollectionGeneratorFilteredSystematic::PatternCollectionGeneratorFilteredSystematic(
    const Options &opts)
    : max_pattern_size(opts.get<int>("max_pattern_size")),
      max_collection_size(opts.get<int>("max_collection_size")),
      max_time(opts.get<double>("max_time")),
      debug(opts.get<bool>("debug")) {
}

static vector<int> get_variable_domains(const TaskProxy &task_proxy) {
    VariablesProxy variables = task_proxy.get_variables();
    vector<int> variable_domains;
    variable_domains.reserve(variables.size());
    for (VariableProxy var : variables) {
        variable_domains.push_back(var.get_domain_size());
    }
    return variable_domains;
}

static int get_pdb_size(const vector<int> &domain_sizes, const Pattern &pattern) {
    int size = 1;
    for (int var : pattern) {
        size += domain_sizes[var];
    }
    return size;
}

void PatternCollectionGeneratorFilteredSystematic::select_systematic_patterns(
    const shared_ptr<AbstractTask> &task, int max_pattern_size,
    PatternCollection &patterns) {
    utils::CountdownTimer timer(max_time);
    TaskProxy task_proxy(*task);
    State initial_state = task_proxy.get_initial_state();
    vector<int> variable_domains = get_variable_domains(task_proxy);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    int collection_size = 0;
    for (int pattern_size = 1; pattern_size <= max_pattern_size; ++pattern_size) {
        cout << "Generate patterns for size " << pattern_size << endl;
        options::Options opts;
        opts.set<int>("pattern_max_size", pattern_size);
        opts.set<bool>("only_interesting_patterns", true);
        PatternCollectionGeneratorSystematic generator(opts);
        PatternCollectionInformation pci = generator.generate(task);
        for (const Pattern &pattern : *pci.get_patterns()) {
            if (timer.is_expired()) {
                cout << "Reached time limit." << endl;
                return;
            }
            int remaining_size = max_collection_size - collection_size;
            int pdb_size = get_pdb_size(variable_domains, pattern);
            if (pdb_size > remaining_size) {
                cout << "Reached maximum collection size." << endl;
                return;
            }
            if (static_cast<int>(pattern.size()) == pattern_size) {
                if (debug) {
                    PatternDatabase pdb(task_proxy, pattern, false, costs);
                    int init_h = pdb.get_value(initial_state);
                    double avg_h = pdb.compute_mean_finite_h();
                    cost_saturation::Projection projection(task_proxy, pattern);
                    vector<int> h_values = projection.compute_goal_distances(costs);
                    vector<int> saturated_costs = projection.compute_saturated_costs(
                        h_values, costs.size());
                    int used_costs = 0;
                    for (int c : saturated_costs) {
                        if (c > 0) {
                            used_costs += c;
                        }
                    }
                    cout << "pattern " << pattern << ": " << init_h << ", "
                         << avg_h << " / " << used_costs << " = "
                         << (used_costs == 0 ? 0 : avg_h / used_costs) << endl;
                }

                patterns.push_back(pattern);
                collection_size += pdb_size;
            }
        }
    }
}

PatternCollectionInformation PatternCollectionGeneratorFilteredSystematic::generate(
    const shared_ptr<AbstractTask> &task) {
    shared_ptr<PatternCollection> patterns = make_shared<PatternCollection>();

    TaskProxy task_proxy(*task);
    max_pattern_size = min(max_pattern_size, static_cast<int>(task_proxy.get_variables().size()));
    select_systematic_patterns(task, max_pattern_size, *patterns);

    return PatternCollectionInformation(task_proxy, patterns);
}


static void add_options(OptionParser &parser) {
    parser.add_option<int>(
        "max_pattern_size",
        "maximum number of variables per pattern",
        "2",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_collection_size",
        "maximum number of states in the pattern collection",
        "200000000",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for generating patterns",
        "infinity",
        Bounds("0.0", "infinity"));
    parser.add_option<bool>(
        "debug",
        "print debugging messages",
        "false");
}

static shared_ptr<PatternCollectionGenerator> _parse(OptionParser &parser) {
    add_options(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return make_shared<PatternCollectionGeneratorFilteredSystematic>(opts);
}

static Plugin<PatternCollectionGenerator> _plugin("filtered_systematic", _parse);
}
