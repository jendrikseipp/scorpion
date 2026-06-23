#include "translate_options.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std;
namespace translate {
namespace {
void usage(ostream &os, const char *prog) {
    os << "usage: " << prog
       << " [--relaxed] [--full-encoding]\n"
       << "    [--invariant-generation-max-candidates N]\n"
       << "    [--sas-file FILE] [--invariant-generation-max-time SECS]\n"
       << "    [--add-implied-preconditions] [--keep-unreachable-facts]\n"
       << "    [--skip-variable-reordering] [--keep-unimportant-variables]\n"
       << "    [--keep-no-ops] [--no-cpython-rng] [--dump-task]\n"
       << "    [--layer-strategy {min,max}]\n"
       << "    DOMAIN_PDDL TASK_PDDL\n";
}

[[noreturn]] void die(const char *prog, const string &msg) {
    cerr << prog << ": " << msg << "\n";
    usage(cerr, prog);
    throw runtime_error(msg);
}

int parse_int(const char *prog, string_view s, const char *flag) {
    try {
        return stoi(string(s));
    } catch (const exception &) {
        die(prog, string(flag) + " requires an integer argument");
    }
}
}

Options &get_options() {
    static Options options;
    return options;
}

void parse_options(int argc, const char *const *argv) {
    Options &o = get_options();
    const char *prog = argc > 0 ? argv[0] : "translate";
    vector<string> positionals;
    for (int i = 1; i < argc; ++i) {
        string_view a = argv[i];
        auto next = [&]() -> const char * {
            if (++i >= argc)
                die(prog, string(a) + " requires an argument");
            return argv[i];
        };
        if (a == "--relaxed") {
            o.generate_relaxed_task = true;
        } else if (a == "--full-encoding") {
            o.use_partial_encoding = false;
        } else if (a == "--invariant-generation-max-candidates") {
            o.invariant_generation_max_candidates =
                parse_int(prog, next(), "--invariant-generation-max-candidates");
        } else if (a == "--sas-file") {
            o.sas_file = next();
        } else if (a == "--invariant-generation-max-time") {
            o.invariant_generation_max_time =
                parse_int(prog, next(), "--invariant-generation-max-time");
        } else if (a == "--add-implied-preconditions") {
            o.add_implied_preconditions = true;
        } else if (a == "--keep-unreachable-facts") {
            o.filter_unreachable_facts = false;
        } else if (a == "--skip-variable-reordering") {
            o.reorder_variables = false;
        } else if (a == "--keep-unimportant-variables") {
            o.filter_unimportant_vars = false;
        } else if (a == "--keep-no-ops") {
            o.keep_no_ops = true;
        } else if (a == "--no-cpython-rng") {
            o.cpython_rng = false;
        } else if (a == "--dump-task") {
            o.dump_task = true;
        } else if (a == "--layer-strategy") {
            string v = next();
            if (v != "min" && v != "max")
                die(prog, "--layer-strategy must be 'min' or 'max'");
            o.layer_strategy = move(v);
        } else if (a == "--help" || a == "-h") {
            usage(cout, prog);
            exit(0);
        } else if (!a.empty() && a[0] == '-') {
            die(prog, "unknown option " + string(a));
        } else {
            positionals.emplace_back(a);
        }
    }
    if (positionals.size() != 2)
        die(prog, "expected exactly two positional arguments "
                  "(domain and task)");
    o.domain = positionals[0];
    o.task = positionals[1];
}
}
