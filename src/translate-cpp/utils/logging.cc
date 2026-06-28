#include "logging.h"

#include "timer.h"

#include <iostream>

using namespace std;
namespace translate::utils {
ostream &log() {
    return cout;
}

ScopedTimerLog::ScopedTimerLog(const char *label)
    : label(label), start_seconds(elapsed_seconds()) {
    log() << label << "..." << endl;
}

ScopedTimerLog::~ScopedTimerLog() {
    double elapsed = elapsed_seconds() - start_seconds;
    log() << "Done! [" << elapsed << "s CPU, " << elapsed << "s wall-clock]"
          << endl;
}
}
