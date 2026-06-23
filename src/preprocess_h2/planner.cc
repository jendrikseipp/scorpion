#include "axiom.h"
#include "causal_graph.h"
#include "h2_mutexes.h"
#include "helper_functions.h"
#include "mutex_group.h"
#include "operator.h"
#include "state.h"
#include "variable.h"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <string_view>
#include <unordered_set>

using namespace std;

static void print_usage(const char *program_name) {
    cout
        << "Usage: " << program_name << " [OPTIONS] < input.sas\n"
        << "\n"
        << "Preprocessor for the Fast Downward planning system.\n"
        << "Reads SAS+ output from translator and produces preprocessed output.\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help                       Show this help message and exit\n"
        << "  --outfile FILE                   Write output to FILE (default: preprocessed-output.sas)\n"
        << "  --h2-time-limit SECONDS          Time limit for h^2 mutex computation in seconds (default: 300)\n"
        << "  --no-h2                          Disable h^2 mutex computation\n"
        << "  --no-backward-h2                 Disable backward h^2 mutex computation\n"
        << "  --keep-unimportant-variables     Do not perform relevance analysis\n"
        << "  --add-implied-preconditions      Include augmented preconditions\n"
        << "  --add-implied-goals              Include augmented goals\n"
        << "  --show-expensive-statistics      Compute expensive statistics\n"
        << "  --keep-duplicate-operators       Do not remove duplicate operators\n"
        << endl;
}

static void parse_error(const char *program_name, const string &message) {
    cerr << "Error: " << message << "\n\n";
    print_usage(program_name);
    exit(2);
}

static void strip_goals(vector<pair<Variable *, int>> &goals) {
    size_t new_index = 0;
    for (size_t i = 0; i < goals.size(); ++i) {
        if (goals[i].first->is_necessary()) {
            if (new_index != i)
                goals[new_index] = goals[i];
            ++new_index;
        }
    }
    goals.erase(goals.begin() + new_index, goals.end());
}

void preprocess(int argc, const char **argv) {
    int h2_mutex_time = 300; // 5 minutes to compute mutexes by default.
    bool include_augmented_preconditions = false;
    bool include_augmented_goals = false;
    bool expensive_statistics = false;
    bool disable_bw_h2 = false;
    bool keep_duplicate_operators = false;

    bool metric;
    vector<Variable *> variables;
    vector<Variable> internal_variables;
    State initial_state;
    vector<pair<Variable *, int>> goals;
    vector<MutexGroup> mutexes;
    vector<Operator> operators;
    vector<Axiom> axioms;
    string outfile = "preprocessed-output.sas";

    for (int i = 1; i < argc; ++i) {
        const string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--keep-unimportant-variables") {
            cout << "Disabling relevance analysis" << endl;
            g_do_not_prune_variables = true;
        } else if (arg == "--h2-time-limit") {
            if (++i >= argc) {
                parse_error(argv[0], "--h2-time-limit requires an argument");
            }
            try {
                h2_mutex_time = stoi(argv[i]);
                if (h2_mutex_time < 0) {
                    parse_error(
                        argv[0], "--h2-time-limit must be non-negative");
                }
            } catch (const invalid_argument &) {
                parse_error(
                    argv[0],
                    string("invalid argument for --h2-time-limit: ") + argv[i]);
            } catch (const out_of_range &) {
                parse_error(
                    argv[0],
                    string("argument for --h2-time-limit out of range: ") +
                        argv[i]);
            }
        } else if (arg == "--no-h2") {
            h2_mutex_time = 0;
        } else if (arg == "--no-backward-h2") {
            disable_bw_h2 = true;
        } else if (arg == "--add-implied-preconditions") {
            include_augmented_preconditions = true;
        } else if (arg == "--add-implied-goals") {
            include_augmented_goals = true;
        } else if (arg == "--show-expensive-statistics") {
            expensive_statistics = true;
        } else if (arg == "--keep-duplicate-operators") {
            keep_duplicate_operators = true;
        } else if (arg == "--outfile") {
            if (++i >= argc) {
                parse_error(argv[0], "--outfile requires an argument");
            }
            outfile = argv[i];
        } else {
            parse_error(argv[0], string("unknown option: ") + string(arg));
        }
    }

    read_preprocessed_problem_description(
        cin, metric, internal_variables, variables, mutexes, initial_state,
        goals, operators, axioms);

    cout << "Building causal graph..." << endl;
    CausalGraph causal_graph(variables, operators, axioms, goals);
    const vector<Variable *> &ordering = causal_graph.get_variable_ordering();

    // Remove unnecessary effects from operators and axioms, then remove
    // operators and axioms without effects.
    strip_mutexes(mutexes);
    strip_operators(operators);
    strip_axioms(axioms);

    // Compute h2 mutexes.
    H2Mutexes h2(h2_mutex_time);
    if (axioms.size() > 0) {
        cout
            << "Disabling h2 analysis because it does not currently support axioms"
            << endl;
    } else if (h2_mutex_time) {
        if (std::any_of(
                operators.cbegin(), operators.cend(),
                [](const auto &op) { return op.has_conditional_effects(); })) {
            disable_bw_h2 = true;
        }

        if (!compute_h2_mutexes(
                ordering, operators, axioms, mutexes, initial_state, goals, h2,
                disable_bw_h2)) {
            generate_unsolvable_cpp_input(outfile);
            return;
        }

        // Update the causal graph and remove unnecessary variables.
        strip_mutexes(mutexes);
        strip_operators(operators);
        strip_axioms(axioms);

        cout << "Change id of operators: " << operators.size() << endl;
        // 1) Change id of values in operators and axioms to remove unreachable
        // atoms from variables.
        for (Operator &op : operators) {
            op.remove_unreachable_atoms(ordering);
        }
        // TODO: Activate this if axioms get supported by the h2 heuristic
        // cout << "Change id of axioms: " << axioms.size() << endl;
        // for(int i = 0; i < axioms.size(); ++i){
        //     axioms[i].remove_unreachable_facts();
        // }
        cout << "Change id of mutexes" << endl;
        for (MutexGroup &mutex : mutexes) {
            mutex.remove_unreachable_atoms();
        }
        cout << "Change id of goals" << endl;
        for (auto &[goal_var, goal_val] : goals) {
            goal_val = goal_var->get_new_id(goal_val);
        }
        strip_goals(goals);
        cout << "Change id of initial state" << endl;
        if (initial_state.remove_unreachable_atoms()) {
            generate_unsolvable_cpp_input(outfile);
            return;
        }

        cout << "Remove unreachable atoms from variables: " << ordering.size()
             << endl;
        // 2) Remove unreachable atoms from variables.
        for (Variable *var : ordering) {
            if (var->is_necessary()) {
                var->remove_unreachable_atoms();
            }
        }

        // Strip after removing unreachable atoms from variables
        strip_mutexes(mutexes);
        strip_operators(operators);
        strip_axioms(axioms);

        // Update causal graph: removes unnecessary variables and updates
        // levels. NOTE: this invalidates all data structures that use variable
        // levels as indices.
        causal_graph.update();

        // Strip again: operators/mutexes/goals may have become redundant due to
        // variables being removed or variable levels changing
        strip_mutexes(mutexes);
        strip_operators(operators);
        strip_axioms(axioms);
        strip_goals(goals);
    }

    if (!keep_duplicate_operators) {
        remove_duplicate_operators(operators);
    }

    // Output some task statistics
    int facts = 0;
    int derived_vars = 0;
    for (Variable *var : ordering) {
        facts += var->get_range();
        if (var->is_derived())
            derived_vars++;
    }
    cout << "Preprocessor variables: " << ordering.size() << endl;
    cout << "Preprocessor facts: " << facts << endl;
    cout << "Preprocessor derived variables: " << derived_vars << endl;
    cout << "Preprocessor operators: " << operators.size() << endl;
    cout << "Preprocessor mutex groups: " << mutexes.size() << endl;

    if (expensive_statistics) {
        // Count potential preconditions
        int num_total_augmented = 0;
        int num_op_augmented = 0;
        int num_total_potential = 0;
        int num_op_potential = 0;
        int num_total_potential_noeff = 0;
        int num_op_potential_noeff = 0;

        for (const Operator &op : operators) {
            int count = op.count_augmented_preconditions();
            if (count) {
                num_op_augmented++;
                num_total_augmented += count;
            }
            count = op.count_potential_preconditions();
            if (count) {
                num_op_potential++;
                num_total_potential += count;
            }
            count = op.count_potential_noeff_preconditions();
            if (count) {
                num_op_potential_noeff++;
                num_total_potential_noeff += count;
            }
        }

        cout << "Augmented preconditions: " << num_total_augmented << endl;
        cout << "Ops with augmented preconditions: " << num_op_augmented
             << endl;
        cout << "Potential preconditions: " << num_total_potential << endl;
        cout << "Ops with potential preconditions: " << num_op_potential
             << endl;
        cout << "Potential preconditions contradict effects: "
             << num_total_potential_noeff << endl;
        cout << "Ops with potential preconditions contradict effects: "
             << num_op_potential_noeff << endl;
        unordered_set<vector<int>> mutexes_fw, mutexes_bw;
        for (MutexGroup &mutex : mutexes) {
            if (!mutex.is_redundant()) {
                if (mutex.is_fw())
                    mutex.add_tuples(mutexes_fw);
                else
                    mutex.add_tuples(mutexes_bw);
            }
        }
        cout << "Preprocessor mutex groups fw: " << mutexes_fw.size()
             << " bw: " << mutexes_bw.size() << endl;
    }

    if (include_augmented_preconditions) {
        for (Operator &op : operators) {
            op.include_augmented_preconditions();
        }
        // Strip operators again as augmented preconditions may reference
        // removed variables
        strip_operators(operators);
    }

    if (include_augmented_goals) {
        // Augment goals with facts that must be true due to mutex constraints
        // For each variable not in the goal, if only one value is not mutex
        // with all goal facts, add it as an augmented goal

        // Build vector indexed by variable level: -1 if not in goal, goal value
        // otherwise
        vector<int> goal_facts(ordering.size(), -1);
        for (const auto &[var, val] : goals) {
            goal_facts[var->get_level()] = val;
        }

        // Pre-filter mutexes: keep only non-redundant ones that contain at
        // least one goal fact
        vector<const MutexGroup *> relevant_mutexes;
        for (const MutexGroup &mutex : mutexes) {
            if (mutex.is_redundant())
                continue;

            if (std::any_of(
                    goals.cbegin(), goals.cend(), [&](const auto &goal) {
                        return mutex.has_atom(
                            goal.first->get_level(), goal.second);
                    })) {
                relevant_mutexes.push_back(&mutex);
            }
        }

        vector<pair<Variable *, int>> augmented_goals;

        for (Variable *var : ordering) {
            if (!var->is_necessary())
                continue;

            int var_level = var->get_level();

            // Skip variables already in the goal.
            if (goal_facts[var_level] != -1)
                continue;

            int compatible_count = 0;
            int compatible_val = -1;

            // Check each reachable value for compatibility with goals
            for (int val = 0; val < var->get_range(); val++) {
                if (!var->is_reachable(val))
                    continue;

                if (std::none_of(
                        relevant_mutexes.cbegin(), relevant_mutexes.cend(),
                        [&](const auto &mutex) {
                            return mutex->has_atom(var_level, val);
                        })) {
                    compatible_count++;
                    compatible_val = val;
                    // Early exit if more than one compatible value found
                    if (compatible_count > 1)
                        break;
                }
            }

            // If exactly one compatible value found, add it as an augmented
            // goal
            if (compatible_count == 1) {
                augmented_goals.emplace_back(var, compatible_val);
                goal_facts[var_level] = compatible_val;
            }
        }

        if (!augmented_goals.empty()) {
            cout << "Augmented " << augmented_goals.size() << " goal facts"
                 << endl;
            goals.insert(
                goals.end(), augmented_goals.begin(), augmented_goals.end());
        }
    }
    // Calculate the problem size
    int task_size = ordering.size() + facts + goals.size();

    for (const MutexGroup &mutex : mutexes)
        task_size += mutex.get_encoding_size();

    for (const Operator &op : operators)
        task_size += op.get_encoding_size();

    for (const Axiom &axiom : axioms)
        task_size += axiom.get_encoding_size();

    cout << "Preprocessor task size: " << task_size << endl;

    cout << "Writing output..." << endl;
    if (ordering.empty()) {
        generate_unsolvable_cpp_input(outfile);
    } else {
        generate_cpp_input(
            ordering, metric, mutexes, initial_state, goals, operators, axioms,
            outfile);
    }
}

int main(int argc, const char **argv) {
    clock_t start_time = clock();
    preprocess(argc, argv);
    double cpu_time_used = get_passed_time(start_time);
    cout << "Preprocessor time: " << cpu_time_used << "s" << endl;
    int peak_memory = get_peak_memory_in_kb();
    if (peak_memory != -1) {
        cout << "Preprocessor peak memory: " << peak_memory << " KB" << endl;
    }
    cout << "done" << endl;
    return 0;
}
