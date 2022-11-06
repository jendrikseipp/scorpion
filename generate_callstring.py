#! /usr/bin/env python
import argparse
import os

from pathlib import Path

DIR = Path(__file__).resolve().parent

def make_hierarchical_callstring(directory):
    subdirs = []
    file = None
    for path in directory.iterdir():
        if path.is_dir():
            subdirs.append(path)
        elif path.is_file():
            file = path
    assert file is not None
    if len(subdirs) > 1:
        # intantiate parallel search
        return "parallelized_search(child_searches=[" + ",".join([make_hierarchical_callstring(subdir) for subdir in subdirs]) + "], goal_test=sketch_subgoal(filename=" + str(DIR / file) + "))"
    elif len(subdirs) == 1:
        # instantiate serialized search
        return "serialized_search(child_searches=[" + make_hierarchical_callstring(subdirs[0]) + "], goal_test=sketch_subgoal(filename=" + str(DIR / file) + "))"
    elif len(subdirs) == 0:
        return "iw(width=0, goal_test=sketch_subgoal(filename=" + str(DIR / file) + "))"
    else:
        raise Exception()


def generate_search_string(root_directory):
    callstring = "serialized_search(child_searches=["
    callstring += make_hierarchical_callstring(root_directory)
    callstring += "], goal_test=top_goal())"
    return callstring

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Parses a directory of hierarchical policies into a callstring')
    parser.add_argument("--root_directory", type=str, required=True)
    args = parser.parse_args()
    print(generate_search_string(Path(args.root_directory)))