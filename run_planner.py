#! /usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def make_hierarchical_callstring(directory):
    subdirs = []
    file = None
    print(directory)
    for path in directory.iterdir():
        if path.is_dir():
            subdirs.append(path)
        elif path.is_file():
            file = path
    assert file is not None
    if len(subdirs) > 1:
        # intantiate parallel search
        return "parallelized_search(child_searches=[" + ",".join([make_hierarchical_callstring(subdir) for subdir in subdirs]) + "], goal_test=sketch_subgoal(filename=" + str(file) + "))"
    elif len(subdirs) == 1:
        # instantiate serialized search
        return "serialized_search(child_searches=[" + make_hierarchical_callstring(subdirs[0]) + "], goal_test=sketch_subgoal(filename=" + str(file) + "))"
    elif len(subdirs) == 0:
        return "iw(width=0, goal_test=sketch_subgoal(filename=" + str(file) + "))"
    else:
        raise Exception()


def generate_search_string(root_directory):
    callstring = "serialized_search(child_searches=["
    callstring += make_hierarchical_callstring(root_directory)
    callstring += "], goal_test=top_goal())"
    return callstring


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Parses a directory of hierarchical policies into a callstring')
    parser.add_argument("--domain_file", type=str, required=True)
    parser.add_argument("--instance_file", type=str, required=True)
    parser.add_argument("--hierarchical_sketch_dir", type=str, required=True)
    args = parser.parse_args()
    subprocess.run([
        "./fast-downward.py",
        "--keep-sas-file",
        "--build=release",
        str(args.domain_file),
        str(args.instance_file),
        "--translate-options",
        "--dump-predicates",
        "--dump-constants",
        "--dump-static-atoms",
        "--dump-goal-atoms",
        "--search-options",
        "--search",
        generate_search_string(Path(args.hierarchical_sketch_dir))])