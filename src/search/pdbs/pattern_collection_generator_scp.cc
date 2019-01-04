#include "pattern_collection_generator_scp.h"

#include "canonical_pdbs_heuristic.h"
#include "incremental_canonical_pdbs.h"
#include "pattern_database.h"
#include "validation.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/causal_graph.h"
#include "../task_utils/sampling.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/markup.h"
#include "../utils/math.h"
#include "../utils/memory.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"
#include "../utils/timer.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <iostream>
#include <limits>

using namespace std;

namespace pdbs {
struct HillClimbingTimeout : public exception {};
struct HillClimbingMaxPDBsGenerated : public exception {};

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

/*
  When growing a pattern, we only want to consider successor patterns
  that are *interesting*. A pattern is interesting if the subgraph of
  the causal graph induced by the pattern satisfies the following two
  properties:
  A. it is weakly connected (considering all kinds of arcs)
  B. from every variable in the pattern, a goal variable is reachable by a
     path that only uses pre->eff arcs

  We can use the assumption that the pattern we want to extend is
  already interesting, so the question is how an interesting pattern
  can be obtained from an interesting pattern by adding one variable.

  There are two ways to do this:
  1. Add a *predecessor* of an existing variable along a pre->eff arc.
  2. Add any *goal variable* that is a weakly connected neighbour of an
     existing variable (using any kind of arc).

  Note that in the iPDB paper, the second case was missed. Adding it
  significantly helps with performance in our experiments (see
  issue743, msg6595).

  In our implementation, for efficiency we replace condition 2. by
  only considering causal graph *successors* (along either pre->eff or
  eff--eff arcs), because these can be obtained directly, and the
  missing case (predecessors along pre->eff arcs) is already covered
  by the first condition anyway.

  This method precomputes all variables which satisfy conditions 1. or
  2. for a given neighbour variable already in the pattern.
*/
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
      min_improvement(opts.get<int>("min_improvement")),
      max_time(opts.get<double>("max_time")),
      max_generated_patterns(opts.get<int>("max_generated_patterns")),
      rng(utils::parse_rng_from_options(opts)),
      num_rejected(0),
      hill_climbing_timer(0) {
}

int PatternCollectionGeneratorSCP::generate_candidate_pdbs(
    const TaskProxy &task_proxy,
    const vector<vector<int>> &relevant_neighbours,
    const PatternDatabase &pdb,
    set<Pattern> &generated_patterns,
    PDBCollection &candidate_pdbs) {
    const Pattern &pattern = pdb.get_pattern();
    int pdb_size = pdb.get_size();
    int max_pdb_size = 0;
    for (int pattern_var : pattern) {
        assert(utils::in_bounds(pattern_var, relevant_neighbours));
        const vector<int> &connected_vars = relevant_neighbours[pattern_var];

        // Only use variables which are not already in the pattern.
        vector<int> relevant_vars;
        set_difference(
            connected_vars.begin(), connected_vars.end(),
            pattern.begin(), pattern.end(),
            back_inserter(relevant_vars));

        for (int rel_var_id : relevant_vars) {
            if (hill_climbing_timer->is_expired())
                throw HillClimbingTimeout();

            VariableProxy rel_var = task_proxy.get_variables()[rel_var_id];
            int rel_var_size = rel_var.get_domain_size();
            if (utils::is_product_within_limit(pdb_size, rel_var_size,
                                               pdb_max_size)) {
                Pattern new_pattern(pattern);
                new_pattern.push_back(rel_var_id);
                sort(new_pattern.begin(), new_pattern.end());
                if (!generated_patterns.count(new_pattern)) {
                    /*
                      If we haven't seen this pattern before, generate a PDB
                      for it and add it to candidate_pdbs if its size does not
                      surpass the size limit.
                    */
                    generated_patterns.insert(new_pattern);
                    candidate_pdbs.push_back(
                        make_shared<PatternDatabase>(task_proxy, new_pattern));
                    max_pdb_size = max(max_pdb_size,
                                       candidate_pdbs.back()->get_size());
                    if (static_cast<int>(generated_patterns.size()) >= max_generated_patterns)
                        throw HillClimbingMaxPDBsGenerated();
                }
            } else {
                ++num_rejected;
            }
        }
    }
    return max_pdb_size;
}

void PatternCollectionGeneratorSCP::sample_states(
    const sampling::RandomWalkSampler &sampler,
    int init_h,
    vector<State> &samples) {
    assert(samples.empty());

    samples.reserve(num_samples);
    for (int i = 0; i < num_samples; ++i) {
        samples.push_back(sampler.sample_state(
                              init_h,
                              [this](const State &state) {
                                  return current_pdbs->is_dead_end(state);
                              }));
        if (hill_climbing_timer->is_expired()) {
            throw HillClimbingTimeout();
        }
    }
}

pair<int, int> PatternCollectionGeneratorSCP::find_best_improving_pdb(
    const vector<State> &samples,
    const vector<int> &samples_h_values,
    PDBCollection &candidate_pdbs) {
    /*
      TODO: The original implementation by Haslum et al. uses A* to compute
      h values for the sample states only instead of generating all PDBs.
      improvement: best improvement (= highest count) for a pattern so far.
      We require that a pattern must have an improvement of at least one in
      order to be taken into account.
    */
    int improvement = 0;
    int best_pdb_index = -1;

    // Iterate over all candidates and search for the best improving pattern/pdb
    for (size_t i = 0; i < candidate_pdbs.size(); ++i) {
        if (hill_climbing_timer->is_expired())
            throw HillClimbingTimeout();

        const shared_ptr<PatternDatabase> &pdb = candidate_pdbs[i];
        if (!pdb) {
            /* candidate pattern is too large or has already been added to
               the canonical heuristic. */
            continue;
        }
        /*
          If a candidate's size added to the current collection's size exceeds
          the maximum collection size, then forget the pdb.
        */
        int combined_size = current_pdbs->get_size() + pdb->get_size();
        if (combined_size > collection_max_size) {
            candidate_pdbs[i] = nullptr;
            continue;
        }

        /*
          Calculate the "counting approximation" for all sample states: count
          the number of samples for which the current pattern collection
          heuristic would be improved if the new pattern was included into it.
        */
        /*
          TODO: The original implementation by Haslum et al. uses m/t as a
          statistical confidence interval to stop the A*-search (which they use,
          see above) earlier.
        */
        int count = 0;
        MaxAdditivePDBSubsets max_additive_subsets =
            current_pdbs->get_max_additive_subsets(pdb->get_pattern());
        for (int sample_id = 0; sample_id < num_samples; ++sample_id) {
            const State &sample = samples[sample_id];
            assert(utils::in_bounds(sample_id, samples_h_values));
            int h_collection = samples_h_values[sample_id];
            if (is_heuristic_improved(
                    *pdb, sample, h_collection, max_additive_subsets)) {
                ++count;
            }
        }
        if (count > improvement) {
            improvement = count;
            best_pdb_index = i;
        }
        if (count > 0) {
            cout << "pattern: " << candidate_pdbs[i]->get_pattern()
                 << " - improvement: " << count << endl;
        }
    }

    return make_pair(improvement, best_pdb_index);
}

bool PatternCollectionGeneratorSCP::is_heuristic_improved(
    const PatternDatabase &pdb, const State &sample, int h_collection,
    const MaxAdditivePDBSubsets &max_additive_subsets) {
    // h_pattern: h-value of the new pattern
    int h_pattern = pdb.get_value(sample);

    if (h_pattern == numeric_limits<int>::max()) {
        return true;
    }

    // h_collection: h-value of the current collection heuristic
    if (h_collection == numeric_limits<int>::max())
        return false;

    for (const auto &subset : max_additive_subsets) {
        int h_subset = 0;
        for (const shared_ptr<PatternDatabase> &additive_pdb : subset) {
            /* Experiments showed that it is faster to recompute the
               h values than to cache them in an unordered_map. */
            int h = additive_pdb->get_value(sample);
            if (h == numeric_limits<int>::max())
                return false;
            h_subset += h;
        }
        if (h_pattern + h_subset > h_collection) {
            /*
              return true if a max additive subset is found for
              which the condition is met
            */
            return true;
        }
    }
    return false;
}

void PatternCollectionGeneratorSCP::hill_climbing(
    const TaskProxy &task_proxy) {
    hill_climbing_timer = new utils::CountdownTimer(max_time);

    cout << "Average operator cost: "
         << task_properties::get_average_operator_cost(task_proxy) << endl;

    const vector<vector<int>> relevant_neighbours =
        compute_relevant_neighbours(task_proxy);

    // Candidate patterns generated so far (used to avoid duplicates).
    set<Pattern> generated_patterns;
    // The PDBs for the patterns in generated_patterns that satisfy the size
    // limit to avoid recomputation.
    PDBCollection candidate_pdbs;
    // The maximum size over all PDBs in candidate_pdbs.
    int max_pdb_size = 0;

    int num_iterations = 0;

    try {
        for (const shared_ptr<PatternDatabase> &current_pdb :
             *(current_pdbs->get_pattern_databases())) {
            int new_max_pdb_size = generate_candidate_pdbs(
                task_proxy, relevant_neighbours, *current_pdb, generated_patterns,
                candidate_pdbs);
            max_pdb_size = max(max_pdb_size, new_max_pdb_size);
        }
        /*
          NOTE: The initial set of candidate patterns (in generated_patterns) is
          guaranteed to be "normalized" in the sense that there are no duplicates
          and patterns are sorted.
        */
        cout << "Done calculating initial candidate PDBs" << endl;

        State initial_state = task_proxy.get_initial_state();
        sampling::RandomWalkSampler sampler(task_proxy, *rng);
        vector<State> samples;
        vector<int> samples_h_values;

        while (true) {
            ++num_iterations;
            int init_h = current_pdbs->get_value(initial_state);
            cout << "current collection size is "
                 << current_pdbs->get_size() << endl;
            cout << "current initial h value: ";
            if (current_pdbs->is_dead_end(initial_state)) {
                cout << "infinite => stopping hill climbing" << endl;
                break;
            } else {
                cout << init_h << endl;
            }

            samples.clear();
            samples_h_values.clear();
            sample_states(sampler, init_h, samples);
            for (const State &sample : samples) {
                samples_h_values.push_back(current_pdbs->get_value(sample));
            }

            pair<int, int> improvement_and_index =
                find_best_improving_pdb(samples, samples_h_values, candidate_pdbs);
            int improvement = improvement_and_index.first;
            int best_pdb_index = improvement_and_index.second;

            if (improvement < min_improvement) {
                cout << "Improvement below threshold. Stop hill climbing."
                     << endl;
                break;
            }

            // Add the best PDB to the CanonicalPDBsHeuristic.
            assert(best_pdb_index != -1);
            const shared_ptr<PatternDatabase> &best_pdb =
                candidate_pdbs[best_pdb_index];
            const Pattern &best_pattern = best_pdb->get_pattern();
            cout << "found a better pattern with improvement " << improvement
                 << endl;
            cout << "pattern: " << best_pattern << endl;
            current_pdbs->add_pdb(best_pdb);

            // Generate candidate patterns and PDBs for next iteration.
            int new_max_pdb_size = generate_candidate_pdbs(
                task_proxy, relevant_neighbours, *best_pdb, generated_patterns,
                candidate_pdbs);
            max_pdb_size = max(max_pdb_size, new_max_pdb_size);

            // Remove the added PDB from candidate_pdbs.
            candidate_pdbs[best_pdb_index] = nullptr;

            cout << "Hill climbing time so far: "
                 << hill_climbing_timer->get_elapsed_time()
                 << endl;
        }
    } catch (HillClimbingTimeout &) {
        cout << "Time limit reached. Abort hill climbing." << endl;
    } catch (HillClimbingMaxPDBsGenerated &) {
        cout << "Maximum number of PDBs generated. Abort hill climbing." << endl;
    }

    cout << "iPDB: iterations = " << num_iterations << endl;
    cout << "iPDB: number of patterns = "
         << current_pdbs->get_pattern_databases()->size() << endl;
    cout << "iPDB: size = " << current_pdbs->get_size() << endl;
    cout << "iPDB: generated = " << generated_patterns.size() << endl;
    cout << "iPDB: rejected = " << num_rejected << endl;
    cout << "iPDB: maximum pdb size = " << max_pdb_size << endl;
    cout << "iPDB: hill climbing time: "
         << hill_climbing_timer->get_elapsed_time() << endl;

    delete hill_climbing_timer;
    hill_climbing_timer = nullptr;
}

PatternCollectionInformation PatternCollectionGeneratorSCP::generate(
    const shared_ptr<AbstractTask> &task) {
    TaskProxy task_proxy(*task);
    utils::Timer timer;

    // Generate initial collection: a pattern for each goal variable.
    PatternCollection initial_pattern_collection;
    for (FactProxy goal : task_proxy.get_goals()) {
        int goal_var_id = goal.get_variable().get_id();
        initial_pattern_collection.emplace_back(1, goal_var_id);
    }
    current_pdbs = utils::make_unique_ptr<IncrementalCanonicalPDBs>(
        task_proxy, initial_pattern_collection);
    cout << "Done calculating initial PDB collection" << endl;

    State initial_state = task_proxy.get_initial_state();
    if (!current_pdbs->is_dead_end(initial_state) && max_time > 0) {
        hill_climbing(task_proxy);
    }

    cout << "Pattern generation (hill climbing) time: " << timer << endl;
    return current_pdbs->get_pattern_collection_information();
}


static void add_hillclimbing_options(OptionParser &parser) {
    parser.add_option<int>(
        "pdb_max_size",
        "maximal number of states per pattern database ",
        "2000000",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "collection_max_size",
        "maximal number of states in the pattern collection",
        "20000000",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "num_samples",
        "number of samples (random states) on which to evaluate each "
        "candidate pattern collection",
        "1000",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "min_improvement",
        "minimum number of samples on which a candidate pattern "
        "collection must improve on the current one to be considered "
        "as the next pattern collection ",
        "10",
        Bounds("1", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for improving the initial pattern "
        "collection via hill climbing. If set to 0, no hill climbing "
        "is performed at all. Note that this limit only affects hill "
        "climbing. Use max_time_dominance_pruning to limit the time "
        "spent for pruning dominated patterns.",
        "infinity",
        Bounds("0.0", "infinity"));
    parser.add_option<int>(
        "max_generated_patterns",
        "maximum number of generated patterns",
        "infinity",
        Bounds("0", "infinity"));
    utils::add_rng_options(parser);
}

static void check_hillclimbing_options(
    OptionParser &parser, const Options &opts) {
    if (opts.get<int>("min_improvement") > opts.get<int>("num_samples"))
        parser.error("minimum improvement must not be higher than number of "
                     "samples");
}

static shared_ptr<PatternCollectionGenerator> _parse(OptionParser &parser) {
    add_hillclimbing_options(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    check_hillclimbing_options(parser, opts);
    if (parser.dry_run())
        return nullptr;

    return make_shared<PatternCollectionGeneratorSCP>(opts);
}

static Plugin<PatternCollectionGenerator> _plugin("scp", _parse);
}
