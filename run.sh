#! /bin/bash

BOUND="50"
killall zapccs
./build.py scp && ./fast-downward.py --build scp --keep --search-memory-limit 1G ../benchmarks/woodworking-opt08-strips/p19.pddl --heuristic "hscp=scp_online([projections(hillclimbing(max_generated_patterns=200, random_seed=0)), projections(systematic(2)), cartesian([landmarks(random_seed=0), goals(random_seed=0)])], interval=-2, orders=greedy_orders(random_seed=0), max_optimization_time=0, saturator=perimstar, diversify=False, samples=1K, max_time=1000, sample_from_generated_states=False, use_offline_samples=false, use_evaluated_state_as_sample=True, random_seed=0)" --search "astar(hscp, bound=$BOUND)" | tee reevaluate-false.txt

killall zapccs
./build.py scp && ./fast-downward.py --build scp --keep --search-memory-limit 1G ../benchmarks/woodworking-opt08-strips/p19.pddl --heuristic "hscp=scp_online([projections(hillclimbing(max_generated_patterns=200, random_seed=0)), projections(systematic(2)), cartesian([landmarks(random_seed=0), goals(random_seed=0)])], interval=-2, orders=greedy_orders(random_seed=0), max_optimization_time=0, saturator=perimstar, diversify=False, samples=1K, max_time=1000, sample_from_generated_states=False, use_offline_samples=false, use_evaluated_state_as_sample=True, random_seed=0)" --search "astar(hscp, lazy_evaluator=hscp, bound=$BOUND)" | tee reevaluate-true.txt

killall zapccs

clean () {
    file="$1"
    sed -i 's/\([0-9][0-9]*\.[0-9][0-9e\-]*\)s/?s/g' "$file"
    sed -i 's/[0-9][0-9]* KB/? KB/g' "$file"
}

clean reevaluate-false.txt
clean reevaluate-true.txt

meld reevaluate-false.txt reevaluate-true.txt
