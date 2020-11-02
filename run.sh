#! /bin/bash

PROJECT="project"
TASK=../benchmarks/blocks/probBLOCKS-4-1.pddl
TASK=../benchmarks/elevators-opt08-strips/p02.pddl
ABSTRACTIONS="[projections(hillclimbing(max_generated_patterns=200, random_seed=0)), projections(systematic(2)), cartesian([landmarks(order=random, random_seed=0), goals(order=random, random_seed=0)])]"
ABSTRACTIONS="[cartesian([goals(order=random, random_seed=0)], debug=true)]"
#ABSTRACTIONS="[projections(systematic(1))]"
#ABSTRACTIONS="[projections(manual_patterns([[6]]))]"

killall zapccs; ./build.py "$PROJECT"

./fast-downward.py --keep --build "$PROJECT" "$TASK" --search "astar(operatorcounting([pho_abstraction_constraints($ABSTRACTIONS, saturated=True, consider_finite_negative_saturated_costs=True,forbid_useless_operators=True)]), bound=0)" | tee online.txt #| grep "\[1 evaluated"

./fast-downward.py --keep --build "$PROJECT" "$TASK" --search "astar(pho($ABSTRACTIONS, max_time=infinity, diversify=False, max_orders=1, max_optimization_time=0, orders=random_orders(random_seed=0), verbosity=debug, saturated=true), bound=0)" | tee offline.txt #| grep "\[1 evaluated"
