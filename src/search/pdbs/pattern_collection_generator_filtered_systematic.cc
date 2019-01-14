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

double compute_mean_finite_value(const vector<int> &values) {
    double sum = 0;
    int size = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] != numeric_limits<int>::max()) {
            sum += values[i];
            ++size;
        }
    }
    if (size == 0) {
        return numeric_limits<double>::infinity();
    } else {
        return sum / size;
    }
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

template<class T, class S, class C>
S &Container(priority_queue<T, S, C> &q) {
    struct HackedQueue : private priority_queue<T, S, C> {
        static S &Container(priority_queue<T, S, C> &q) {
            return q.*& HackedQueue::c;
        }
    };
    return HackedQueue::Container(q);
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
          current_pattern_size(1),
          current_patterns(get_patterns(task, current_pattern_size)) {
        max_pattern_size = min(
            max_pattern_size, static_cast<int>(TaskProxy(*task).get_variables().size()));
        assert(current_pattern_size <= max_pattern_size);
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


struct Candidate {
    unique_ptr<cost_saturation::Projection> projection;
    double score;

    Candidate(unique_ptr<cost_saturation::Projection> &&projection, double score)
        : projection(move(projection)),
          score(score) {
    }

    bool operator>(const Candidate &other) const {
        return score > other.score;
    }
};


PatternCollectionGeneratorFilteredSystematic::PatternCollectionGeneratorFilteredSystematic(
    const Options &opts)
    : max_pattern_size(opts.get<int>("max_pattern_size")),
      max_collection_size(opts.get<int>("max_collection_size")),
      max_patterns(opts.get<int>("max_patterns")),
      max_time(opts.get<double>("max_time")),
      keep_best(opts.get<bool>("keep_best")),
      scoring_function(static_cast<ScoringFunction>(opts.get_enum("scoring_function"))),
      debug(opts.get<bool>("debug")) {
}

double PatternCollectionGeneratorFilteredSystematic::rate_projection(
    const cost_saturation::Projection &projection,
    const vector<int> &costs,
    const State &initial_state) {
    vector<int> h_values = projection.compute_goal_distances(costs);
    vector<int> saturated_costs = projection.compute_saturated_costs(
        h_values, costs.size());
    double avg_h = compute_mean_finite_value(h_values);
    int init_id = projection.get_abstract_state_id(initial_state);
    double init_h = h_values[init_id];
    int size = projection.get_num_states();
    int used_costs = 0;
    for (int c : saturated_costs) {
        if (c > 0) {
            used_costs += c;
        }
    }
    used_costs = max(used_costs, 1);

    double score;
    if (scoring_function == ScoringFunction::INIT_H) {
        score = init_h;
    } else if (scoring_function == ScoringFunction::AVG_H) {
        score = avg_h;
    } else if (scoring_function == ScoringFunction::INIT_H_PER_COSTS) {
        score = init_h / used_costs;
    } else if (scoring_function == ScoringFunction::AVG_H_PER_COSTS) {
        score = avg_h / used_costs;
    } else if (scoring_function == ScoringFunction::INIT_H_PER_SIZE) {
        score = init_h / size;
    } else if (scoring_function == ScoringFunction::AVG_H_PER_SIZE) {
        score = avg_h / size;
    } else {
        ABORT("Scoring function not implemented");
    }

    if (debug) {
        cout << "pattern: " << projection.get_pattern() << ", init-h: "
             << init_h << ", avg-h: " << avg_h << ", costs: " << used_costs
             << ", size: " << size << endl;
    }

    return score;
}

PatternCollectionInformation
PatternCollectionGeneratorFilteredSystematic::select_systematic_patterns(
    const shared_ptr<AbstractTask> &task) {
    utils::CountdownTimer timer(max_time);
    TaskProxy task_proxy(*task);
    shared_ptr<cost_saturation::TaskInfo> task_info =
        make_shared<cost_saturation::TaskInfo>(task_proxy);
    State initial_state = task_proxy.get_initial_state();
    vector<int> variable_domains = get_variable_domains(task_proxy);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    SequentialPatternGenerator pattern_generator(task, max_pattern_size);
    priority_queue<Candidate, vector<Candidate>, greater<vector<Candidate>::value_type>> candidates;
    int64_t collection_size = 0;
    while (true) {
        if (timer.is_expired()) {
            cout << "Reached time limit." << endl;
            break;
        }
        Pattern pattern = pattern_generator.get_next_pattern();
        if (pattern.empty()) {
            cout << "Generated all patterns up to size " << max_pattern_size
                 << "." << endl;
            break;
        }
        int pdb_size = get_pdb_size(variable_domains, pattern);
        if (pdb_size == -1) {
            // Pattern is too large.
            continue;
        }

        if (!keep_best && static_cast<int>(candidates.size()) == max_patterns) {
            cout << "Reached maximum number of patterns." << endl;
            break;
        }

        if (!keep_best &&
            max_collection_size != numeric_limits<int>::max() &&
            pdb_size > static_cast<int64_t>(max_collection_size) - collection_size) {
            cout << "Reached maximum collection size." << endl;
            break;
        }

        unique_ptr<cost_saturation::Projection> projection =
            utils::make_unique_ptr<cost_saturation::Projection>(task_proxy, task_info, pattern);
        double score = rate_projection(*projection, costs, initial_state);
        candidates.emplace(move(projection), score);
        collection_size += pdb_size;

        if (static_cast<int>(candidates.size()) > max_patterns) {
            assert(keep_best);
            if (debug) {
                cout << "Remove pattern " << candidates.top().projection->get_pattern()
                     << " with score " << candidates.top().score << endl;
            }
            collection_size -= candidates.top().projection->get_num_states();
            candidates.pop();
        }
    }
    shared_ptr<PatternCollection> patterns = make_shared<PatternCollection>();
    shared_ptr<ProjectionCollection> projections = make_shared<ProjectionCollection>();
    patterns->reserve(candidates.size());
    projections->reserve(candidates.size());
    // The PQ doesn't allow retrieving the unique_ptrs.
    for (Candidate &candidate : Container(candidates)) {
        patterns->push_back(candidate.projection->get_pattern());
        projections->push_back(move(candidate.projection));
    }
    PatternCollectionInformation pci(task_proxy, patterns);
    pci.set_projections(projections);
    return pci;
}

PatternCollectionInformation PatternCollectionGeneratorFilteredSystematic::generate(
    const shared_ptr<AbstractTask> &task) {
    return select_systematic_patterns(task);
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
    parser.add_option<bool>(
        "keep_best",
        "keep best patterns",
        "false");
    vector<string> scoring_functions;
    scoring_functions.push_back("INIT_H");
    scoring_functions.push_back("AVG_H");
    scoring_functions.push_back("INIT_H_PER_COSTS");
    scoring_functions.push_back("AVG_H_PER_COSTS");
    scoring_functions.push_back("INIT_H_PER_SIZE");
    scoring_functions.push_back("AVG_H_PER_SIZE");
    parser.add_enum_option(
        "scoring_function",
        scoring_functions,
        "scoring function",
        "INIT_H");
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
