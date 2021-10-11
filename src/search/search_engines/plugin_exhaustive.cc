#include "exhaustive_search.h"
#include "../heuristics/blind_search_heuristic.h"
#include "search_common.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace plugin_astar {
static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Exhaustive Search (eager)",
        "Exhaustive exploration of reachable state space. Used to prove properties of heuristics");

    parser.document_language_support("action costs", "supported");
    parser.document_language_support(
        "conditional effects",
        "not supported");
    parser.document_language_support(
        "axioms",
        "not supported (the heuristic supports them in theory, but none of "
        "the currently implemented abstraction generators do)");
    parser.document_property("admissible", "yes");
    parser.document_property("consistent", "yes");
    parser.document_property("safe", "yes");
    parser.document_property("preferred operators", "no");

    parser.add_option<shared_ptr<Evaluator>>("eval", "evaluator for h-value");

    exhaustive_search::add_options_to_parser(parser);
    Options opts = parser.parse();

    shared_ptr<exhaustive_search::ExhaustiveSearch> engine;
    if (!parser.dry_run()) {
        auto temp = search_common::create_astar_open_list_factory_and_f_eval(opts);
        opts.set("open", temp.first);
        opts.set("f_eval", temp.second);
        opts.set("reopen_closed", false);
        vector<shared_ptr<Evaluator>> preferred_list;
        opts.set("preferred", preferred_list);
        engine = make_shared<exhaustive_search::ExhaustiveSearch>(opts);
    }

    return engine;
}


static Plugin<SearchEngine> _plugin("exhaustive", _parse);
}
