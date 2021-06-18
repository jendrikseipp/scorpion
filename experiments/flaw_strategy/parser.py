#! /usr/bin/env python

import logging
import re
import sys

from lab.parser import Parser

def add_found_concret_solution(content, props):
    props["conrete_solution"] = int("Found concrete solution for subtask" in content)

def add_cartesian_states_to_solve(content, props):
    if props["conrete_solution"] == 1:
        props["cartesian_states_to_solve"] = props["cartesian_states"]
    else:
        props["cartesian_states_to_solve"] = None

def main():
    parser = CommonParser()
    parser.add_pattern("cartesian_states", r"^Cartesian states: (\d+)\n", type=int)
    parser.add_function(add_found_concret_solution)
    parser.add_function(add_cartesian_states_to_solve)


    parser.parse()


if __name__ == "__main__":
    main()
