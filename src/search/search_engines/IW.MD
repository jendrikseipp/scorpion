# Search Algorithm Call Strings

## IW(k)
`iw(k) = iw(width=k, goal_test=top_goal())`

## IWR(k)
`iwr(k) = iw(width=k, goal_test=sketch(<filename>))`

## SIW(k)
`siw(k) = serialized_search(child_search=iw(k), goal_test=top_goal())`

## SIWR(k)
`siwr(k) = serialized_search(child_search=iwr(k), goal_test=top_goal())`

## HSIWR(k)
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
iw, parallelized_search, serialized_search runs steps until goal is reached, i.e., standard solve function.

## Hierarchical Search Engine
Set parent search engine to pass information back to the parent. Must be called on child search in initialize of parent search.
```void set_parent_search_engine(SearchEngine* parent);```
Callback method that is called on parent when child search reaches goal. Default behavior will be to check whether parent search reaches goal.
```virtual void on_goal(const State& state) = 0```
Reinitializes search for new initial state.
```virtual void set_initial_state(const State& state) = 0```

Extensions: do we want to perform parent goal check in nongoal states of child search? Then ask for highest level goal reached?
