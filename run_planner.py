#! /usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

CWD = Path.cwd()
print(CWD)

def make_hierarchical_callstring(path):
    subpaths = []
    file = None
    for name in path.iterdir():
        if name.is_dir():
            subpaths.append(path / name)
        elif name.is_file():
            file = path / name
    assert file is not None
    if len(subpaths) > 1:
        # intantiate parallel search
        return "parallelized_search(child_searches=[" + ",".join([make_hierarchical_callstring(subpath) for subpath in subpaths]) + "], goal_test=sketch_subgoal(filename=" + str(file) + "))"
    elif len(subpaths) == 1:
        # instantiate serialized search
        return "serialized_search(child_searches=[" + make_hierarchical_callstring(subpaths[0]) + "], goal_test=sketch_subgoal(filename=" + str(file) + "))"
    elif len(subpaths) == 0:
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
    parser.add_argument("--fd_file", type=str, default="./fast-downward.py")
    parser.add_argument("--domain_file", type=str, required=True)
    parser.add_argument("--instance_file", type=str, required=True)
    parser.add_argument("--hierarchical_sketch_dir", type=str, required=True)
    parser.add_argument("--plan-file", type=str, required=True)
    args = parser.parse_args()
    command = [
        "./fast-downward.py",
        "--keep-sas-file",
        "--plan-file",
        args.plan_file,
        args.domain_file,
        args.instance_file,
        "--translate-options",
        "--dump-predicates",
        "--dump-constants",
        "--dump-static-atoms",
        "--dump-goal-atoms",
        "--search-options",
        "--search",
        generate_search_string(Path(args.hierarchical_sketch_dir).resolve())]
    print(f'Executing "{" ".join(map(str, command))}"')
    subprocess.run(command)
