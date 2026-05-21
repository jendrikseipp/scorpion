#ifndef TRANSLATE_UTILS_TIMER_H
#define TRANSLATE_UTILS_TIMER_H

#include <chrono>

namespace translate::utils {
// Wall-clock seconds since program start.
double elapsed_seconds();

class Timer {
    std::chrono::steady_clock::time_point start;
public:
    Timer();
    void reset();
    double seconds() const;
};
}

#endif
