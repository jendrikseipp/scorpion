#! /usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

from generate_callstring import generate_search_string

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Parses a directory of hierarchical policies into a callstring')
    parser.add_argument("--domain_file", type=str, required=True)
    parser.add_argument("--instance_file", type=str, required=True)
    parser.add_argument("--hierarchical_sketch_dir", type=str, required=True)
    args = parser.parse_args()
    print(generate_search_string(Path(args.hierarchical_sketch_dir)))
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