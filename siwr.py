#! /usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def generate_search_string(sketch_file, width):
    callstring = "serialized_search(child_searches=["
    callstring += "iw(width=" + str(width) + ", goal_test=sketch_subgoal(filename=" + str(sketch_file) + "))"
    callstring += "], goal_test=top_goal())"
    return callstring


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Serialized Iterated Width With Sketches (SIWR) Algorithm')
    parser.add_argument("--fd_file", type=str, default="./fast-downward.py")
    parser.add_argument("--domain_file", type=str, required=True)
    parser.add_argument("--instance_file", type=str, required=True)
    parser.add_argument("--sketch_file", type=str, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--plan_file", type=str, required=True)
    args = parser.parse_args()
    search_string = generate_search_string(Path(args.sketch_file).resolve(), args.width)
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
