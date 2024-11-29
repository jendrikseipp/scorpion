#include "novelty_table.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

using namespace std;

namespace novelty {
FactIndexer::FactIndexer(const TaskProxy &task_proxy) {
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
    int current_pair_offset = 0;
    int num_facts_in_higher_vars = num_facts;
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
#ifndef NDEBUG
    cout << "Facts: " << num_facts << endl;
    cout << "Pairs: " << num_pairs << endl;
    cout << "Pair offsets: " << pair_offsets << endl;
#endif
}

NoveltyTable::NoveltyTable(
    const TaskProxy &task_proxy, int width, const shared_ptr<FactIndexer> &fact_indexer_)
    : width(width),
      fact_indexer(fact_indexer_),
      compute_novelty_timer(false) {
    if (!fact_indexer) {
        cout << "Create fact indexer." << endl;
        fact_indexer = make_shared<FactIndexer>(task_proxy);
    }
    reset();
}

int NoveltyTable::compute_novelty_and_update_table(const State &state) {
    compute_novelty_timer.resume();
    int num_vars = state.size();
    int novelty = UNKNOWN_NOVELTY;

    // Check for novelty 2.
    if (width == 2) {
        for (int var1 = 0; var1 < num_vars; ++var1) {
            FactPair fact1 = state[var1].get_pair();
            for (int var2 = var1 + 1; var2 < num_vars; ++var2) {
                FactPair fact2 = state[var2].get_pair();
                int pair_id = fact_indexer->get_pair_id(fact1, fact2);
                bool seen = seen_fact_pairs[pair_id];
                if (!seen) {
                    novelty = 2;
                    seen_fact_pairs[pair_id] = true;
                }
            }
        }
    }

    // Check for novelty 1.
    for (FactProxy fact_proxy : state) {
        FactPair fact = fact_proxy.get_pair();
        int fact_id = fact_indexer->get_fact_id(fact);
        if (!seen_facts[fact_id]) {
            seen_facts[fact_id] = true;
            novelty = 1;
        }
    }

    compute_novelty_timer.stop();
    return novelty;
}

int NoveltyTable::compute_novelty_and_update_table(
    const OperatorProxy &op, const State &succ_state) {
    compute_novelty_timer.resume();
    int novelty = UNKNOWN_NOVELTY;

    // Check for novelty 2.
    if (width == 2) {
        int num_vars = succ_state.size();
        for (EffectProxy effect : op.get_effects()) {
            FactPair fact1 = effect.get_fact().get_pair();
            for (int var2 = 0; var2 < num_vars; ++var2) {
                if (fact1.var == var2) {
                    continue;
                }
                FactPair fact2 = succ_state[var2].get_pair();
                int pair_id = fact_indexer->get_pair_id(fact1, fact2);
                bool seen = seen_fact_pairs[pair_id];
                if (!seen) {
                    novelty = 2;
                    seen_fact_pairs[pair_id] = true;
                }
            }
        }
    }

    // Check for novelty 1.
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        int fact_id = fact_indexer->get_fact_id(fact);
        if (!seen_facts[fact_id]) {
            seen_facts[fact_id] = true;
            novelty = 1;
        }
    }

    compute_novelty_timer.stop();
    return novelty;
}

void NoveltyTable::reset() {
    seen_facts.assign(fact_indexer->get_num_facts(), false);
    if (width == 2) {
        seen_fact_pairs.assign(fact_indexer->get_num_pairs(), false);
    }
}

void NoveltyTable::dump_state_and_novelty(const State &state, int novelty) const {
    string sep;
    cout << state.get_id() << " [";
    for (FactProxy fact_proxy : state) {
        FactPair fact = fact_proxy.get_pair();
        cout << sep << fact.value;
        sep = ", ";
    }
    cout << "]: " << novelty << endl;
}

void NoveltyTable::print_statistics() const {
    utils::g_log << "Time for computing novelty: " << compute_novelty_timer << endl;
}
}
