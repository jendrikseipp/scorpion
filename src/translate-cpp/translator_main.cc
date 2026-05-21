#include "utils/logging.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <iostream>
#include <string>

using namespace translate;

int main(int argc, const char **argv) {
    try {
        if (argc < 3) {
            utils::log() << "usage: " << argv[0]
                         << " <domain.pddl> <problem.pddl> [options]"
                         << std::endl;
            utils::exit_with(utils::ExitCode::TRANSLATE_INPUT_ERROR);
        }

        utils::log() << "Fast Downward translator (C++ port)" << std::endl;
        utils::log() << "Domain file:  " << argv[1] << std::endl;
        utils::log() << "Problem file: " << argv[2] << std::endl;

        // TODO: parse -> normalize -> instantiate -> invariants ->
        //       fact groups -> axioms -> simplify -> write output.sas
        utils::log() << "Translator pipeline not yet implemented."
                     << std::endl;

        utils::log() << "Total time: " << utils::elapsed_seconds() << "s"
                     << std::endl;
        return static_cast<int>(utils::ExitCode::TRANSLATE_UNSUPPORTED);
    } catch (const utils::ExitException &e) {
        return static_cast<int>(e.get_exit_code());
    }
}
