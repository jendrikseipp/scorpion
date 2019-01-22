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
#include "../utils/math.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <queue>

using namespace std;

namespace pdbs {
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
        if (utils::is_product_within_limit(
                size, domain_sizes[var], numeric_limits<int>::max())) {
            size *= domain_sizes[var];
        } else {
            return -1;
        }
    }
    return size;
}

static bool contains_positive_finite_value(const vector<int> &values) {
    return any_of(values.begin(), values.end(),
                  [](int v) {return v > 0 && v != numeric_limits<int>::max();});
}

static PatternCollection get_patterns(
    const shared_ptr<AbstractTask> &task, int pattern_size) {
    cout << "Generate patterns for size " << pattern_size << endl;
    options::Options opts;
    opts.set<int>("pattern_max_size", pattern_size);
    opts.set<bool>("only_interesting_patterns", true);
    PatternCollectionGeneratorSystematic generator(opts);
    PatternCollectionInformation pci = generator.generate(task);
    PatternCollection patterns;
    for (const Pattern &pattern : *pci.get_patterns()) {
        if (static_cast<int>(pattern.size()) == pattern_size) {
            patterns.push_back(pattern);
        }
    }
    return patterns;
}


class SequentialPatternGenerator {
    shared_ptr<AbstractTask> task;
    int max_pattern_size;
    int current_pattern_size;
    PatternCollection current_patterns;
public:
    SequentialPatternGenerator(const shared_ptr<AbstractTask> &task, int max_pattern_size_)
        : task(task),
          max_pattern_size(max_pattern_size_),
          current_pattern_size(0) {
        assert(max_pattern_size_ >= 0);
        max_pattern_size = min(
            max_pattern_size, static_cast<int>(TaskProxy(*task).get_variables().size()));
    }

    Pattern get_next_pattern() {
        if (!current_patterns.empty()) {
            Pattern pattern = current_patterns.back();
            current_patterns.pop_back();
            assert(!pattern.empty());
            return pattern;
        } else if (current_pattern_size < max_pattern_size) {
            ++current_pattern_size;
            current_patterns = get_patterns(task, current_pattern_size);
            return get_next_pattern();
        } else {
            return {};
        }
    }
};


PatternCollectionGeneratorFilteredSystematic::PatternCollectionGeneratorFilteredSystematic(
    const Options &opts)
    : max_pattern_size(opts.get<int>("max_pattern_size")),
      max_pdb_size(opts.get<int>("max_pdb_size")),
      max_collection_size(opts.get<int>("max_collection_size")),
      max_patterns(opts.get<int>("max_patterns")),
      max_time(opts.get<double>("max_time")),
      max_time_per_restart(opts.get<double>("max_time_per_restart")),
      debug(opts.get<bool>("debug")) {
}

bool PatternCollectionGeneratorFilteredSystematic::select_systematic_patterns(
    const shared_ptr<AbstractTask> &task,
    const shared_ptr<cost_saturation::TaskInfo> &task_info,
    const shared_ptr<ProjectionCollection> &projections,
    PatternSet &pattern_set,
    int64_t &collection_size,
    double overall_remaining_time) {
    utils::CountdownTimer timer(min(overall_remaining_time, max_time_per_restart));
    TaskProxy task_proxy(*task);
    State initial_state = task_proxy.get_initial_state();
    vector<int> variable_domains = get_variable_domains(task_proxy);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    SequentialPatternGenerator pattern_generator(task, max_pattern_size);
    while (true) {
        if (timer.is_expired()) {
            cout << "Reached restart time limit." << endl;
            return false;
        }
        Pattern pattern = pattern_generator.get_next_pattern();
        if (pattern.empty()) {
            cout << "Generated all patterns up to size " << max_pattern_size
                 << "." << endl;
            return false;
        } else if (pattern_set.count(pattern)) {
            continue;
        }
        int pdb_size = get_pdb_size(variable_domains, pattern);
        if (pdb_size == -1 || pdb_size > max_pdb_size) {
            // Pattern is too large.
            continue;
        }

        if (static_cast<int>(projections->size()) == max_patterns) {
            cout << "Reached maximum number of patterns." << endl;
            return true;
        }

        if (max_collection_size != numeric_limits<int>::max() &&
            pdb_size > static_cast<int64_t>(max_collection_size) - collection_size) {
            cout << "Reached maximum collection size." << endl;
            return true;
        }

        unique_ptr<cost_saturation::Projection> projection =
            utils::make_unique_ptr<cost_saturation::Projection>(task_proxy, task_info, pattern);
        vector<int> goal_distances = projection->compute_goal_distances(costs);
        if (contains_positive_finite_value(goal_distances)) {
            utils::Log() << "Add pattern " << projection->get_pattern() << endl;
            vector<int> saturated_costs = projection->compute_saturated_costs(
                goal_distances, costs.size());
            cost_saturation::reduce_costs(costs, saturated_costs);
            projections->push_back(move(projection));
            pattern_set.insert(pattern);
            collection_size += pdb_size;
        }
    }
}

PatternCollectionInformation PatternCollectionGeneratorFilteredSystematic::generate(
    const shared_ptr<AbstractTask> &task) {
    utils::CountdownTimer timer(max_time);
    TaskProxy task_proxy(*task);
    shared_ptr<cost_saturation::TaskInfo> task_info =
        make_shared<cost_saturation::TaskInfo>(task_proxy);
    shared_ptr<ProjectionCollection> projections = make_shared<ProjectionCollection>();
    PatternSet pattern_set;
    int64_t collection_size = 0;
    bool limit_reached = false;
    while (!limit_reached) {
        utils::Log() << "Patterns: " << projections->size() << ", collection size: "
                     << collection_size << endl;
        int collection_size_before = collection_size;
        limit_reached = select_systematic_patterns(
            task, task_info, projections, pattern_set, collection_size,
            timer.get_remaining_time());
        if (collection_size == collection_size_before) {
            cout << "Restart did not add any pattern." << endl;
            break;
        }
        if (timer.is_expired()) {
            cout << "Reached overall time limit." << endl;
            break;
        }
    }
    shared_ptr<PatternCollection> patterns = make_shared<PatternCollection>();
    patterns->reserve(projections->size());
    for (auto &projection : *projections) {
        patterns->push_back(projection->get_pattern());
    }
    PatternCollectionInformation pci(task_proxy, patterns);
    pci.set_projections(projections);
    return pci;
}


static void add_options(OptionParser &parser) {
    parser.add_option<int>(
        "max_pattern_size",
        "maximum number of variables per pattern",
        "2",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_pdb_size",
        "maximum number of states in a PDB",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_collection_size",
        "maximum number of states in the pattern collection",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_patterns",
        "maximum number of patterns",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for generating patterns",
        "infinity",
        Bounds("0.0", "infinity"));
    parser.add_option<double>(
        "max_time_per_restart",
        "maximum time in seconds for each restart",
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
