#! /usr/bin/env python

from lab.parser import Parser
import re
import logging


class CommonParser(Parser):
    def __init__(self):
        super().__init__()
        self.add_bottom_up_pattern ( 
            "num_transitions", r"Total number of transitions in Cartesian abstractions: (\d+)", type=int
        )
        # self.add_bottom_up_pattern ( 
        #     "num_single_transitions", r"Total number of single transitions in Cartesian abstractions: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern ( 
        #     "num_labels", r"Total number of labels in Cartesian abstractions: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern ( 
        #     "change_in_size", r"Total change in transitions ((#single transitions+#labels)/#transitions): (.+)s", type=float
        # )
        # self.add_bottom_up_pattern(
        #     "num_abstractions", "Number of abstractions: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern(
        #     "tree_generation_time", r"Time to generate tree: (.+)s", type=float
        # )
        # self.add_bottom_up_pattern("tree_depth", "Depth of tree: (\d+)", type=int)
        # self.add_bottom_up_pattern(
        #     "num_generated_nodes", "Generated nodes: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern(
        #     "num_generated_sum_nodes", "Generated sum nodes: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern(
        #     "num_generated_nontrivial_sum_nodes",
        #     "Generated nontrivial sum nodes: (\d+)",
        #     type=int,
        # )
        # self.add_bottom_up_pattern(
        #     "num_generated_max_nodes", "Generated max nodes: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern("num_nodes", "Reachable nodes: (\d+)", type=int)
        # self.add_bottom_up_pattern(
        #     "num_sum_nodes", "Reachable sum nodes: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern(
        #     "num_nontrivial_sum_nodes",
        #     "Reachable nontrivial sum nodes: (\d+)",
        #     type=int,
        # )
        # self.add_bottom_up_pattern(
        #     "num_max_nodes", "Reachable max nodes: (\d+)", type=int
        # )
        # self.add_bottom_up_pattern(
        #     "num_lookup_tables",
        #     "Initializing structured SCP order heuristic with (\d+) values to lookup",
        #     type=int,
        # )
        # self.add_pattern( #top down
        #     "instructions",
        #     "Initializing structured SCP order heuristic with (\d+) compositional instructions",
        #     type=int,
        # )
        # self.add_pattern(
        #     "post_pruned_orders",
        #     "Post pruning removed: (\d+) orders",
        #     type=int,
        # )
        # self.add_pattern(
        #     "dead_end_tables",
        #     "dead end tables: (\d+)",
        #     type=int,
        # )
        # self.add_pattern(
        #     "num_orders",
        #     r"Number of orders: (\d+)",
        #     type=int,
        # )
        # self.add_pattern(
        #     "unsolvability_heuristic",
        #     r"unsolvability: (\d+)",
        #     type=int,
        # )
        # self.add_pattern(
        #     "max_lookup_cache_size",
        #     r"max lookup table entries: (\d+.\d+)",
        #     type=float,
        # )
        # self.add_pattern(
        #     "recomputed_lookup_tables",
        #     r"recomputed lookup tables: (\d+)",
        #     type=int,
        # )
        # self.add_pattern(
        #     "lookup_cache_hits",
        #     r"lookup table cache hits: (\d+)",
        #     type=int,
        # )
        # self.add_pattern(
        #     "lookup_cache_size",
        #     r"lookup table cache size: (\d+)",
        #     type=int,
        # )
        # self.add_pattern(
        #     "stored_cost_functions",
        #     r"stored cost functions: (\d+)",
        #     type=int,
        # )
        self.add_function(self.search_started)

    def _get_flags(self, flags_string):
        flags = 0
        for char in flags_string:
            flags |= getattr(re, char)
        return flags

    def add_bottom_up_pattern(
        self, name, regex, file="run.log", required=False, type=int, flags=""
    ):
        def search_from_bottom(content, props):
            reversed_content = "\n".join(reversed(content.splitlines()))
            match = re.search(regex, reversed_content, flags=self._get_flags(flags))
            if required and not match:
                logging.error("Pattern {0} not found in file {1}".format(regex, file))
            if match:
                props[name] = type(match.group(1))

        self.add_function(search_from_bottom, file=file)

    def search_started(self, content, props):
        props["search_started"] = int("g=0, 1 evaluated, 0 expanded" in content)


def add_sscp_memory_cause(run):
    if run.get("error") == "search-out-of-memory":
        if run.get("tree_generation_time") is not None:
            run["search_out_of_memory"] = True
        elif run.get("abstractions") is not None:
            run["graph_out_of_memory"] = True
        else:
            run["abstractions_out_of_memory"] = True
    return run


def add_search_started(run):
    run["search_started"] = run.get("search_start_time") is not None
    return run
