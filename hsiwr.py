#! /usr/bin/env python3
import re
import argparse
import subprocess
import os
from pathlib import Path


def make_callstring(path: Path):
    subpaths = []
    rule_file = None
    sketch_file = None
    for subpath in path.iterdir():
        if subpath.is_dir():
            subpaths.append(subpath)
        elif subpath.is_file():
            name = subpath.name
            if name.startswith("rule_"):
                rule_file = name
            elif name.startswith("sketch_"):
                sketch_file = name
            else:
                raise Exception()
    if sketch_file is not None:
        # Nondeterminstic decomposition
        if subpaths:
            callstring = "parallelized_search(child_searches=["
            for subpath in subpaths:
                callstring += make_callstring(subpath) + ","
            callstring += "], goal_test="
            if rule_file:
                callstring += f"sketch_subgoal(filename={str(path / rule_file)})"
            else:
                callstring += f"top_goal()"
            callstring += ")"
            return callstring
        else:
            sketch_width = int(re.findall(r"sketch_(\d+).txt", sketch_file)[0][0])
            if rule_file:
                return f"serialized_search(child_searches=[iw(width={sketch_width},goal_test=sketch_subgoal(filename={str(path / sketch_file)}))], goal_test=sketch_subgoal(filename={str(path / rule_file)}))"
            else:
                return f"serialized_search(child_searches=[iw(width={sketch_width},goal_test=sketch_subgoal(filename={str(path / sketch_file)}))], goal_test=top_goal())"
    else:
        # Lower level behavior requires search
        if rule_file:
            rule_width = int(re.findall(r"rule_(\d+).txt", rule_file)[0][0])
            return f"iw(width={rule_width}, goal_test=sketch_subgoal(filename={str(path / rule_file)}))"
        else:
            Exception()
    Exception()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Hierarchical Serialized Iterated Width With Sketches (HSIWR) Algorithm')
    parser.add_argument("--fd_file", type=str, default="./fast-downward.py")
    parser.add_argument("--domain_file", type=str, required=True)
    parser.add_argument("--instance_file", type=str, required=True)
    parser.add_argument("--hierarchical_sketch_dir", type=str, required=True)
    parser.add_argument("--plan_file", type=str, required=True)
    args = parser.parse_args()
    search_string = make_callstring(Path(args.hierarchical_sketch_dir).resolve())
    print(search_string)
    command = [
        Path(args.fd_file).resolve(),
        "--keep-sas-file",
        "--plan-file",
        Path(args.plan_file).resolve(),
        Path(args.domain_file).resolve(),
        Path(args.instance_file).resolve(),
        "--translate-options",
        "--dump-static-predicates",
        "--dump-predicates",
        "--dump-constants",
        "--dump-static-atoms",
        "--dump-goal-atoms",
        "--search-options",
        "--search",
        search_string]
    print(f'Executing "{" ".join(map(str, command))}"')
    subprocess.run(command)
