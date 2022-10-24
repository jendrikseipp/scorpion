#ifndef TASKS_MODIFIABLE_INITIAL_STATE_TASK_H
#define TASKS_MODIFIABLE_INITIAL_STATE_TASK_H

#include "delegating_task.h"

#include <memory>
#include <vector>


namespace extra_tasks {
class ModifiedInitialStateTask : public tasks::DelegatingTask {
private:
    const std::vector<int> initial_state_values;

public:
    ModifiedInitialStateTask(
        const std::shared_ptr<AbstractTask> &parent,
        std::vector<int>&& initial_state_values);

    virtual std::vector<int> get_initial_state_values() const override;
};
}

#endif
