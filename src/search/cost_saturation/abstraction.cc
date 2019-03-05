#include "abstraction.h"

#include "../utils/collections.h"

#include <cassert>

using namespace std;

namespace cost_saturation {
Abstraction::Abstraction(unique_ptr<AbstractionFunction> abstraction_function)
    : has_transition_system_(true),
      abstraction_function(move(abstraction_function)) {
}

bool Abstraction::has_transition_system() const {
    return has_transition_system_;
}

void Abstraction::remove_transition_system() {
    assert(has_transition_system());
    release_transition_system_memory();
    has_transition_system_ = false;
}

std::unique_ptr<AbstractionFunction> Abstraction::extract_abstraction_function() {
    return move(abstraction_function);
}
}
