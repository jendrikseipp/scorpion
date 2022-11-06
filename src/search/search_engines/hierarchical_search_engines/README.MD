# How to use

## IW

```console
./fast-downward.py --keep-sas-file --build=debug domain.pddl instance_2_2_0.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "iw(width=2, goal_test=top_goal())"
```

## SIW

```console
./fast-downward.py --keep-sas-file --build=debug domain.pddl instance_2_2_0.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "serialized_search(child_searches=[iw(width=2, goal_test=increment_goal_count())], goal_test=top_goal())"
```

## SIWR

```console
./fast-downward.py --keep-sas-file --build=debug domain_gripper.pddl p10_gripper.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "serialized_search(child_searches=[iw(width=1, goal_test=sketch_subgoal(filename=/home/dominik/projects/code/scorpion/hierarchical_sketch/sketch_1.txt))], goal_test=top_goal())"
```

## HSIWR

```console
./fast-downward.py --keep-sas-file --build=debug domain_gripper.pddl p10_gripper.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "serialized_search(child_searches=[parallelized_search(child_searches=[serialized_search(child_searches=[iw(width=0, goal_test=sketch_subgoal(filename=/home/dominik/projects/code/scorpion/hierarchical_sketch/rule_0/rule_0/sketch_0.txt))], goal_test=sketch_subgoal(filename=/home/dominik/projects/code/scorpion/hierarchical_sketch/rule_0/sketch_1.txt)),serialized_search(child_searches=[iw(width=0, goal_test=sketch_subgoal(filename=/home/dominik/projects/code/scorpion/hierarchical_sketch/rule_1/rule_0/sketch_0.txt))], goal_test=sketch_subgoal(filename=/home/dominik/projects/code/scorpion/hierarchical_sketch/rule_1/sketch_1.txt))], goal_test=sketch_subgoal(filename=/home/dominik/projects/code/scorpion/hierarchical_sketch/sketch_1.txt))], goal_test=top_goal())"
```
