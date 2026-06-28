#ifndef UTILS_TIMER_H
#define UTILS_TIMER_H

#include <chrono>
#include <string>

namespace translate::utils {
// Wall-clock seconds since program start.
double elapsed_seconds();

// Process CPU seconds (user + system) so far, mirroring Python's
// os.times()[0] + os.times()[1]. Used to print phase timings in the
// Python translator's "[%.3fs CPU, %.3fs wall-clock]" format so that
// Lab's stock translator parser captures them.
double cpu_seconds();

// Format a "[%.3fs CPU, %.3fs wall-clock]" suffix matching the Python
// translator (and the regex in downward/parsers/translator_parser.py).
std::string format_timing(double cpu, double wall);

class Timer {
    std::chrono::steady_clock::time_point start;
public:
    Timer();
    void reset();
    double seconds() const;
};

// Captures wall and CPU time at construction; str() returns the
// Python-style "[%.3fs CPU, %.3fs wall-clock]" elapsed-since-construction
// suffix.
class PhaseTimer {
    Timer wall;
    double cpu_start;
public:
    PhaseTimer();
    std::string str() const;
};
}

#endif
