#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import re
import resource
import subprocess
import sys


TASKS = [
    "agricola/p15.pddl",
    "airport/p04.pddl",
    "barman/p06.pddl",
    "blocksworld/p13.pddl",
    "childsnack/p22.pddl",
    "data-network/p29.pddl",
    "depots/p09.pddl",
    "driverlog/p12.pddl",
    "elevators/p19.pddl",
    "floortile/p13.pddl",
    "freecell/p28.pddl",
    "ged/p22.pddl",
    "grid/p05.pddl",
    "gripper/p30.pddl",
    "hiking/p25.pddl",
    "logistics/p06.pddl",
    "miconic/p30.pddl",
    "mprime/p25.pddl",
    "nomystery/p10.pddl",
    "openstacks/p20.pddl",
    "organic-synthesis-split/p21.pddl",
    "parcprinter/p30.pddl",
    "parking/p07.pddl",
    "pathways/p28.pddl",
    "pegsol/p12.pddl",
    "pipesworld-notankage/p23.pddl",
    "pipesworld-tankage/p30.pddl",
    "rovers/p23.pddl",
    "satellite/p08.pddl",
    "scanalyzer/p11.pddl",
    "snake/p10.pddl",
    "sokoban/p21.pddl",
    "storage/p07.pddl",
    "termes/p16.pddl",
    "tetris/p18.pddl",
    "thoughtful/p15.pddl",
    "tidybot/p08.pddl",
    "tpp/p10.pddl",
    "transport/p11.pddl",
    "visitall/p11.pddl",
    "woodworking/p05.pddl",
    "zenotravel/p06.pddl",
]

PLANNER_TIME_RE = re.compile(r"^INFO\s+Planner time: ([0-9]+(?:\.[0-9]+)?)s$", re.MULTILINE)

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DRIVER = REPO_ROOT / "fast-downward.py"
PREPROCESSOR = REPO_ROOT / "builds/release/bin/preprocess-h2"
TRANSLATE_OUTPUT_DIR = REPO_ROOT / "misc/tests/.preprocessing-speed/translated"
PREPROCESS_OUTPUT_DIR = REPO_ROOT / "misc/tests/.preprocessing-speed/preprocessed"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["translate", "preprocess"])
    return parser.parse_args()


def get_benchmarks_dir() -> Path:
    try:
        return Path(os.environ["AUTOSCALE_BENCHMARKS_SAT"]).resolve()
    except KeyError:
        sys.exit("AUTOSCALE_BENCHMARKS_SAT must point to the autoscale benchmarks")


def task_to_output_path(output_dir: Path, task: str) -> Path:
    return output_dir / Path(task).with_suffix(".sas")


def parse_planner_time(output: str) -> float:
    match = PLANNER_TIME_RE.search(output)
    if not match:
        raise ValueError("Could not parse planner time from output")
    return float(match.group(1))


def run_driver_command(cmd: list[str]) -> float:
    result = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
    )

    output = result.stdout + result.stderr
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        raise subprocess.CalledProcessError(result.returncode, cmd)

    return parse_planner_time(output)


def run_preprocessor(input_file: Path, output_file: Path) -> float:
    if not PREPROCESSOR.exists():
        sys.exit(f"Missing preprocessor executable: {PREPROCESSOR}")

    with input_file.open("rb") as infile:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run(
            [str(PREPROCESSOR), "--outfile", str(output_file)],
            cwd=REPO_ROOT,
            stdin=infile,
            capture_output=True,
        )
        after = resource.getrusage(resource.RUSAGE_CHILDREN)

    if result.returncode != 0:
        if result.stdout:
            print(result.stdout.decode(), end="")
        if result.stderr:
            print(result.stderr.decode(), end="", file=sys.stderr)
        raise subprocess.CalledProcessError(result.returncode, result.args)

    return (after.ru_utime + after.ru_stime) - (before.ru_utime + before.ru_stime)


def translate_all(benchmarks_dir: Path) -> None:
    TRANSLATE_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    total_cpu = 0.0

    for task in TASKS:
        print(f"=== Translating {task} ===")
        output_file = task_to_output_path(TRANSLATE_OUTPUT_DIR, task)
        output_file.parent.mkdir(parents=True, exist_ok=True)
        output_file.unlink(missing_ok=True)

        task_cpu = run_driver_command([
            sys.executable,
            str(DRIVER),
            "--translate",
            "--sas-file",
            str(output_file),
            str(benchmarks_dir / task),
        ])
        if not output_file.exists():
            sys.exit(f"Translator did not create {output_file}")

        total_cpu += task_cpu
        print(f"Translator CPU time: {task_cpu:.2f}s")

    print()
    print(f"Total translator CPU time: {total_cpu:.2f}s")


def preprocess_all() -> None:
    PREPROCESS_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    total_cpu = 0.0

    for task in TASKS:
        print(f"=== Preprocessing {task} ===")
        input_file = task_to_output_path(TRANSLATE_OUTPUT_DIR, task)
        output_file = task_to_output_path(PREPROCESS_OUTPUT_DIR, task)

        if not input_file.exists():
            sys.exit(f"Missing translator output: {input_file}\nRun '{Path(__file__).name} translate' first.")

        output_file.parent.mkdir(parents=True, exist_ok=True)
        output_file.unlink(missing_ok=True)

        task_cpu = run_preprocessor(input_file, output_file)
        if not output_file.exists():
            sys.exit(f"Preprocessor did not create {output_file}")

        total_cpu += task_cpu
        print(f"Preprocessor CPU time: {task_cpu:.2f}s")

    print()
    print(f"Total preprocessor CPU time: {total_cpu:.2f}s")


def main() -> None:
    args = parse_args()

    if args.mode == "translate":
        benchmarks_dir = get_benchmarks_dir()
        translate_all(benchmarks_dir)
    else:
        preprocess_all()


if __name__ == "__main__":
    main()
