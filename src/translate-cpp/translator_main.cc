#include "normalize/normalize.h"
#include "parser/lisp_parser.h"
#include "parser/parser.h"
#include "pipeline/translate.h"
#include "translate_options.h"
#include "utils/logging.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

using namespace translate;

namespace {
/*
  Match the Python translator's signal/error handling so the driver
  reports the canonical translator exit codes (see
  src/translate/__main__.py and driver/returncodes.py) rather than
  shell-mangled signal-killed codes like 232 (= 256 + (-24) for a
  process killed by SIGXCPU).
*/
extern "C" void handle_sigxcpu(int) {
    static const char msg[] = "\nTranslator hit the time limit\n";
    // async-signal-safe path: write() + _exit() are; printf/exit are not.
    ssize_t r = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)r;
    _exit(static_cast<int>(utils::ExitCode::TRANSLATE_OUT_OF_TIME));
}

void handle_bad_alloc() {
    std::cerr << "\nTranslator ran out of memory" << std::endl;
    std::_Exit(static_cast<int>(utils::ExitCode::TRANSLATE_OUT_OF_MEMORY));
}

void install_signal_and_error_handlers() {
    // SIGXCPU: driver/limits.py setrlimit(RLIMIT_CPU, ...). Default
    // action would terminate the process and the driver would report
    // a negative returncode (visible as 232 in shell wrappers).
    struct sigaction sa{};
    sa.sa_handler = handle_sigxcpu;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGXCPU, &sa, nullptr);
    // std::bad_alloc: matches Python's MemoryError -> exit(20).
    std::set_new_handler(handle_bad_alloc);
}
}

namespace {
void dump_statistics(const sas::SASTask &task) {
    int derived = 0;
    for (int l : task.variables.axiom_layers) if (l >= 0) ++derived;
    int facts = 0;
    for (int r : task.variables.ranges) facts += r;
    int mutex_total = 0;
    for (const auto &m : task.mutexes) mutex_total += static_cast<int>(m.facts.size());
    int task_size = task.variables.get_encoding_size();
    for (const auto &m : task.mutexes) task_size += static_cast<int>(m.facts.size());
    task_size += static_cast<int>(task.goal.pairs.size());
    for (const auto &op : task.operators) {
        task_size += 1 + static_cast<int>(op.prevail.size());
        for (const auto &[v, pre, post, cond] : op.pre_post) {
            task_size += 1 + static_cast<int>(cond.size());
            if (pre != -1) ++task_size;
        }
    }
    for (const auto &ax : task.axioms)
        task_size += 1 + static_cast<int>(ax.condition.size());

    utils::log() << "Translator variables: " << task.variables.ranges.size()
                 << std::endl;
    utils::log() << "Translator derived variables: " << derived << std::endl;
    utils::log() << "Translator facts: " << facts << std::endl;
    utils::log() << "Translator goal facts: " << task.goal.pairs.size()
                 << std::endl;
    utils::log() << "Translator mutex groups: " << task.mutexes.size()
                 << std::endl;
    utils::log() << "Translator total mutex groups size: " << mutex_total
                 << std::endl;
    utils::log() << "Translator operators: " << task.operators.size()
                 << std::endl;
    utils::log() << "Translator axioms: " << task.axioms.size() << std::endl;
    utils::log() << "Translator task size: " << task_size << std::endl;
}
}

int main(int argc, const char **argv) {
    install_signal_and_error_handlers();
    try {
        parse_options(argc, argv);
        const Options &opts = get_options();

        utils::log() << "Fast Downward translator (C++ port)" << std::endl;
        // Phase log lines use the Python translator's wording and
        // "[%.3fs CPU, %.3fs wall-clock]" format so Lab's stock
        // translator parser captures them as translator_time_<phase>.
        utils::log() << "Parsing..." << std::endl;
        utils::PhaseTimer parse_t;
        auto domain_sexpr = parser::parse_pddl_file("domain", opts.domain);
        auto task_sexpr = parser::parse_pddl_file("task", opts.task);
        auto task = parser::parse_task(domain_sexpr, task_sexpr);
        utils::log() << "Parsing: " << parse_t.str() << std::endl;

        utils::log() << "Normalizing task..." << std::endl;
        utils::PhaseTimer normalize_t;
        normalize::normalize(task);
        utils::log() << "Normalizing task: " << normalize_t.str() << std::endl;

        if (opts.generate_relaxed_task) {
            for (auto &action : task.actions) {
                auto &effs = action.effects;
                effs.erase(std::remove_if(effs.begin(), effs.end(),
                                          [](const pddl::Effect &e) {
                                              if (!e.literal) return false;
                                              const auto &lit =
                                                  static_cast<const pddl::Literal &>(
                                                      *e.literal);
                                              return lit.negated();
                                          }),
                           effs.end());
            }
        }
        if (opts.dump_task) task.dump(utils::log());

        auto sas_task = pipeline::pddl_to_sas(task);
        dump_statistics(sas_task);

        utils::log() << "Writing output..." << std::endl;
        utils::PhaseTimer write_t;
        std::ofstream out(opts.sas_file);
        if (!out)
            utils::exit_with(utils::ExitCode::TRANSLATE_CRITICAL_ERROR,
                             "Could not open output file: " + opts.sas_file);
        sas_task.output(out);
        utils::log() << "Writing output: " << write_t.str() << std::endl;

        struct rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        utils::log() << "Translator peak memory: " << ru.ru_maxrss << " KB"
                     << std::endl;
        utils::log() << "Done! "
                     << utils::format_timing(utils::cpu_seconds(),
                                             utils::elapsed_seconds())
                     << std::endl;
        return 0;
    } catch (const parser::ParseError &e) {
        std::cerr << "Parse error:\n" << e.what() << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_INPUT_ERROR);
    } catch (const utils::ExitException &e) {
        return static_cast<int>(e.get_exit_code());
    } catch (const std::bad_alloc &) {
        std::cerr << "\nTranslator ran out of memory" << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_OUT_OF_MEMORY);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_CRITICAL_ERROR);
    }
}
