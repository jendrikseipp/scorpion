#include "flaw.h"

using namespace std;

namespace cegar {
Flaw::Flaw(int abstract_state_id,
           Split &&desired_split)
    :      abstract_state_id(abstract_state_id),
           desired_split(move(desired_split)) {
}
}
