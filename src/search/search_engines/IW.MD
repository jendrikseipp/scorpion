# Game Plan
## Search Algorithm Call Strings

### IW(k)
`iw(k) = iw(width=k, goal_test=top_goal())`

### IWR(k)
`iwr(k) = iw(width=k, goal_test=sketch(<filename>))`

### SIW(k)
`siw(k) = serialized_search(child_search=iw(k), goal_test=top_goal())`

### SIWR(k)
`siwr(k) = serialized_search(child_search=iwr(k), goal_test=top_goal())`

### HSIWR(k)
```
hsiwr(k) =
serialized_search(child_search=
  parallelized_search(child_searches=[
    serialized_search(child_search=
      iwr(k, goal_test=<policy_1_filename>),
      goal_test=<rule_1_filename>),
    ...,
    serialized_search(child_search=
      iwr(k, goal_test=<policy_n_filename>),
      goal_test=<rule_n_filename>)],
    goal_test=sketch(<filename>)),
  goal_test=top_goal())
```
Arbitrarily many levels are possible.

## Search Engine step-Function

iw expands one state in BrFS with novelty pruning.
parallelized_search executes one step of each child_search (until next g-layer?).
serialized_search executes one step of child_search.

## Search Engine solve-Function
iw, parallelized_search, serialized_search runs steps until goal is reached, i.e., default solve function.

## Hierarchical Search Engine
iw, parallelized_search, serialized_search are HierarchicalSearchEngines. We must be able to pass information up and down in the hierarchy. For instance, in SIW(k) is the low level goal state also a high level goal state. This requires a pointer to the parent search and functions to pass information between parent and child.

1. [parent -> child] Notify child about its parent search engine. Must be called on child search in initialize of parent search.
```virtual void set_parent_search_engine(SearchEngine& parent) = 0;```
2. [child -> parent] Notify parent that low level goal was reached. Default behavior will be to check this state is also goal in parent search.
```virtual void on_goal(const State& state) = 0```
3. [parent -> child] Notify child that we found new initial state.
```virtual void set_initial_state(const State& state) = 0```

Extensions: we might want to perform parent goal check in nongoal states of child search.

## Implementational Details
- Q1: sharing state space over searches? A: Top level search creates a shared SearchSpace that remains for the rest of the search.
- Q2: how to set new initial state with low overhead? A: `class ModifiedInitialStateTask : public DelegatingTask {};` When modifying a SearchEngines task, this information propagates to the task_proxy.

- filename string: filename_1 (filename_2 (filename_3 (filename_4, filename_5)))