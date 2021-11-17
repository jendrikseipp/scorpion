#include "flaw.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "split_selector.h"
#include "transition.h"


using namespace std;

namespace cegar {
Flaw::Flaw(State &&concrete_state,
           int abstract_state_id,
           Split&& desired_split)
    : concrete_state(move(concrete_state)),
      abstract_state_id(abstract_state_id),
      desired_split(move(desired_split)) {
}

}
