#include "logging.h"

#include <iostream>

using namespace std;
namespace translate::utils {
ostream &log() {
    return cout;
}
}
