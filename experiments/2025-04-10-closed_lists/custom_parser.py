

import logging
import re

from lab.parser import Parser


def retrieve_avg_num_var(content, props):   
    if "memory_error" in props:
        return

    if "num_slots" in props:
        props["avg_num_var"] = float((props["num_slots"] * 2)) / props["registered_states"]

    elif "num_variables" in props:
        props["avg_num_var"] = float(props["num_variables"])


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
        "num_slots",
        r"\[t=.+s, \d+ KB\] FixedHashSet destroyed, size: (\d+) entries",
        type=int)
    parser.add_pattern(
        "num_variables",
        r"\[t=.+s, \d+ KB\] Variables: (\d+)",
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

