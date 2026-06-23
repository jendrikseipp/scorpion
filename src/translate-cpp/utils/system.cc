#include "system.h"

#include "logging.h"

using namespace std;
namespace translate::utils {
void exit_with(ExitCode code) {
    throw ExitException(code, string());
}

void exit_with(ExitCode code, const string &message) {
    if (!message.empty())
        log() << message << endl;
    throw ExitException(code, message);
}
}
