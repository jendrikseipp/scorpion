import logging
import re

from lab.parser import Parser
from lab import tools


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


def add_scores(content, props):
    """
    Convert some properties into scores in the range [0, 1].

    Best possible performance in a task is counted as 1, while worst
    performance is counted as 0.

    """
    try:
        max_time = props["limit_search_time"]
    except KeyError:
        print("search time limit missing -> can't compute time scores")
    else:
        props["search_start_time_score"] = tools.compute_log_score(
            props.get("coverage", 0), props.get("search_start_time"),
            lower_bound=1.0, upper_bound=max_time
        )

    try:
        max_memory_kb = props["limit_search_memory"] * 1024
    except KeyError:
        print("search memory limit missing -> can't compute memory score")
    else:
        props["search_start_memory_score"] = tools.compute_log_score(
            props.get("coverage", 0), props.get("search_start_memory"),
            lower_bound=2000, upper_bound=max_memory_kb
        )


def get_parser():
    parser = CommonParser()
    parser.add_pattern(
        "search_start_time",
        r"\[t=(.+)s, \d+ KB\] Initial heuristic value for .+: (?:\d+|infinity)\n",
        type=float,
    )
    parser.add_pattern(
        "search_start_memory",
        r"\[t=.+s, (\d+) KB\] Initial heuristic value for .+: (?:\d+|infinity)\n",
        type=int,
    )

    parser.add_function(add_scores)

    parser.add_pattern("preprocessor_time", r"Preprocessor time: (.+)s\n", type=float)
    parser.add_pattern("preprocessor_memory", r"Preprocessor peak memory: (.+) KB\n", type=int)
    for name in ["task size", "variables", "facts", "operators", "mutex groups"]:
        parser.add_pattern(f"preprocessor_{name.replace(' ', '_')}", rf"Preprocessor {name}: (.+)\n", type=int)

    parser.add_pattern("preprocessor_merged_variable_groups", r"Merged (\d+) variable groups\n", type=int)
    parser.add_pattern("preprocessor_variables_merged", r"Total variables merged: (\d+)\n", type=int)
    parser.add_pattern("preprocessor_variables_eliminated_by_merging", r"Variables eliminated by merging: (\d+)\n", type=int)
    parser.add_pattern("preprocessor_variable_merging_time", r"Variable merging time: (.+)s\n", type=float)

    parser.add_pattern("preprocessor_mutex_computation_time", r"Mutex computation completed in (.+?)s \(\d+ iterations\)\n", type=float)
    parser.add_pattern("preprocessor_mutex_computation_iterations", r"Mutex computation completed in .+?s \((\d+) iterations\)\n", type=int)

    return parser
