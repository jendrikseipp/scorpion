#include "normalize/normalize.h"
#include "parser/lisp_parser.h"
#include "parser/parser.h"
#include "pipeline/translate.h"
#include "translate_options.h"
#include "utils/logging.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <string>

using namespace translate;

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
    try {
        parse_options(argc, argv);
        const Options &opts = get_options();

        utils::log() << "Fast Downward translator (C++ port)" << std::endl;
        utils::log() << "Parsing..." << std::endl;
        auto domain_sexpr = parser::parse_pddl_file("domain", opts.domain);
        auto task_sexpr = parser::parse_pddl_file("task", opts.task);
        auto task = parser::parse_task(domain_sexpr, task_sexpr);

        utils::log() << "Normalizing task..." << std::endl;
        normalize::normalize(task);

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

        utils::log() << "Translating to SAS+..." << std::endl;
        auto sas_task = pipeline::pddl_to_sas(task);
        dump_statistics(sas_task);

        utils::log() << "Writing output..." << std::endl;
        std::ofstream out(opts.sas_file);
        if (!out)
            utils::exit_with(utils::ExitCode::TRANSLATE_CRITICAL_ERROR,
                             "Could not open output file: " + opts.sas_file);
        sas_task.output(out);
        utils::log() << "Done! " << utils::elapsed_seconds() << "s"
                     << std::endl;
        return 0;
    } catch (const parser::ParseError &e) {
        std::cerr << "Parse error:\n" << e.what() << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_INPUT_ERROR);
    } catch (const utils::ExitException &e) {
        return static_cast<int>(e.get_exit_code());
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_CRITICAL_ERROR);
    }
}
