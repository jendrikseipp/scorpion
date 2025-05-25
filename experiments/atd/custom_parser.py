#! /usr/bin/env python
import json
from lab.parser import Parser
import re
import logging


class CommonParser(Parser):
    def __init__(self):
        super().__init__()
        self.add_bottom_up_pattern ( 
            "num_transitions", r"Total number of transitions in Cartesian abstractions \(after label reduction\): (\d+)", type=int
        )
        self.add_bottom_up_pattern ( 
            "num_single_transitions", r"Total number of single transitions in Cartesian abstractions: (\d+)", type=int
        )
        self.add_bottom_up_pattern ( 
            "num_labels", r"Total number of labels in Cartesian abstractions: (\d+)", type=int
        )
        self.add_bottom_up_pattern ( 
            "change_in_size", r"Total change in transitions \(\(#single transitions\+#labels\)/#transitions\): ([\d\.]+)", type=float
        )
        self.add_function(self.extract_label_size_counts_json)
        self.add_bottom_up_pattern ( 
            "cp_time", r"Time for computing cost partitionings: (.+)s", type=float
        )
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

    def extract_label_size_counts_json(self, content, props):
        match = re.search(r'Label size counts: (\{.*?\})', content)
        if match:
            try:
                parsed = json.loads(match.group(1))
                for k, v in parsed.items():
                    props[f"label_size_{k}"] = v
            except json.JSONDecodeError as e:
                logging.error(f"Failed to parse label size JSON: {e}")

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
