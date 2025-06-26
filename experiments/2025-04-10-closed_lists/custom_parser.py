

import logging
import re

from lab.parser import Parser


def retrieve_avg_num_var(content, props):   
    if "memory_error" in props:
        return

    if "state_set_size" in props and "size_per_entry" in props:
        props["avg_edges_per_state"] = float((props["state_set_size"] * props["size_per_entry"])) / props["registered_states"]

    if "root_table_size" in props:
        pass

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


def get_parser():
    parser = CommonParser()
    parser.add_pattern(
        "state_set_size",
        r"\[t=.+s, \d+ KB\] State set destroyed, size: (\d+) entries",
        type=int)
    parser.add_pattern(
        "size_per_entry",
        r"\[t=.+s, \d+ KB\] State set destroyed, size per entry: (\d+) blocks",
        type=int)
    parser.add_pattern(
        "state_set_occupied_tree",
        r"\[t=.+s, \d+ KB\] State set destroyed, byte size: (\d+\.\d+)MB",
        type=float)
    parser.add_pattern(
        "state_set_allocated_tree",
        r"\[t=.+s, \d+ KB\] State set destroyed, byte capacity: (\d+\.\d+)MB",
        type=float)
    parser.add_pattern(
        "num_atoms",
        r"Translator variables: (\d+)",
        type=int)
    parser.add_pattern(
        "registered_states",
        r"\[t=.+s, \d+ KB\] Number of registered states: (\d+)",
        type=int)
    parser.add_pattern(
        "memory_error",
        r"(Failed to allocate memory)",
        type=bool,
    )

    parser.add_function(retrieve_avg_num_var)
    return parser

