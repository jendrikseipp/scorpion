#include "idastar_search.h"

#include "../evaluation_context.h"
#include "../evaluator.h"

#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"

#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/timer.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace idastar_search {
static const int INF = numeric_limits<int>::max();
static const int MEMORY_PADDING_MB = 512;

FifoCache::FifoCache(int max_size)
    : max_size(max_size) {
    if (max_size == INF) {
        utils::reserve_extra_memory_padding(MEMORY_PADDING_MB);
    }
}

void FifoCache::add(const State &state, int g, int iteration) {
    assert(state_to_g_and_iteration.size() == states.size());
    if (max_size == 0) {
        return;
    }
    if (max_size == INF && !utils::extra_memory_padding_is_reserved()) {
        max_size = states.size();
    }
    if (!state_to_g_and_iteration.count(state)) {
        states.push(state);
    }
    state_to_g_and_iteration[state] = make_pair(g, iteration);
    assert(state_to_g_and_iteration.size() == states.size());
    if (static_cast<int>(state_to_g_and_iteration.size()) > max_size) {
        State &oldest_state = states.front();
        state_to_g_and_iteration.erase(oldest_state);
        states.pop();
    }
}

CacheValue FifoCache::lookup(const State &state) const {
    auto iter = state_to_g_and_iteration.find(state);
    if (iter == state_to_g_and_iteration.end()) {
        return make_pair(INF, -1);
    } else {
        return iter->second;
    }
}

void FifoCache::clear() {
    state_to_g_and_iteration.clear();
    queue<State>().swap(states);
}

IDAstarSearch::IDAstarSearch(
    const shared_ptr<Evaluator> &h_evaluator, int initial_f_limit, int cache_size,
    bool single_plan, OperatorCost cost_type, int bound, double max_time,
    const string &description, utils::Verbosity verbosity)
    : SearchAlgorithm(cost_type, bound, max_time, description, verbosity),
      h_evaluator(h_evaluator),
      single_plan(single_plan),
      iteration(0),
      f_limit(initial_f_limit),
      cheapest_plan_cost(numeric_limits<int>::max()),
      num_cache_hits(0),
      num_expansions(0),
      num_evaluations(0) {
    if (h_evaluator->does_cache_estimates()) {
        cerr << "Error: set cache_estimates=false for IDA* heuristics." << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    if (cache_size > 0) {
        cache = utils::make_unique_ptr<FifoCache>(cache_size);
    }
}

void IDAstarSearch::initialize() {
    cout << "Conducting IDA* search, (real) bound = " << bound << endl;
}

void IDAstarSearch::print_statistics() const {
    cout << "Expansions: " << num_expansions << endl;
    cout << "Evaluations: " << num_evaluations << endl;
    cout << "IDA* cache hits: " << num_cache_hits << endl;
    cout << "IDA* iterations: " << iteration << endl;
}

int IDAstarSearch::compute_h_value(const State &state) const {
    EvaluationContext eval_context(state);
    return eval_context.get_evaluator_value_or_infinity(h_evaluator.get());
}

int IDAstarSearch::recursive_search(const IDAstarNode &node) {
    int f = node.g + node.h;
    if (f > f_limit) {
        return f;
    }
    if (task_properties::is_goal_state(task_proxy, node.state)) {
        int plan_cost = calculate_plan_cost(operator_sequence, task_proxy);
        cout << "Found solution with cost " << plan_cost << endl;
        if (plan_cost < cheapest_plan_cost) {
            plan_manager.save_plan(operator_sequence, task_proxy, !single_plan);
            cheapest_plan_cost = plan_cost;
            set_plan(operator_sequence);
            f_limit = plan_cost - 1;
        }
        return -1;
    }

    ++num_expansions;
    int next_limit = INF;
    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(node.state, applicable_ops);
    OperatorsProxy operators = task_proxy.get_operators();
    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = operators[op_id];
        State succ_state = node.state.get_unregistered_successor(op);
        int succ_g = node.g + get_adjusted_cost(op);
        if (cache) {
            CacheValue pair = cache->lookup(succ_state);
            int old_succ_g = pair.first;
            int old_iteration = pair.second;
            if (succ_g > old_succ_g || (succ_g == old_succ_g && iteration == old_iteration)) {
                ++num_cache_hits;
                continue;
            } else {
                cache->add(succ_state, succ_g, iteration);
            }
        }
        int succ_h = compute_h_value(succ_state);
        ++num_evaluations;
        if (succ_h != INF) {
            operator_sequence.push_back(op_id);
            IDAstarNode succ_node(move(succ_state), succ_g, succ_h);
            int rec_limit = recursive_search(succ_node);
            if (found_solution() && single_plan) {
                return -1;
            }
            operator_sequence.pop_back();
            next_limit = min(next_limit, rec_limit);
        }
    }
    return next_limit;
}

SearchStatus IDAstarSearch::step() {
    cout << "IDA* search start time: " << utils::g_timer() << endl;
    State initial_state = task_proxy.get_initial_state();
    int init_h = compute_h_value(initial_state);
    utils::g_log << "Initial h value: " << init_h << endl;
    IDAstarNode node(initial_state, 0, init_h);
    while (f_limit != INF && f_limit != -1 && (!single_plan || !found_solution())) {
        utils::g_log << "f limit: " << f_limit << endl;
        ++iteration;
        f_limit = recursive_search(node);
    }
    if (utils::extra_memory_padding_is_reserved()) {
        utils::release_extra_memory_padding();
    }
    if (found_solution()) {
        return SOLVED;
    }
    return FAILED;
}

void IDAstarSearch::save_plan_if_necessary() {
    // We don't need to save here, as we automatically save plans when we find them.
}

class IDAstarSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, idastar_search::IDAstarSearch> {
public:
    IDAstarSearchFeature() : TypedFeature("idastar") {
        document_title("IDA* search");
        document_synopsis("IDA* search with an optional g-value cache.");
        add_option<shared_ptr<Evaluator>>(
            "eval",
            "evaluator for h-value. Make sure to use cache_estimates=false.");
        add_option<int>(
            "initial_f_limit",
            "initial depth limit",
            "0",
            plugins::Bounds("0", "infinity"));
        add_option<int>(
            "cache_size",
            "maximum number of states to cache. For cache_size=infinity the cache "
            "fills up until approaching the memory limit, at which point the "
            "current number of states becomes the maximum cache size.",
            "0",
            plugins::Bounds("0", "infinity"));
        add_option<bool>(
            "single_plan",
            "stop after finding the first plan",
            "true");
        add_search_algorithm_options_to_feature(*this, "idastar");
    }

    virtual shared_ptr<IDAstarSearch> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<IDAstarSearch>(
            options.get<shared_ptr<Evaluator>>("eval"),
            options.get<int>("initial_f_limit"),
            options.get<int>("cache_size"),
            options.get<bool>("single_plan"),
            get_search_algorithm_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<IDAstarSearchFeature> _plugin;
}
