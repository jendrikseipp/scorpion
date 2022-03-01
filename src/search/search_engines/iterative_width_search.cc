#include "iterative_width_search.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"

#include <cassert>
#include <cstdlib>
#include <optional.hh>

using namespace std;

namespace novelty_search {
ShortFact::ShortFact()
    : var(numeric_limits<uint16_t>::max()),
      value(numeric_limits<uint16_t>::max()) {
}

ShortFact::ShortFact(int var, int value) :
    var(var),
    value(value) {
    if (var > RANGE || value > RANGE) {
        cerr << "Fact doesn't fit into ShortFact." << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}


Conjunction::Conjunction()
    : size_(0) {
}

void Conjunction::push_back(FactPair fact) {
    assert(size_ < capacity());
    facts[size_] = ShortFact(fact.var, fact.value);
    ++size_;
}

bool Conjunction::operator==(const Conjunction &other) const {
    return size_ == other.size_ && facts == other.facts;
}

FactPair Conjunction::operator[](int index) const {
    assert(index >= 0 && index < capacity());
    return FactPair(facts[index].var, facts[index].value);
}

int Conjunction::get_watched_index() const {
    return watched_index;
}

void Conjunction::set_watched_index(int index) {
    assert(index >= 0 && index < capacity());
    watched_index = index;
}

int Conjunction::size() const {
    return size_;
}

int Conjunction::capacity() const {
    return facts.size();
}

ConjunctionArray::const_iterator Conjunction::begin() const {
    return facts.begin();
}

ConjunctionArray::const_iterator Conjunction::end() const {
    return begin() + size_;
}

void feed(utils::HashState &hash_state, const Conjunction &conjunction) {
    feed(hash_state, static_cast<uint64_t>(conjunction.size()));
    for (auto &fact : conjunction) {
        feed(hash_state, fact.var);
        feed(hash_state, fact.value);
    }
}


NoveltySearch::NoveltySearch(const Options &opts)
    : SearchEngine(opts),
      width(opts.get<int>("width")),
      condition_width(opts.get<int>("condition_width")),
      debug(opts.get<utils::Verbosity>("verbosity") == utils::Verbosity::DEBUG),
      rng(utils::parse_rng_from_options(opts)),
      compute_novelty_timer(false) {
    if (width > condition_width) {
        cerr << "width must be <= condition_width" << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    utils::g_log << "Setting up novelty search." << endl;
    utils::g_log << "Conjunction size: " << sizeof(Conjunction) << endl;
    utils::Timer setup_timer;

    fact_id_offsets.reserve(task_proxy.get_variables().size());
    int num_facts = 0;
    for (VariableProxy var : task_proxy.get_variables()) {
        fact_id_offsets.push_back(num_facts);
        int domain_size = var.get_domain_size();
        num_facts += domain_size;
    }
    utils::g_log << "Facts: " << num_facts << endl;

    if (debug && width <= 2) {
        seen_facts.resize(num_facts, false);
        seen_fact_pairs.resize(num_facts);
        for (int fact_id = 0; fact_id < num_facts; ++fact_id) {
            // Note that we could store only a triangle instead of the full square.
            seen_fact_pairs[fact_id].resize(num_facts, false);
        }
    }

    fact_watchers.resize(num_facts);

    watch_all_conjunctions();

    if (condition_width > width) {
        ConjunctionSet subsets = get_precondition_subsets();
        for (const Conjunction &conjunction : subsets) {
            int index = 0;
            int fact_id = get_fact_id(conjunction[index]);
            fact_watchers[fact_id].push_back(conjunction);
            fact_watchers[fact_id].back().set_watched_index(index);
        }
    }

    int num_conjunctions = 0;
    for (auto &watchers : fact_watchers) {
        num_conjunctions += watchers.size();
    }
    utils::g_log << "Conjunctions: " << num_conjunctions << endl;
    utils::g_log << "Time for setting up novelty search: " << setup_timer << endl;
}

bool NoveltySearch::is_novel(const State &state) {
    int num_vars = fact_id_offsets.size();
    // Unpack state to make accesses faster.
    state.unpack();
    const vector<int> &state_values = state.get_unpacked_values();
    bool novel = false;
    for (int var = 0; var < num_vars; ++var) {
        int fact_id = get_fact_id(var, state_values[var]);
        for (int i = static_cast<int>(fact_watchers[fact_id].size()) - 1; i >= 0; --i) {
            Conjunction &conjunction = fact_watchers[fact_id][i];
            int n = conjunction.size();
            bool conjunction_holds = true;
            // We know that the watched fact holds, now check the other n-1 facts.
            for (int j = 0; j < n - 1; ++j) {
                int k = conjunction.get_watched_index() + 1 + j;
                // Avoid slow modulo if possible.
                k = (k >= n ? k % n : k);
                FactPair conj_fact = conjunction[k];
                if (state_values[conj_fact.var] != conj_fact.value) {
                    // Watch new fact for conjunction.
                    conjunction.set_watched_index(k);
                    fact_watchers[get_fact_id(conj_fact)].push_back(
                        move(fact_watchers[fact_id][i]));
                    utils::swap_and_pop_from_vector(fact_watchers[fact_id], i);
                    conjunction_holds = false;
                    break;
                }
            }
            if (conjunction_holds) {
                if (debug) {
                    dump_conjunction("visit", conjunction);
                }
                novel = true;
                utils::swap_and_pop_from_vector(fact_watchers[fact_id], i);
            }
        }
    }
    return novel;
}

void NoveltySearch::initialize() {
    utils::g_log << "Starting novelty search." << endl;
    State initial_state = state_registry.get_initial_state();
    if (debug) {
        cout << "generate state: ";
        task_properties::dump_fdr(initial_state);
    }
    statistics.inc_generated();
    SearchNode node = search_space.get_node(initial_state);
    node.open_initial();
    open_list.push_back(initial_state.get_id());
    is_novel(initial_state);
}

static void cartesian_product(
    const vector<int> &v, const function<void(const vector<int> &)> &callback) {
    long long N = 1;
    for (int domain_size : v) {
        N *= domain_size;
    }
    vector<int> u(v.size());
    for (long long n = 0; n < N; ++n) {
        lldiv_t q {n, 0};
        for (long long i = v.size() - 1; 0 <= i; --i) {
            q = div(q.quot, static_cast<size_t>(v[i]));
            u[i] = q.rem;
        }
        callback(u);
    }
}

void NoveltySearch::watch_all_conjunctions(int k) {
    int n = task_proxy.get_variables().size();
    assert(k <= n);

    vector<bool> bitmask(k, true);  // k leading 1's
    bitmask.resize(n, false);  // n-k trailing 0's

    vector<int> variables;
    vector<int> domain_sizes;
    do {
        variables.clear();
        domain_sizes.clear();
        for (int i = 0; i < n; ++i) {
            if (bitmask[i]) {
                variables.push_back(i);
            }
        }
        for (int var : variables) {
            domain_sizes.push_back(task_proxy.get_variables()[var].get_domain_size());
        }
        cartesian_product(domain_sizes, [&](const vector<int> &values) {
                              Conjunction conjunction;
                              for (size_t i = 0; i < values.size(); ++i) {
                                  int var = variables[i];
                                  int value = values[i];
                                  conjunction.push_back(FactPair(var, value));
                              }
                              int index = 0;
                              int fact_id = get_fact_id(conjunction[index]);
                              conjunction.set_watched_index(index);
                              fact_watchers[fact_id].push_back(move(conjunction));
                          });
    } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
}

void NoveltySearch::watch_all_conjunctions() {
    int num_vars = task_proxy.get_variables().size();
    int n = min(width, num_vars);
    for (int k = 1; k <= n; ++k) {
        watch_all_conjunctions(k);
    }
}

void NoveltySearch::add_subsets(
    const vector<FactPair> &facts, int k, ConjunctionSet &conjunctions) const {
    int n = facts.size();
    assert(k <= n);
    vector<bool> bitmask(k, true);  // k leading 1's
    bitmask.resize(n, false);  // n-k trailing 0's

    do {
        Conjunction subset;
        for (int i = 0; i < n; ++i) {
            if (bitmask[i]) {
                subset.push_back(facts[i]);
            }
        }
        if (debug) {
            dump_conjunction("subset", subset);
        }
        conjunctions.insert(move(subset));
    } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
}

ConjunctionSet NoveltySearch::get_precondition_subsets() const {
    vector<vector<FactPair>> preconditions_and_goal;
    preconditions_and_goal.reserve(task_proxy.get_operators().size() + 1);
    for (OperatorProxy op : task_proxy.get_operators()) {
        if (static_cast<int>(op.get_preconditions().size()) > width) {
            preconditions_and_goal.push_back(
                task_properties::get_fact_pairs(op.get_preconditions()));
        }
    }
    preconditions_and_goal.push_back(task_properties::get_fact_pairs(task_proxy.get_goals()));

    int max_condition_size = 0;
    ConjunctionSet subsets;
    for (const vector<FactPair> &condition : preconditions_and_goal) {
        if (debug) {
            cout << "condition: " << condition << endl;
        }
        int size = condition.size();
        max_condition_size = max(max_condition_size, size);
        int n = min(condition_width, size);
        for (int k = width + 1; k <= n; ++k) {
            add_subsets(condition, k, subsets);
        }
    }
    utils::g_log << "Max condition size: " << max_condition_size << endl;
    return subsets;
}

bool NoveltySearch::visit_fact_pair(int fact_id1, int fact_id2) {
    if (fact_id1 > fact_id2) {
        swap(fact_id1, fact_id2);
    }
    assert(fact_id1 < fact_id2);
    bool novel = !seen_fact_pairs[fact_id1][fact_id2];
    seen_fact_pairs[fact_id1][fact_id2] = true;
    return novel;
}

bool NoveltySearch::is_novel(OperatorID op_id, const State &state) {
    if (debug) {
        cout << "generate state: ";
        task_properties::dump_fdr(state);
    }
    int num_vars = fact_id_offsets.size();
    bool simple_novel = false;
    utils::unused_variable(simple_novel);
    if (debug && width <= 2) {
        for (EffectProxy effect : task_proxy.get_operators()[op_id].get_effects()) {
            FactPair fact = effect.get_fact().get_pair();
            int fact_id = get_fact_id(fact);
            if (!seen_facts[fact_id]) {
                seen_facts[fact_id] = true;
                simple_novel = true;
            }
        }
        for (EffectProxy effect : task_proxy.get_operators()[op_id].get_effects()) {
            FactPair fact1 = effect.get_fact().get_pair();
            int fact_id1 = get_fact_id(fact1);
            for (int var2 = 0; var2 < num_vars; ++var2) {
                if (fact1.var == var2) {
                    continue;
                }
                FactPair fact2 = state[var2].get_pair();
                int fact_id2 = get_fact_id(fact2);
                if (visit_fact_pair(fact_id1, fact_id2)) {
                    simple_novel = true;
                }
            }
        }
    }

    if (debug) {
        for (size_t fact_id = 0; fact_id < fact_watchers.size(); ++fact_id) {
            const ConjunctionList &conjunctions = fact_watchers[fact_id];
            cout << "fact " << fact_id << ": " << conjunctions << endl;
        }
    }

    return is_novel(state);
}

void NoveltySearch::print_statistics() const {
    utils::g_log << "Time for computing novelty: " << compute_novelty_timer << endl;
    statistics.print_detailed_statistics();
    search_space.print_statistics();
}

SearchStatus NoveltySearch::step() {
    tl::optional<SearchNode> node;
    while (true) {
        if (open_list.empty()) {
            utils::g_log << "Completely explored state space -- no solution!" << endl;
            return FAILED;
        }
        StateID id = open_list.front();
        open_list.pop_front();
        State s = state_registry.lookup_state(id);
        node.emplace(search_space.get_node(s));

        if (node->is_closed())
            continue;

        node->close();
        assert(!node->is_dead_end());
        statistics.inc_expanded();
        break;
    }

    State s = node->get_state();
    if (check_goal_and_set_plan(s))
        return SOLVED;

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(s, applicable_ops);
    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        if ((node->get_real_g() + op.get_cost()) >= bound)
            continue;

        State succ_state = state_registry.get_successor_state(s, op);

        compute_novelty_timer.resume();
        bool novel = is_novel(op_id, succ_state);
        compute_novelty_timer.stop();

        if (!novel) {
            continue;
        }

        statistics.inc_generated();
        SearchNode succ_node = search_space.get_node(succ_state);
        if (succ_node.is_new()) {
            succ_node.open(*node, op, get_adjusted_cost(op));
            open_list.push_back(succ_state.get_id());
        }
    }

    return IN_PROGRESS;
}

void NoveltySearch::dump_conjunction(
    const string &name, const Conjunction &conjunction) const {
    if (!name.empty()) {
        cout << name << ": ";
    }
    cout << conjunction << endl;
}

void NoveltySearch::dump_search_space() const {
    search_space.dump(task_proxy);
}

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Iterated width search", "");

    parser.add_option<int>(
        "width", "maximum conjunction size", "2", Bounds("1", "8"));
    parser.add_option<int>(
        "condition_width", "maximum size of condition subset", "8", Bounds("1", "8"));
    utils::add_rng_options(parser);
    SearchEngine::add_options_to_parser(parser);

    Options opts = parser.parse();

    if (parser.dry_run()) {
        return nullptr;
    }

    return make_shared<novelty_search::NoveltySearch>(opts);
}

static Plugin<SearchEngine> _plugin("iw", _parse);
}
