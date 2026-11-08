#ifndef UTILS_LOGGING_H
#define UTILS_LOGGING_H

#include <iostream>

namespace translate::utils {
// All translator log output goes to stdout, matching the Python translator.
std::ostream &log();
}

#endif
