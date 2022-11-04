#! /usr/bin/env python

from pathlib import Path

from lab.experiment import Experiment

import project


ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "memory",
    "initial_h_value",
    "search_start_time_score", "search_start_memory_score",
    "time_for_searching_abstract_traces",
    "time_for_finding_flaws_and_computing_splits",
    "time_for_splitting_states",
    "time_for_updating_goal_distances",
    "time_for_building_abstraction",
    project.Attribute("cegar_found_concrete_solution", min_wins=False, absolute=True),
    project.Attribute("cegar_proved_unsolvability", min_wins=False, absolute=True),
    "cegar_*", "cartesian_*", "successor_generator_*",
]

exp = Experiment()
exp.add_step(
    "remove-combined-properties", project.remove_file, Path(exp.eval_dir) / "properties"
)

project.fetch_algorithm(exp, "2022-08-30-C-store-parents", "batch-sg-children=True-parents=True", new_algo="01-batch-sg-store-old")
project.fetch_algorithm(exp, "2022-08-30-D-store-f-optimal", "batch-sg-children=True-parents=True", new_algo="02-batch-sg-store-new")

filters = [project.add_evaluations_per_time]

project.add_absolute_report(
    exp, attributes=ATTRIBUTES, filter=filters, filter_cegar_found_concrete_solution=1, name=f"{exp.name}"
)


exp.run_steps()
