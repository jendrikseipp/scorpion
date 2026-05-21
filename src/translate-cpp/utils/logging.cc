#include "logging.h"

#include "timer.h"

#include <iostream>

namespace translate::utils {
std::ostream &log() {
    return std::cout;
}

ScopedTimerLog::ScopedTimerLog(const char *label)
    : label(label), start_seconds(elapsed_seconds()) {
    log() << label << "..." << std::endl;
}

ScopedTimerLog::~ScopedTimerLog() {
    double elapsed = elapsed_seconds() - start_seconds;
    log() << "Done! [" << elapsed << "s CPU, " << elapsed << "s wall-clock]"
          << std::endl;
}
}
