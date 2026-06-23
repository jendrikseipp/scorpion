#include "timer.h"

#include <cstdio>
#include <ctime>

using namespace std;
namespace translate::utils {
using clock_type = chrono::steady_clock;

namespace {
const clock_type::time_point program_start = clock_type::now();
}

double elapsed_seconds() {
    using namespace chrono;
    auto delta = clock_type::now() - program_start;
    return duration_cast<duration<double>>(delta).count();
}

double cpu_seconds() {
    return static_cast<double>(clock()) / CLOCKS_PER_SEC;
}

string format_timing(double cpu, double wall) {
    char buf[64];
    snprintf(buf, sizeof(buf), "[%.3fs CPU, %.3fs wall-clock]", cpu,
                  wall);
    return buf;
}

PhaseTimer::PhaseTimer() : cpu_start(cpu_seconds()) {}

string PhaseTimer::str() const {
    return format_timing(cpu_seconds() - cpu_start, wall.seconds());
}

Timer::Timer() : start(clock_type::now()) {}

void Timer::reset() {
    start = clock_type::now();
}

double Timer::seconds() const {
    using namespace chrono;
    return duration_cast<duration<double>>(clock_type::now() - start).count();
}
}
