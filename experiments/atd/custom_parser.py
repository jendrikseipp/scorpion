#! /usr/bin/env python
import json
from lab.parser import Parser
import re
import logging


class CommonParser(Parser):
    def __init__(self):
        super().__init__()

        self.num_labels_values = []
        self.label_size_counts_values = []
        self.reused_label_size_counts_values = []

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
            r"Total number of single transitions in Abstractions: (\d+)",
            type=int,
            name_mapping=[
                (0, "cartesian1_num_single_transitions"),
                (1, "cartesian2_num_single_transitions"),
                (-1, "projection_num_single_transitions"),
            ]
        )
        self.add_indexed_pattern_mapping(
            r"Total number of reused labels in Abstractions: (\d+)",
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
        self.add_sum_pattern ( 
            "num_single_transitions", r"Total number of single transitions in Abstractions: (\d+)", type=int
        )
        self.add_pattern(None, r"Total number of labels in Abstractions: (\d+)", self.handle_num_labels)
        self.add_sum_pattern ( 
            "num_reused_labels", r"Total number of reused labels in Abstractions: (\d+)", type=int
        )
        # self.add_bottom_up_pattern ( 
        #     "change_in_size", r"Total change in transitions \(\(#single transitions\+#labels\)/#transitions\): ([\d\.]+)", type=float
        # )
        self.add_function(self.extract_label_size_counts_json)
        self.add_function(self.extract_reused_label_size_counts_json)
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

     ### --- num_labels logic ---
    def handle_num_labels(self, match, props):
        value = int(match.group(1))
        self.num_labels_values.append(value)

    ### --- label size counts ---
    def extract_label_size_counts_json(self, content, props):
        matches = list(re.finditer(r'Label size counts: (\{.*?\})', content))
        for match in matches:
            try:
                parsed = json.loads(match.group(1))
                self.label_size_counts_values.append(parsed)
            except json.JSONDecodeError as e:
                logging.error(f"Failed to parse label size JSON: {e}")

    ### --- reused label size counts ---
    def extract_reused_label_size_counts_json(self, content, props):
        matches = list(re.finditer(r'Reused label size counts: (\{.*?\})', content))
        for match in matches:
            try:
                parsed = json.loads(match.group(1))
                self.reused_label_size_counts_values.append(parsed)
            except json.JSONDecodeError as e:
                logging.error(f"Failed to parse reused label size JSON: {e}")

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

    def finalize(self, props):
        # Handle num_labels
        values = self.num_labels_values
        if len(values) >= 1:
            props["num_labels_cartesian1"] = values[0]
        if len(values) >= 2:
            props["num_labels_cartesian2"] = values[1] - values[0]
        if len(values) >= 3:
            props["num_labels_projection"] = values[2] - values[1]
        if values:
            props["num_labels"] = values[-1]  # Total value

        # Handle label_size_counts → per phase
        size_values = self.label_size_counts_values
        if len(size_values) >= 1:
            for k, v in size_values[0].items():
                props[f"cartesian1_label_size_{k}"] = v
        if len(size_values) >= 2:
            for k in size_values[1].keys():
                prev = size_values[0].get(k, 0)
                diff = size_values[1][k] - prev
                props[f"cartesian2_label_size_{k}"] = diff
        if len(size_values) >= 3:
            for k in size_values[2].keys():
                prev = size_values[1].get(k, 0)
                diff = size_values[2][k] - prev
                props[f"projection_label_size_{k}"] = diff
        if size_values:
            last_values = size_values[-1]
            for k, v in last_values.items():
                props[f"label_size_{k}"] = v

        # Handle reused_label_size_counts → per phase
        reused_values = self.reused_label_size_counts_values
        if len(reused_values) >= 1:
            for k, v in reused_values[0].items():
                props[f"cartesian1_reused_label_size_{k}"] = v
        if len(reused_values) >= 2:
            for k in reused_values[1].keys():
                prev = reused_values[0].get(k, 0)
                diff = reused_values[1][k] - prev
                props[f"cartesian2_reused_label_size_{k}"] = diff
        if len(reused_values) >= 3:
            for k in reused_values[2].keys():
                prev = reused_values[1].get(k, 0)
                diff = reused_values[2][k] - prev
                props[f"projection_reused_label_size_{k}"] = diff
        if reused_values:
            last_values = reused_values[-1]
            for k, v in last_values.items():
                props[f"reused_label_size_{k}"] = v

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
