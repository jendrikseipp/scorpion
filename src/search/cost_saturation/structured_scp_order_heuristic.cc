#include "structured_scp_order_heuristic.h"

#include "structured_scp_order_generator_full.h"
#include "types.h"
#include "utils.h"

#include "../plugins/plugin.h"

using namespace std;

namespace cost_saturation {
StructuredSCPOrderHeuristic::StructuredSCPOrderHeuristic(
    const shared_ptr<AbstractTask> &transform, bool cache_estimates,
    const string &description, utils::Verbosity verbosity,
    AbstractionFunctions &&abs_functions,
    vector<UnsolvabilityInfo> &&unsolvability_infos,
    Instructions &&instructions, vector<vector<vector<int>>> &&lookup_tables)
    : Heuristic(transform, cache_estimates, description, verbosity),
      abs_functions(move(abs_functions)),
      unsolvability_infos(move(unsolvability_infos)),
      instructions(move(instructions)),
      lookup_tables(move(lookup_tables)) {
    size_t num_lookups = 0;
    for (const vector<vector<int>> &lookup_table_of_abstraction :
         this->lookup_tables) {
        if (!lookup_table_of_abstraction.empty()) {
            num_lookups += lookup_table_of_abstraction[0].size();
        }
    }
    assert(this->abs_functions.size() == this->unsolvability_infos.size());
    if (this->lookup_tables.empty()) {
        // In case of an empty DAG, we return 0 as heuristic value.
        values = {0};
    } else {
        values = vector<int>(num_lookups + this->instructions.size(), -1);
    }

    cout << "Initializing structured SCP order heuristic with " << num_lookups
         << " values to look up (in " << this->lookup_tables.size()
         << " abstractions)" << endl;
    cout << "Initializing structured SCP order heuristic with "
         << this->instructions.size() << " compositional instructions." << endl;
}

int StructuredSCPOrderHeuristic::compute_heuristic(
    const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    vector<int> abstract_state_ids =
        get_abstract_state_ids(abs_functions, state);
    for (size_t abstr_id = 0; abstr_id < unsolvability_infos.size();
         ++abstr_id) {
        if (unsolvability_infos[abstr_id]
                .unsolvable_states[abstract_state_ids[abstr_id]]) {
            return DEAD_END;
        }
    }

    // Perform all lookups.
    size_t value_id = 0;
    for (size_t abstr_id = 0; abstr_id < lookup_tables.size(); ++abstr_id) {
        if (lookup_tables[abstr_id].empty()) {
            continue;
        }
        const vector<int> &lookup_table =
            lookup_tables[abstr_id][abstract_state_ids[abstr_id]];
        for (int value : lookup_table) {
            if (value == INF) {
                return DEAD_END;
            }
            assert(value_id < values.size());
            values[value_id] = value;
            ++value_id;
        }
    }

    // Process instructions.
    int num_instructions = instructions.size();
    for (int i = 0; i < num_instructions; ++i) {
        assert(static_cast<size_t>(value_id) < values.size());
        int begin = instructions.id_offsets[i];
        int end = instructions.id_offsets[i + 1];
        assert(begin < end);
        if (instructions.types[i] == InstructionType::MAX) {
            int result = -INF;
            for (int j = begin; j < end; ++j) {
                result = max(result, values[instructions.ids[j]]);
            }
            values[value_id] = result;
        } else {
            assert(instructions.types[i] == InstructionType::SUM);
            int result = 0;
            for (int j = begin; j < end; ++j) {
                result += values[instructions.ids[j]];
            }
            values[value_id] = result;
        }
        ++value_id;
    }
    return values.back();
}

class StructuredSCPOrderHeuristicFeature
    : public plugins::TypedFeature<Evaluator, StructuredSCPOrderHeuristic> {
public:
    StructuredSCPOrderHeuristicFeature() : TypedFeature("sscp") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Structured SCP heuristic");
        document_synopsis(
            "Compute a DAG that compactly represents the maximum over the "
            "saturated cost partitioning heuristics for all orders of the "
            "given abstractions and evaluate it for each state (Höft et al., "
            "KR 2025).");
        add_heuristic_options_to_feature(*this, "sscp");
        add_option<shared_ptr<StructuredSCPOrderGenerator>>(
            "structured_order_generator", "a structured order generator",
            "structured_order_generator_full()");
    }

    virtual shared_ptr<StructuredSCPOrderHeuristic> create_component(
        const plugins::Options &options) const override {
        shared_ptr<StructuredSCPOrderGenerator> generator =
            options.get<shared_ptr<StructuredSCPOrderGenerator>>(
                "structured_order_generator");
        StructuredSCPOrder structured_order = generator->generate();
        return plugins::make_shared_from_arg_tuples<
            StructuredSCPOrderHeuristic>(
            get_heuristic_arguments_from_options(options),
            move(structured_order.abs_functions),
            move(structured_order.unsolvability_infos),
            move(structured_order.instructions),
            move(structured_order.lookup_tables));
    }
};

static plugins::FeaturePlugin<StructuredSCPOrderHeuristicFeature> _plugin;
}
