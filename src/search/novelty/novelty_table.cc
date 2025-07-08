#include "novelty_table.h"

#include "../algorithms/array_pool.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

using namespace std;

namespace novelty {
static array_pool_template::ArrayPool<FactPair> get_effects_by_operator(
    const OperatorsProxy &ops) {
    array_pool_template::ArrayPool<FactPair> effects_by_operator;
    int total_num_effects = 0;
    for (OperatorProxy op : ops) {
        total_num_effects += op.get_effects().size();
    }
    effects_by_operator.reserve(ops.size(), total_num_effects);
    for (OperatorProxy op : ops) {
        vector<FactPair> effects;
        effects.reserve(op.get_effects().size());
        for (EffectProxy effect : op.get_effects()) {
            effects.push_back(effect.get_fact().get_pair());
        }
        sort(effects.begin(), effects.end());
        // Several conditional effects might set the same fact.
        effects.erase(unique(effects.begin(), effects.end()), effects.end());
        assert(utils::is_sorted_unique(effects));
        effects_by_operator.push_back(move(effects));
    }
    effects_by_operator.shrink_to_fit();
    return effects_by_operator;
}

static inline FactPair get_fact(const State &state, int var) {
    return {var, state.get_unpacked_values()[var]};
}

static vector<int> compute_primary_variables(const VariablesProxy &variables) {
    vector<int> primary_variables;
    for (VariableProxy var : variables) {
        if (!var.is_derived()) {
            primary_variables.push_back(var.get_id());
        }
    }
    primary_variables.shrink_to_fit();
    return primary_variables;
}

TaskInfo::TaskInfo(const TaskProxy &task_proxy) :
    primary_variables(compute_primary_variables(task_proxy.get_variables())),
    effects_by_operator(get_effects_by_operator(task_proxy.get_operators())),
    has_axioms(task_properties::has_axioms(task_proxy)) {
    // TODO: only consider primary variables for the fact (pair) indexing.
    fact_offsets.reserve(task_proxy.get_variables().size());
    num_facts = 0;
    for (VariableProxy var : task_proxy.get_variables()) {
        fact_offsets.push_back(num_facts);
        int domain_size = var.get_domain_size();
        num_facts += domain_size;
    }

    int num_vars = task_proxy.get_variables().size();
    int last_domain_size = task_proxy.get_variables()[num_vars - 1].get_domain_size();
    // We don't need offsets for facts of the last variable.
    int num_pair_offsets = num_facts - last_domain_size;
    pair_offsets.reserve(num_pair_offsets);
    int64_t current_pair_offset = 0;
    int64_t num_facts_in_higher_vars = num_facts;
    num_pairs = 0;
    for (int var_id = 0; var_id < num_vars - 1; ++var_id) {  // Skip last var.
        int domain_size = task_proxy.get_variables()[var_id].get_domain_size();
        int var_last_fact_id = get_fact_id(FactPair(var_id, domain_size - 1));
        num_facts_in_higher_vars -= domain_size;
        num_pairs += (domain_size * num_facts_in_higher_vars);
        for (int value = 0; value < domain_size; ++value) {
            pair_offsets.push_back(current_pair_offset - (var_last_fact_id + 1));
            current_pair_offset += num_facts_in_higher_vars;
        }
    }
    assert(static_cast<int>(pair_offsets.size()) == num_pair_offsets);
    assert(num_facts_in_higher_vars == last_domain_size);
    utils::g_log << "Facts: " << num_facts << endl;
    utils::g_log << "Fact pairs: " << num_pairs << endl;
#ifndef NDEBUG
    int64_t expected_id = 0;
    for (FactProxy fact_proxy1 : task_proxy.get_variables().get_facts()) {
        FactPair fact1 = fact_proxy1.get_pair();
        for (FactProxy fact_proxy2 : task_proxy.get_variables().get_facts()) {
            FactPair fact2 = fact_proxy2.get_pair();
            if (!(fact1 < fact2) || fact1.var == fact2.var) {
                continue;
            }
            int64_t id = get_pair_id(fact1, fact2);
            if (id != expected_id) {
                cout << "Fact pair " << fact1 << " & " << fact2 << endl;
                cout << "Offset: " << pair_offsets[get_fact_id(fact1)] << endl;
                cout << "ID fact2: " << get_fact_id(fact2) << endl;
                cout << "ID: " << id << endl;
                cout << "Expected id: " << expected_id << endl;
                ABORT("Pair ID does not match expected value.");
            }
            ++expected_id;
        }
    }
#endif
}

NoveltyTable::NoveltyTable(int width, const TaskInfo &task_info)
    : width(width),
      task_info(task_info) {
    reset();
}

int NoveltyTable::compute_novelty_and_update_table(const State &state) {
    const auto &primary_variables = task_info.get_primary_variables();
    int num_vars = primary_variables.size();
    int min_novelty = UNKNOWN_NOVELTY;

    // Check for novelty 1.
    for (int var : primary_variables) {
        FactPair fact = get_fact(state, var);
        int fact_id = task_info.get_fact_id(fact);
        if (!seen_facts[fact_id]) {
            seen_facts[fact_id] = true;
            min_novelty = 1;
        }
    }

    // Check for novelty 2.
    if (width == 2) {
        for (int pos1 = 0; pos1 < num_vars; ++pos1) {
            int var1 = primary_variables[pos1];
            FactPair fact1 = get_fact(state, var1);
            for (int pos2 = pos1 + 1; pos2 < num_vars; ++pos2) {
                int var2 = primary_variables[pos2];
                FactPair fact2 = get_fact(state, var2);
                uint64_t pair_id = task_info.get_pair_id(fact1, fact2);
                bool seen = seen_fact_pairs[pair_id];
                if (!seen) {
                    seen_fact_pairs[pair_id] = true;
                    min_novelty = min(min_novelty, 2);
                }
            }
        }
    }

    return min_novelty;
}

int NoveltyTable::compute_novelty_and_update_table(
    const State &parent_state, int op_id, const State &succ_state) {
    int min_novelty = UNKNOWN_NOVELTY;

    // Check for novelty 1.
    for (FactPair effect_fact : task_info.get_effects(op_id)) {
        FactPair fact = get_fact(succ_state, effect_fact.var);
        int fact_id = task_info.get_fact_id(fact);
        if (!seen_facts[fact_id]) {
            seen_facts[fact_id] = true;
            min_novelty = 1;
        }
    }

    // Check for novelty 2.
    if (width == 2) {
        for (FactPair fact1 : task_info.get_effects(op_id)) {
            FactPair parent_fact1 = get_fact(parent_state, fact1.var);
            if (fact1 == parent_fact1) {
                continue;
            }
            for (int var2 : task_info.get_primary_variables()) {
                if (fact1.var == var2) {
                    continue;
                }
                FactPair fact2 = get_fact(succ_state, var2);
                uint64_t pair_id = task_info.get_pair_id(fact1, fact2);
                bool seen = seen_fact_pairs[pair_id];
                if (!seen) {
                    seen_fact_pairs[pair_id] = true;
                    min_novelty = min(min_novelty, 2);
                }
            }
        }
    }

    return min_novelty;
}

void NoveltyTable::reset() {
    seen_facts.assign(task_info.get_num_facts(), false);
    if (width == 2) {
        seen_fact_pairs.assign(task_info.get_num_pairs(), false);
    }
}

void NoveltyTable::dump() {
    int num_seen_facts = count(seen_facts.begin(), seen_facts.end(), true);
    cout << "Seen " << num_seen_facts << "/" << task_info.get_num_facts() << " facts";
    if (width == 2) {
        int num_seen_fact_pairs = count(seen_fact_pairs.begin(), seen_fact_pairs.end(), true);
        cout << " and " << num_seen_fact_pairs << "/" << task_info.get_num_pairs() << " pairs.";
    }
    cout << endl;
}
}
