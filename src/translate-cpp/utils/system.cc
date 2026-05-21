#include "system.h"

#include "logging.h"

namespace translate::utils {
void exit_with(ExitCode code) {
    throw ExitException(code, std::string());
}

void exit_with(ExitCode code, const std::string &message) {
    if (!message.empty())
        log() << message << std::endl;
    throw ExitException(code, message);
}
}
