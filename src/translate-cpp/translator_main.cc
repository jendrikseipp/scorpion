#include "grounding/build.h"
#include "grounding/model.h"
#include "grounding/program.h"
#include "grounding/split.h"
#include "instantiate/instantiate.h"
#include "normalize/normalize.h"
#include "parser/lisp_parser.h"
#include "parser/parser.h"
#include "translate_options.h"
#include "utils/logging.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <exception>
#include <iostream>
#include <string>

using namespace translate;

int main(int argc, const char **argv) {
    try {
        parse_options(argc, argv);
        const Options &opts = get_options();

        utils::log() << "Fast Downward translator (C++ port)" << std::endl;
        utils::log() << "Domain file:  " << opts.domain << std::endl;
        utils::log() << "Problem file: " << opts.task << std::endl;

        utils::log() << "Parsing domain..." << std::endl;
        auto domain_sexpr = parser::parse_pddl_file("domain", opts.domain);
        utils::log() << "Parsing problem..." << std::endl;
        auto task_sexpr = parser::parse_pddl_file("task", opts.task);
        utils::log() << "Building task..." << std::endl;
        auto task = parser::parse_task(domain_sexpr, task_sexpr);

        utils::log() << "Parsed task '" << task.task_name << "' from domain '"
                     << task.domain_name << "':" << std::endl;
        utils::log() << "  types:      " << task.types.size() << std::endl;
        utils::log() << "  objects:    " << task.objects.size() << std::endl;
        utils::log() << "  predicates: " << task.predicates.size() << std::endl;
        utils::log() << "  functions:  " << task.functions.size() << std::endl;
        utils::log() << "  init:       " << task.init.size() << std::endl;
        utils::log() << "  actions:    " << task.actions.size() << std::endl;
        utils::log() << "  axioms:     " << task.axioms.size() << std::endl;

        utils::log() << "Normalizing task..." << std::endl;
        normalize::normalize(task);
        utils::log() << "After normalization:" << std::endl;
        utils::log() << "  actions:    " << task.actions.size() << std::endl;
        utils::log() << "  axioms:     " << task.axioms.size() << std::endl;

        if (opts.dump_task)
            task.dump(utils::log());

        auto prog = grounding::build_program(task);
        utils::log() << "Datalog program:" << std::endl;
        utils::log() << "  facts: " << prog.facts.size() << std::endl;
        utils::log() << "  rules: " << prog.rules.size() << std::endl;
        grounding::split_rules(prog);
        utils::log() << "After rule splitting: " << prog.rules.size()
                     << " rules" << std::endl;
        auto model = grounding::compute_model(prog);
        utils::log() << "Model size: " << model.size() << " atoms"
                     << std::endl;

        utils::log() << "Instantiating..." << std::endl;
        auto inst = instantiate::instantiate(task, model);
        utils::log() << "  relaxed_reachable: "
                     << (inst.relaxed_reachable ? "yes" : "no") << std::endl;
        utils::log() << "  fluent_facts: " << inst.fluent_facts.size()
                     << std::endl;
        utils::log() << "  instantiated_actions: "
                     << inst.instantiated_actions.size() << std::endl;
        utils::log() << "  instantiated_axioms: "
                     << inst.instantiated_axioms.size() << std::endl;
        utils::log() << "  goal: "
                     << (inst.instantiated_goal ?
                         std::to_string(inst.instantiated_goal->size()) +
                         " literals" : "impossible")
                     << std::endl;

        utils::log() << "Translator pipeline beyond instantiation "
                     << "is not yet implemented." << std::endl;
        utils::log() << "Total time: " << utils::elapsed_seconds() << "s"
                     << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_UNSUPPORTED);
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
