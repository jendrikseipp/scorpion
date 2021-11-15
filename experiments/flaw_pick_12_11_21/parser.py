#! /usr/bin/env python

import logging
import re
import sys

from lab.parser import Parser

class CustomParser(Parser):
    def __init__(self):
        Parser.__init__(self)
        #self.add_pattern("cartesian_states", r"^Cartesian states: (\d+)\n", type=int)

def add_found_concret_solution(content, props):
    props["conrete_solution"] = int("Found concrete solution." in content)

def add_search_time_if_cs(content, props):
    if props["conrete_solution"] == 1:
        props["search_time_if_cs"] = props["search_time"]
    else:
        props["search_time_if_cs"] = None

def add_cartesian_states_if_cs(content, props):
    if props["conrete_solution"] == 1:
        props["cartesian_states_if_cs"] = props["cartesian_states"]
    else:
        props["cartesian_states_if_cs"] = None

def no_search(content, props):
    if "search_start_time" not in props:
        error = props.get("error")
        if error is not None and error != "incomplete-search-found-no-plan":
            props["error"] = "no-search-due-to-" + error

#def add_flaw_find_time_ratio(content, props):
#    if props["conrete_solution"] == 1:
#        props["flaw_find_time_ratio"] = props["flaw_find_time"] / props["additive_cartesian_heuristic_build_time"]
#    else:
#        props["flaw_find_time_ratio"] = None

def main():
    parser = CustomParser()
    parser.add_pattern("num_flaw_searches", r"\] #Flaw searches: (.+)\n", type=int)
    parser.add_pattern("num_flaw_refinmentss", r"\] #Flaws refined: (.+)\n", type=int)
    parser.add_pattern("num_state_expansions_in_flaw_search", r"\] Expanded concrete states: (.+)\n", type=int)
    parser.add_pattern("cartesian_states", r"\] Cartesian states: (.+)\n", type=int)
    parser.add_pattern("flaw_find_time", r"\] Time for finding flaws: (.+)s\n", type=float)
    parser.add_pattern("additive_cartesian_heuristic_build_time", r"\] Time for initializing additive Cartesian heuristic: (.+)s\n", type=float)

    parser.add_pattern("search_start_time", r"\[t=(.+)s, \d+ KB\] g=0, 1 evaluated, 0 expanded", type=float)
    parser.add_pattern("search_start_memory", r"\[t=.+s, (\d+) KB\] g=0, 1 evaluated, 0 expanded", type=int)
    
    parser.add_function(add_found_concret_solution)
    parser.add_function(add_cartesian_states_if_cs)
    parser.add_function(add_search_time_if_cs)
    parser.add_function(no_search)
    
    parser.parse()


if __name__ == "__main__":
    main()
