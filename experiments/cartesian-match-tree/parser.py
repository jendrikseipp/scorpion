#! /usr/bin/env python

import logging
import math
import re

from lab.parser import Parser


class CommonParser(Parser):
    def add_repeated_pattern(
        self, name, regex, file="run.log", required=False, type=int
    ):
        def find_all_occurences(content, props):
            matches = re.findall(regex, content)
            if required and not matches:
                logging.error(f"Pattern {regex} not found in file {file}")
            props[name] = [type(m) for m in matches]

        self.add_function(find_all_occurences, file=file)

    def add_bottom_up_pattern(
        self, name, regex, file="run.log", required=False, type=int
    ):
        def search_from_bottom(content, props):
            reversed_content = "\n".join(reversed(content.splitlines()))
            match = re.search(regex, reversed_content)
            if required and not match:
                logging.error(f"Pattern {regex} not found in file {file}")
            if match:
                props[name] = type(match.group(1))

        self.add_function(search_from_bottom, file=file)


def find_cegar_termination_criterion(content, props):
    outcomes = {
        "cegar_found_concrete_solution": "Found concrete solution.",
        "cegar_proved_unsolvability": "Abstract task is unsolvable.",
        "cegar_reached_states_limit": "Reached maximum number of states.",
        "cegar_reached_transitions_limit": "Reached maximum number of transitions.",
        "cegar_reached_time_limit": "Reached time limit.",
        "cegar_reached_memory_limit": "Reached memory limit.",
    }

    for outcome, text in outcomes.items():
        props[outcome] = int(text in content)
        if props[outcome]:
            if "cegar_outcome" in props:
                props["cegar_outcome"] = "mixed"
            else:
                props["cegar_outcome"] = outcome


def add_scores(content, props):
    """
    Convert some properties into scores in the range [0, 1].

    Best possible performance in a task is counted as 1, while worst
    performance is counted as 0.

    """

    def log_score(value, min_bound, max_bound):
        if value is None:
            return 0
        value = max(value, min_bound)
        value = min(value, max_bound)
        raw_score = math.log(value) - math.log(max_bound)
        best_raw_score = math.log(min_bound) - math.log(max_bound)
        return raw_score / best_raw_score

    try:
        max_time = props["limit_search_time"]
    except KeyError:
        print("search time limit missing -> can't compute time scores")
    else:
        props["search_start_time_score"] = log_score(
            props.get("search_start_time"), min_bound=1.0, max_bound=max_time
        )

    try:
        max_memory_kb = props["limit_search_memory"] * 1024
    except KeyError:
        print("search memory limit missing -> can't compute memory score")
    else:
        props["search_start_memory_score"] = log_score(
            props.get("search_start_memory"), min_bound=2000, max_bound=max_memory_kb
        )


def main():
    parser = CommonParser()
    parser.add_bottom_up_pattern(
        "search_start_time",
        r"\[t=(.+)s, \d+ KB\] g=0, 1 evaluated, 0 expanded",
        type=float,
    )
    parser.add_bottom_up_pattern(
        "search_start_memory",
        r"\[t=.+s, (\d+) KB\] g=0, 1 evaluated, 0 expanded",
        type=int,
    )
    parser.add_pattern(
        "initial_h_value",
        r"f = (\d+) \[1 evaluated, 0 expanded, t=.+s, \d+ KB\]",
        type=int,
    )
    parser.add_repeated_pattern(
        "h_values",
        r"New best heuristic value for .+: (\d+)\n",
        type=int,
    )

    # Cartesian abstractions.
    parser.add_pattern("time_for_computing_cartesian_abstractions", r"Time for initializing additive Cartesian heuristic: (.+)s\n", type=float)
    parser.add_pattern("time_for_computing_cartesian_abstractions", r"Time for building Cartesian abstractions: (.+)s\n", type=float)
    parser.add_pattern("cartesian_states", r"Cartesian states: (.+)\n", type=int)
    parser.add_pattern("cartesian_unreachable_states", r"Unreachable Cartesian states: (.+)\n", type=int)
    parser.add_pattern("cartesian_unsolvable_states", r"Unsolvable Cartesian states: (.+)\n", type=int)
    #parser.add_bottom_up_pattern("cartesian_states_total", r"Total number of Cartesian states: (.+)\n", type=int)
    #parser.add_pattern("cartesian_transitions", r"Total number of non-looping transitions: (.+)\n", type=int)
    #parser.add_pattern("cartesian_transitions", r"Total number of transitions in Cartesian abstractions: (.+)\n", type=int)
    parser.add_pattern("cartesian_loops", r"Looping transitions: (.+)\n", type=int)
    parser.add_pattern("cartesian_transitions", r"Non-looping transitions: (.+)\n", type=int)

    parser.add_pattern("cartesian_states_memory", r"Estimated memory usage for abstract states: (.+) KB\n", type=int)
    parser.add_pattern("cartesian_single_state_memory", r"Estimated memory usage for single Cartesian state: (.+) B\n", type=int)
    parser.add_pattern("cartesian_sets_memory", r"Estimated memory usage for Cartesian states: (.+) KB\n", type=int)
    parser.add_pattern("cartesian_hierarchy_memory", r"Refinement hierarchy estimated memory usage: (.+) KB\n", type=int)
    parser.add_pattern("cartesian_hierarchy_nodes", r"Refinement hierarchy nodes: (.+)\n", type=int)
    parser.add_pattern("cartesian_helper_nodes", r"Cartesian helper nodes: (.+)\n", type=int)
    parser.add_pattern("cartesian_sets", r"Cartesian sets: (.+)\n", type=int)

    parser.add_pattern("cartesian_match_tree_static_memory", r"Match tree estimated memory usage for operator info: (.+) KB\n", type=int)

    parser.add_pattern("cartesian_transitions_memory", r"Match tree estimated memory usage: (.+) KB\n", type=int)
    parser.add_pattern("cartesian_transitions_memory", r"Transition system estimated memory usage: (.+) KB\n", type=int)

    parser.add_pattern("cartesian_goal_distances_memory", r"Goal distances estimated memory usage: (.+) KB\n", type=int)
    parser.add_pattern("cartesian_shortest_path_tree_memory", r"Shortest path tree estimated memory usage: (.+) KB\n", type=int)
    parser.add_pattern("cartesian_shortest_path_children_memory", r"Shortest path children estimated memory usage: (.+) KB\n", type=int)

    parser.add_pattern("time_for_searching_abstract_traces", r"Time for finding abstract traces: (.+)s\n", type=float)
    parser.add_pattern("time_for_finding_flaws_and_computing_splits", r"Time for finding flaws and computing splits: (.+)s\n", type=float)
    parser.add_pattern("time_for_splitting_states", r"Time for splitting states: (.+)s\n", type=float)
    parser.add_pattern("time_for_updating_goal_distances", r"Time for updating goal distances: (.+)s\n", type=float)
    parser.add_pattern("time_for_building_abstraction", r"Time for building abstraction: (.+)s\n", type=float)

    #parser.add_repeated_pattern("cartesian_abstraction_build_times", r"Time for building abstraction: (.+)s\n", type=float)
    #parser.add_pattern("cartesian_saturated_cost_functions_time", r"Time for computing saturated cost functions: (.+)s\n", type=float)

    parser.add_pattern("successor_generator_creation_time", r"time for successor generation creation: (.+)s\n", type=float)
    parser.add_pattern("successor_generator_creation_memory", r"peak memory difference for successor generator creation: (.+) KB\n", type=int)

    parser.add_bottom_up_pattern("cegar_lower_bound", r"Abstract solution cost: (.+)\n", type=int)

    parser.add_function(find_cegar_termination_criterion)
    parser.add_function(add_scores)

    parser.parse()


if __name__ == "__main__":
    main()
