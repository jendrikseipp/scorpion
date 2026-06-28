#include "system.h"

#include "logging.h"

#ifndef _WIN32
#include <sys/resource.h>
#endif

using namespace std;
namespace translate::utils {
long get_peak_memory_in_kb() {
#ifdef _WIN32
    return -1;
#else
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
#endif
}

void exit_with(ExitCode code) {
    throw ExitException(code, string());
}

void exit_with(ExitCode code, const string &message) {
    if (!message.empty())
        log() << message << endl;
    throw ExitException(code, message);
}
}
