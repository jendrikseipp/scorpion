#include "timer.h"

namespace translate::utils {
using clock_type = std::chrono::steady_clock;

namespace {
const clock_type::time_point program_start = clock_type::now();
}

double elapsed_seconds() {
    using namespace std::chrono;
    auto delta = clock_type::now() - program_start;
    return duration_cast<duration<double>>(delta).count();
}

Timer::Timer() : start(clock_type::now()) {}

void Timer::reset() {
    start = clock_type::now();
}

double Timer::seconds() const {
    using namespace std::chrono;
    return duration_cast<duration<double>>(clock_type::now() - start).count();
}
}
