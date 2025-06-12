#include "utils.h"
#include "projection_generator.h"

#include "explicit_projection_factory.h"
#include "projection.h"

#include "../pdbs/dominance_pruning.h"
#include "../pdbs/pattern_generator.h"
#include "../plugins/options.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"

#include <memory>

using namespace pdbs;
using namespace std;

namespace cost_saturation {
ProjectionGenerator::ProjectionGenerator(
    const shared_ptr<pdbs::PatternCollectionGenerator> &patterns,
    bool dominance_pruning,
    bool combine_labels,
    bool create_complete_transition_system,
    int min_ops_per_label,
    utils::Verbosity verbosity)
    : AbstractionGenerator(verbosity),
      pattern_generator(patterns),
      dominance_pruning(dominance_pruning),
      combine_labels(combine_labels),
      create_complete_transition_system(create_complete_transition_system),
      min_ops_per_label(min_ops_per_label) {
}

Abstractions ProjectionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task,
    DeadEnds *dead_ends) {
    utils::Timer patterns_timer;
    TaskProxy task_proxy(*task);
    num_total_non_label_transitions = 0;
	num_total_reused_label_transitions = 0;
    num_total_label_transitions = 0;

    task_properties::verify_no_axioms(task_proxy);
    if (!create_complete_transition_system &&
        task_properties::has_conditional_effects(task_proxy)) {
        cerr << "Error: configuration doesn't support conditional effects. "
            "Use projections(..., create_complete_transition_system=true) "
            "to build projections that support conditional effects."
             << endl;
        utils::exit_with(utils::ExitCode::SEARCH_UNSUPPORTED);
    }

    log << "Compute patterns" << endl;
    pattern_generator->set_dead_ends_store(dead_ends);
    PatternCollectionInformation pattern_collection_info =
        pattern_generator->generate(task);
    shared_ptr<pdbs::PatternCollection> patterns =
        pattern_collection_info.get_patterns();
    shared_ptr<pdbs::ProjectionCollection> projections =
        pattern_collection_info.get_projections();

    int max_pattern_size = 0;
    for (const pdbs::Pattern &pattern : *patterns) {
        max_pattern_size = max(max_pattern_size, static_cast<int>(pattern.size()));
    }

    log << "Number of patterns: " << patterns->size() << endl;
    log << "Maximum pattern size: " << max_pattern_size << endl;
    log << "Time for computing patterns: " << patterns_timer << endl;

    if (dominance_pruning) {
        shared_ptr<PDBCollection> pdbs = pattern_collection_info.get_pdbs();
        shared_ptr<vector<PatternClique>> pattern_cliques =
            pattern_collection_info.get_pattern_cliques();
        prune_dominated_cliques(
            *patterns,
            *pdbs,
            *pattern_cliques, task_proxy.get_variables().size(),
            numeric_limits<double>::infinity(),
            log);
    }

    log << "Build projections" << endl;
    utils::Timer pdbs_timer;
    shared_ptr<TaskInfo> task_info = make_shared<TaskInfo>(task_proxy);
    Abstractions abstractions;
    task_properties::verify_no_axioms(task_proxy);
    for (const pdbs::Pattern &pattern : *patterns) {
        unique_ptr<Abstraction> projection;
        if (projections) {
            // Projections have already been computed by the generator.
            projection = move((*projections)[abstractions.size()]);
        } else if (create_complete_transition_system) {
            projection = ExplicitProjectionFactory(
                task_proxy, pattern, min_ops_per_label).convert_to_abstraction();

            num_total_non_label_transitions += projection->get_num_non_label_transitions();
            num_total_reused_label_transitions += 
            projection->get_num_label_transitions() - projection->get_num_new_labels();
            num_total_label_transitions += projection->get_num_label_transitions();        
        } else {
            task_properties::verify_no_conditional_effects(task_proxy);
            projection = make_unique<Projection>(
                task_proxy, task_info, pattern, combine_labels);
        }

        if (log.is_at_least_debug()) {
            log << "Pattern " << abstractions.size() + 1 << ": "
                << pattern << endl;
            projection->dump();
        }
        abstractions.push_back(move(projection));
    }
    num_transitions = 0;
    int collection_size = 0;
    for (auto &abstraction : abstractions) {
        collection_size += abstraction->get_num_states();
        abstraction->for_each_transition(
            [this] (const Transition &) { ++num_transitions; });
    }
    log << "Total Number of transitions in Abstractions (before label reduction): " << num_transitions << endl;
    log << "Total number of transitions in Abstractions (after label reduction): " << num_total_non_label_transitions + num_total_label_transitions << endl;
    log << "Total number of single transitions in Abstractions: " << num_total_non_label_transitions << endl;
    // log << "Total number of labels in Abstractions: " << label_id_to_ops.size() << endl;
    // log << "Total number of reused labels in Abstractions: " << num_total_reused_label_transitions << endl;
    // log << "Total change in transitions ((#single transitions+#labels)/#transitions): " << 
	// static_cast<double>(num_total_single_transitions+num_total_label_transitions)/num_transitions << endl;
    // if (!label_id_to_ops.empty()) {
    //     map<int, int> label_size_counts;
    //     map<int, int> reused_label_size_counts;
    //     for (const auto& [label_id, ops] : label_id_to_ops) {
    //         int label_size = ops.size();
    //         label_size_counts[label_size]++;

    //         // Count reuses
    //         if (auto it = reused_label_ids.find(label_id); it != reused_label_ids.end()) {
    //             reused_label_size_counts[label_size] += it->second;
    //         }
    //     }
    //     log << "Label size counts: {";
    //     bool first = true;
    //     for (const auto& [size, count] : label_size_counts) {
    //         if (!first) log << ", ";
    //         log << "\"" << size << "\": " << count;
    //         first = false;
    //     }
    //     log << "}" << std::endl;

    //     log << "Reused label size counts: {";
    //     first = true;
    //     for (const auto& [size, count] : reused_label_size_counts) {
    //         if (!first) log << ", ";
    //         log << "\"" << size << "\": " << count;
    //         first = false;
    //     }
    //     log << "}" << std::endl;
    // }
    log << "Time for building projections: " << pdbs_timer << endl;
    log << "Number of projections: " << abstractions.size() << endl;
    log << "Number of states in projections: " << collection_size << endl;

#ifndef NDEBUG
    uint64_t num_transitions = 0;
    for (auto &abstraction : abstractions) {
        abstraction->for_each_transition(
            [&num_transitions] (const Transition &) {++num_transitions;});
    }
    log << "Number of transitions in projections: " << num_transitions << endl;
#endif

    return abstractions;
}

class ProjectionGeneratorFeature
    : public plugins::TypedFeature<AbstractionGenerator, ProjectionGenerator> {
public:
    ProjectionGeneratorFeature() : TypedFeature("projections") {
        document_title("");
        document_synopsis("Projection generator");
        add_option<shared_ptr<pdbs::PatternCollectionGenerator>>(
            "patterns",
            "pattern generation method",
            plugins::ArgumentInfo::NO_DEFAULT);
        add_option<bool>(
            "dominance_pruning",
            "prune dominated patterns",
            "false");
        add_option<bool>(
            "combine_labels",
            "group labels that only induce parallel transitions",
            "true");
        add_option<bool>(
            "create_complete_transition_system",
            "create explicit transition system",
            "false");
        add_option<int>(
            "min_ops_per_label",
            "minimum number of operators a label must have",
            "infinity",
            plugins::Bounds("0", "infinity"));
        add_abstraction_generator_arguments_to_feature(*this);
    }

    virtual shared_ptr<ProjectionGenerator> create_component(
        const plugins::Options &options) const override {
        return plugins::make_shared_from_arg_tuples<ProjectionGenerator>(
            options.get<shared_ptr<pdbs::PatternCollectionGenerator>>("patterns"),
            options.get<bool>("dominance_pruning"),
            options.get<bool>("combine_labels"),
            options.get<bool>("create_complete_transition_system"),
            options.get<int>("min_ops_per_label"),
            get_abstraction_generator_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<ProjectionGeneratorFeature> _plugin;
}
