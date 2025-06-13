#! /usr/bin/env python
import json
from lab.parser import Parser
import re
import logging


class CommonParser(Parser):
    def __init__(self):
        super().__init__()

        self.add_pattern (
        "search_start_time", r"\[t=(.+)s, \d+ KB\] g=0, 1 evaluated, 0 expanded", type=float,
        )
        self.add_pattern (
        "search_start_memory", r"\[t=.+s, (\d+) KB\] g=0, 1 evaluated, 0 expanded", type=int,
        )
        self.add_indexed_pattern_mapping(
            r"Total number of transitions in Abstractions \(after label reduction\): (\d+)",
            type=int,
            name_mapping=[
                (0, "cartesian1_num_transitions"),
                (1, "cartesian2_num_transitions"),
                (-1, "projection_num_transitions"),
            ]
        )
        self.add_indexed_pattern_mapping(
            r"Total number of non-label transitions in Abstractions: (\d+)",
            type=int,
            name_mapping=[
                (0, "cartesian1_num_non_label_transitions"),
                (1, "cartesian2_num_non_label_transitions"),
                (-1, "projection_num_non_label_transitions"),
            ]
        )
        self.add_indexed_pattern_mapping(
            r"Total number of label transitions in Abstractions: (\d+)",
            type=int,
            name_mapping=[
                (0, "cartesian1_num_label_transitions"),
                (1, "cartesian2_num_label_transitions"),
                (-1, "projection_num_label_transitions"),
            ]
        )
        self.add_indexed_pattern_mapping(
            r"Total number of labels in Abstractions: (\d+)",
            type=int,
            name_mapping=[
                (0, "cartesian1_num_labels"),
                (1, "cartesian2_num_labels"),
                (-1, "projection_num_labels"),
            ]
        )
        self.add_indexed_pattern_mapping(
            r"Total number of reused label transitions in Abstractions: (\d+)",
            type=int,
            name_mapping=[
                (0, "cartesian1_num_reused_labels"),
                (1, "cartesian2_num_reused_labels"),
                (-1, "projection_num_reused_labels"),
            ]
        )
        self.add_sum_pattern ( 
            "num_transitions", r"Total number of transitions in Abstractions \(after label reduction\): (\d+)", type=int
        )
        self.add_bottom_up_pattern ( 
            "change_in_size", r"Total change in transitions \(\(#non-label transitions\+#label transitions\)/#transitions\): ([\d\.]+)", type=float
        )
        self.add_sum_pattern ( 
            "num_non_label_transitions", r"Total number of non-label transitions in Abstractions: (\d+)", type=int
        )
        self.add_sum_pattern ( 
            "num_label_transitions", r"Total number of label transitions in Abstractions: (\d+)", type=int
        )
        self.add_sum_pattern ( 
            "num_labels", r"Total number of labels in Abstractions: (\d+)", type=int
        )
        self.add_sum_pattern ( 
            "num_reused_labels", r"Total number of reused label transitions in Abstractions: (\d+)", type=int
        )
        self.add_label_size_counts_sum_dict()
        self.add_indexed_json_pattern_mapping(
            r'Total label size counts: (\{.*?\})',
            name_mapping=[
                (0, "cartesian1_label_size_counts"),
                (1, "cartesian2_label_size_counts"),
                (-1, "projection_label_size_counts"),
            ]
        )
        self.add_label_size_counts_sum_dict(
            name="reused_label_size_counts",
            regex=r'Total reused label size counts: (\{.*?\})'
        )
        self.add_indexed_json_pattern_mapping(
            r'Total reused label size counts: (\{.*?\})',
            name_mapping=[
                (0, "cartesian1_reused_label_size_counts"),
                (1, "cartesian2_reused_label_size_counts"),
                (-1, "projection_reused_label_size_counts"),
            ]
        )
        self.add_sum_pattern ( 
            "cp_time", r"Time for computing cost partitionings: (.+)s", type=float
        )
        self.add_function(self.search_started)

    def _get_flags(self, flags_string):
        flags = 0
        for char in flags_string:
            flags |= getattr(re, char)
        return flags

    # def add_pattern(self, name, regex, file="run.log", required=False, type=int, flags="M"):
    #     Parser.add_pattern(
    #         self, name, regex, file=file, required=required, type=type, flags=flags
    #     )

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

    def add_sum_pattern(self, name, regex, file="run.log", type=int, flags=""):
        def sum_matches(content, props):
            matches = re.finditer(regex, content, flags=self._get_flags(flags))
            total = sum(type(match.group(1)) for match in matches)
            props[name] = total
        self.add_function(sum_matches, file=file)

    def add_label_size_counts_sum_dict(self, name="label_size_counts", regex=r'Total label size counts: (\{.*?\})', file="run.log"):
        def sum_label_size_dicts(content, props):
            matches = list(re.finditer(regex, content))
            total_counts = {}
            for match in matches:
                try:
                    parsed = json.loads(match.group(1))
                    for key, value in parsed.items():
                        key = int(key)  # ensure keys are ints
                        total_counts[key] = total_counts.get(key, 0) + value
                except json.JSONDecodeError as e:
                    logging.error(f"Failed to parse label size JSON: {e}")
            props[name] = total_counts
        self.add_function(sum_label_size_dicts, file=file)

    def add_indexed_pattern_mapping(
        self, regex, file="run.log", type=int, flags="", name_mapping=None
    ):
        def extract_and_map(content, props):
            matches = list(re.finditer(regex, content, flags=self._get_flags(flags)))
            for index, prop_name in name_mapping:
                try:
                    match = matches[index]
                    props[prop_name] = type(match.group(1))
                except IndexError:
                    # If there are not enough matches, skip this one
                    logging.warning(
                        f"Not enough matches for pattern '{regex}' to extract index {index} ({prop_name})"
                    )

        self.add_function(extract_and_map, file=file)
    def add_indexed_json_pattern_mapping(
        self, regex, file="run.log", flags="", name_mapping=None
    ):
        def extract_and_map_json(content, props):
            matches = list(re.finditer(regex, content, flags=self._get_flags(flags)))
            for index, prop_name in name_mapping:
                try:
                    match = matches[index]
                    parsed = json.loads(match.group(1))
                    props[prop_name] = parsed
                except IndexError:
                    logging.warning(
                        f"Not enough matches for pattern '{regex}' to extract index {index} ({prop_name})"
                    )
                except json.JSONDecodeError as e:
                    logging.error(f"Failed to parse JSON for pattern '{regex}': {e}")

        self.add_function(extract_and_map_json, file=file)

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
