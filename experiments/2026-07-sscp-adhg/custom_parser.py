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
    """Convert some properties into scores in the range [0, 1]."""
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
    parser.add_pattern(
        "initial_h_value",
        r"Initial heuristic value for sscp: (\d+)\n",
        type=int,
    )

    # Time breakdown of the heuristic construction, for later speed
    # analysis: pattern selection, projection construction, DAG generation.
    # Together with search_start_time (all construction), search_time and
    # total_time from the standard parsers, this splits every phase.
    parser.add_pattern(
        "time_for_computing_patterns",
        r"Time for computing patterns: (.+)s\n", type=float
    )
    parser.add_pattern(
        "time_for_building_projections",
        r"Time for building projections: (.+)s\n", type=float
    )
    # The DAG generation time line reads "Time to generate DAG" in all
    # compared revisions.
    parser.add_pattern(
        "sscp_dag_time", r"Time to generate DAG: (.+)s\n", type=float
    )
    # Cache effectiveness counters, to guide future optimizations.
    parser.add_pattern(
        "sscp_lookup_cache_hits", r"Lookup table cache hits: (\d+)\n",
        type=int
    )
    parser.add_pattern(
        "sscp_recomputed_lookup_tables", r"Recomputed lookup tables: (\d+)\n",
        type=int
    )
    parser.add_pattern(
        "sscp_generated_nodes", r"Generated nodes: (\d+)\n", type=int
    )
    parser.add_pattern(
        "sscp_reachable_nodes", r"Reachable nodes: (\d+)\n", type=int
    )
    parser.add_pattern(
        "sscp_instructions",
        r"with (\d+) compositional instructions\.\n",
        type=int,
    )
    parser.add_pattern(
        "sscp_stored_cost_functions", r"Stored cost functions: (\d+)\n",
        type=int,
    )
    parser.add_pattern(
        "sscp_abstractions", r"Number of abstractions: (\d+)\n", type=int
    )

    parser.add_function(add_scores)
    return parser
