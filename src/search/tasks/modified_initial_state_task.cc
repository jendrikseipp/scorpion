#include "modified_initial_state_task.h"

#include <memory>
#include <vector>

using namespace std;

namespace extra_tasks {

ModifiedInitialStateTask::ModifiedInitialStateTask(
    const shared_ptr<AbstractTask> &parent,
    vector<int>&& initial_state_values)
    : DelegatingTask(parent),
      initial_state_values(move(initial_state_values)) { }


vector<int> ModifiedInitialStateTask::get_initial_state_values() const {
    return initial_state_values;
}

}