#ifndef UTILS_LOGGING_H
#define UTILS_LOGGING_H

#include <iostream>

namespace translate::utils {
// All translator log output goes to stdout, matching the Python translator.
std::ostream &log();

class ScopedTimerLog {
    const char *label;
    double start_seconds;
public:
    explicit ScopedTimerLog(const char *label);
    ~ScopedTimerLog();
};
}

#endif
