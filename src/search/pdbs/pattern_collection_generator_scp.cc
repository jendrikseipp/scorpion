#include "pattern_collection_generator_scp.h"

#include "pattern_database.h"

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_proxy.h"

#include "../cost_saturation/projection.h"
#include "../cost_saturation/utils.h"
#include "../task_utils/causal_graph.h"
#include "../task_utils/sampling.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

using namespace std;

namespace pdbs {
static const int INF = numeric_limits<int>::max();

static vector<int> get_goal_variables(const TaskProxy &task_proxy) {
    vector<int> goal_vars;
    GoalsProxy goals = task_proxy.get_goals();
    goal_vars.reserve(goals.size());
    for (FactProxy goal : goals) {
        goal_vars.push_back(goal.get_variable().get_id());
    }
    assert(utils::is_sorted_unique(goal_vars));
    return goal_vars;
}

static vector<vector<int>> compute_relevant_neighbours(const TaskProxy &task_proxy) {
    const causal_graph::CausalGraph &causal_graph = task_proxy.get_causal_graph();
    const vector<int> goal_vars = get_goal_variables(task_proxy);

    vector<vector<int>> connected_vars_by_variable;
    VariablesProxy variables = task_proxy.get_variables();
    connected_vars_by_variable.reserve(variables.size());
    for (VariableProxy var : variables) {
        int var_id = var.get_id();

        // Consider variables connected backwards via pre->eff arcs.
        const vector<int> &pre_to_eff_predecessors = causal_graph.get_eff_to_pre(var_id);

        // Consider goal variables connected (forwards) via eff--eff and pre->eff arcs.
        const vector<int> &causal_graph_successors = causal_graph.get_successors(var_id);
        vector<int> goal_variable_successors;
        set_intersection(
            causal_graph_successors.begin(), causal_graph_successors.end(),
            goal_vars.begin(), goal_vars.end(),
            back_inserter(goal_variable_successors));

        // Combine relevant goal and non-goal variables.
        vector<int> relevant_neighbours;
        set_union(
            pre_to_eff_predecessors.begin(), pre_to_eff_predecessors.end(),
            goal_variable_successors.begin(), goal_variable_successors.end(),
            back_inserter(relevant_neighbours));

        connected_vars_by_variable.push_back(move(relevant_neighbours));
    }
    return connected_vars_by_variable;
}


PatternCollectionGeneratorSCP::PatternCollectionGeneratorSCP(const Options &opts)
    : pdb_max_size(opts.get<int>("pdb_max_size")),
      collection_max_size(opts.get<int>("collection_max_size")),
      num_samples(opts.get<int>("num_samples")),
      min_improvement(opts.get<double>("min_improvement")),
      max_time(opts.get<double>("max_time")),
      debug(opts.get<bool>("debug")),
      rng(utils::parse_rng_from_options(opts)),
      init_h(0) {
}

void PatternCollectionGeneratorSCP::sample_states(
    const sampling::RandomWalkSampler &sampler,
    int init_h,
    vector<State> &samples) {
    samples.reserve(num_samples);
    while (static_cast<int>(samples.size()) < num_samples) {
        samples.push_back(
            sampler.sample_state(
                init_h,
                [this](const State &state) {
                    return compute_current_heuristic(state) == INF;
                }));
    }
}

double PatternCollectionGeneratorSCP::evaluate_pdb(const PatternDatabase &pdb) {
    if (num_samples == 0) {
        return pdb.compute_mean_finite_h();
    } else {
        int num_improvements = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            int old_h = sample_h_values[i];
            int new_h = pdb.get_value(samples[i]);
            if (new_h > old_h) {
                ++num_improvements;
            }
        }
        return num_improvements;
    }
}

vector<int> PatternCollectionGeneratorSCP::get_connected_variables(
    const Pattern &pattern) {
    if (pattern.empty()) {
        return goal_vars;
    } else {
        unordered_set<int> candidate_vars;
        for (int var : pattern) {
            const vector<int> &neighbours = relevant_neighbours[var];
            candidate_vars.insert(neighbours.begin(), neighbours.end());
        }
        vector<int> connected_vars(candidate_vars.begin(), candidate_vars.end());
        sort(connected_vars.begin(), connected_vars.end());
        return connected_vars;
    }
}

pair<int, double> PatternCollectionGeneratorSCP::compute_best_variable_to_add(
    const TaskProxy &task_proxy, const vector<int> &costs,
    const Pattern &pattern, int num_states, const utils::CountdownTimer &timer) {
    vector<int> connected_vars = get_connected_variables(pattern);

    // Ignore variables already in the pattern.
    vector<int> relevant_vars;
    set_difference(
        connected_vars.begin(), connected_vars.end(),
        pattern.begin(), pattern.end(),
        back_inserter(relevant_vars));

    if (relevant_vars.empty()) {
        return {
                   -1, 0.
        };
    }

    // TODO: try simple hill climbing.
    int best_var = -1;
    double max_improvement = (num_samples > 0) ? min_improvement - 1 : 0.;
    for (int var : relevant_vars) {
        if (timer.is_expired()) {
            break;
        }
        int domain_size = task_proxy.get_variables()[var].get_domain_size();
        if (!utils::is_product_within_limit(num_states, domain_size, pdb_max_size)) {
            continue;
        }
        vector<int> new_pattern = pattern;
        new_pattern.push_back(var);
        sort(new_pattern.begin(), new_pattern.end());
        // TODO: Store best PDB?
        PatternDatabase pdb(task_proxy, new_pattern, false, costs);
        double improvement = evaluate_pdb(pdb);
        cout << "pattern " << new_pattern << ": " << improvement << endl;
        if (improvement > max_improvement) {
            best_var = var;
            max_improvement = improvement;
        }
    }
    return {
               best_var, max_improvement
    };
}

Pattern PatternCollectionGeneratorSCP::compute_next_pattern(
    const TaskProxy &task_proxy, const vector<int> &costs, const utils::CountdownTimer &timer) {
    Pattern pattern;
    int num_states = 1;
    double score = 0.;
    while (!timer.is_expired()) {
        auto result = compute_best_variable_to_add(task_proxy, costs, pattern, num_states, timer);
        int var = result.first;
        double new_score = result.second;
        if (var == -1 || new_score <= score) {
            break;
        }
        pattern.push_back(var);
        sort(pattern.begin(), pattern.end());
        int domain_size = task_proxy.get_variables()[var].get_domain_size();
        num_states *= domain_size;
        score = new_score;
        utils::Log() << "pattern: " << pattern << ", score: " << score
                     << ", size: " << num_states << endl;
    }
    return pattern;
}

int PatternCollectionGeneratorSCP::compute_current_heuristic(const State &state) const {
    int sum_h = 0;
    for (size_t i = 0; i < projections.size(); ++i) {
        const cost_saturation::Projection &projection = *projections[i];
        int state_id = projection.get_abstract_state_id(state);
        int h = cost_partitioned_h_values[i][state_id];
        if (h == numeric_limits<int>::max()) {
            return h;
        }
        sum_h += h;
    }
    return sum_h;
}

static int compute_sum(int a, int b) {
    if (a == INF || b == INF) {
        return INF;
    } else {
        return a + b;
    }
}

PatternCollectionInformation PatternCollectionGeneratorSCP::generate(
    const shared_ptr<AbstractTask> &task) {
    TaskProxy task_proxy(*task);
    utils::CountdownTimer timer(max_time);
    utils::Log log;

    relevant_neighbours = compute_relevant_neighbours(task_proxy);
    goal_vars = get_goal_variables(task_proxy);

    shared_ptr<PatternCollection> patterns = make_shared<PatternCollection>();
    sampling::RandomWalkSampler sampler(task_proxy, *rng);

    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    while (!timer.is_expired()) {
        // Sample states.
        samples.clear();
        samples.push_back(task_proxy.get_initial_state());
        sample_states(sampler, init_h, samples);
        sample_h_values.resize(samples.size(), 0);

        // Find pattern.
        Pattern pattern = compute_next_pattern(task_proxy, costs, timer);
        if (pattern.empty()) {
            break;
        }
        patterns->push_back(pattern);
        projections.push_back(utils::make_unique_ptr<cost_saturation::Projection>(task_proxy, pattern));
        cost_saturation::Projection &projection = *projections.back();
        vector<int> h_values = projection.compute_goal_distances(costs);
        cost_partitioned_h_values.push_back(h_values);
        log << "Add pattern " << pattern << endl;

        // Update h values for initial state and samples.
        int init_id = projection.get_abstract_state_id(task_proxy.get_initial_state());
        if (h_values[init_id] == INF) {
            break;
        }
        init_h += h_values[init_id];
        assert(samples.size() == sample_h_values.size());
        for (size_t sample_id = 0; sample_id < samples.size(); ++sample_id) {
            const State &sample = samples[sample_id];
            int &sample_h = sample_h_values[sample_id];
            sample_h = compute_sum(sample_h, h_values[projection.get_abstract_state_id(sample)]);
        }

        // Compute SCF and reduce remaining costs.
        vector<int> saturated_costs = projection.compute_saturated_costs(h_values, costs.size());
        cost_saturation::reduce_costs(costs, saturated_costs);

        projection.remove_transition_system();
        if (num_samples == 0) {
            projections.pop_back();
            cost_partitioned_h_values.pop_back();
        }
    }

    cout << "Pattern generation (scp) time: " << timer.get_elapsed_time() << endl;
    return PatternCollectionInformation(task_proxy, patterns);
}


static void add_options(OptionParser &parser) {
    parser.add_option<int>(
        "pdb_max_size",
        "maximal number of states per pattern database ",
        "2000000",
        Bounds("1", "infinity"));
    // TODO: Respect collection_max_size.
    parser.add_option<int>(
        "collection_max_size",
        "maximal number of states in the pattern collection",
        "20000000",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "num_samples",
        "number of samples (random states) on which to evaluate each "
        "candidate pattern. If num_samples=0, use average h value.",
        "0",
        Bounds("0", "infinity"));
    parser.add_option<double>(
        "min_improvement",
        "minimum number of samples on which a candidate pattern "
        "must improve on the current one to be considered "
        "as the next pattern collection ",
        "0.001",
        Bounds("0.001", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for generating patterns",
        "infinity",
        Bounds("0.0", "infinity"));
    parser.add_option<bool>(
        "debug",
        "print debugging messages",
        "false");
    utils::add_rng_options(parser);
}

static shared_ptr<PatternCollectionGenerator> _parse(OptionParser &parser) {
    add_options(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return make_shared<PatternCollectionGeneratorSCP>(opts);
}

static Plugin<PatternCollectionGenerator> _plugin("scp", _parse);
}
