#include "flaw.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "split_selector.h"
#include "transition.h"


using namespace std;

namespace cegar {
Flaw::Flaw(int abstract_state_id,
           Split &&desired_split)
    :      abstract_state_id(abstract_state_id),
           desired_split(move(desired_split)) {
}
}
