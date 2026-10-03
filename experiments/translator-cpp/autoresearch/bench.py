#!/usr/bin/env python3
"""Serial, CPU-pinned translator probes and paired A/B measurements.

Suite JSON accepts ["domain:problem.pddl", ...], explicit objects with
{name, domain, problem, options}, or {"tasks": [...]}. Repetitions count
four-run ABBA/BAAB blocks, giving two measurements per variant per block.
All outputs, including failed runs, remain in the result directory.
"""

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import resource
import shutil
import signal
import statistics
import subprocess
import sys
import time


METRICS = ("cpu_s", "wall_s", "rss_kb")
DEFAULT_REPO = Path(__file__).resolve().parents[3]


def read_resolver(repo):
    path = repo / "misc/tests/test-translator-equivalence.py"
    spec = importlib.util.spec_from_file_location("translator_equivalence", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # Use the driver's naming rules; the older test helper misses Airport's
    # per-instance pNN-domain.pddl files and silently picks the wrong domain.
    def resolve_domain(problem):
        names = ["domain.pddl", problem.stem + "-domain" + problem.suffix,
                 problem.name[:3] + "-domain.pddl", "domain_" + problem.name,
                 "domain-" + problem.name]
        return next((problem.parent / name for name in names
                     if (problem.parent / name).is_file()), None)
    module.resolve_domain = resolve_domain
    return module


def load_tasks(args, resolver):
    entries = []
    if args.suite:
        entries = json.loads(args.suite.read_text())
        if isinstance(entries, dict):
            entries = entries["tasks"]
        if not isinstance(entries, list):
            raise ValueError("suite JSON must contain a list of tasks")
    entries.extend(args.tasks)
    for domain in args.domains:
        directory = args.benchmarks / domain
        problems = sorted(p for p in directory.glob("*.pddl")
                          if not resolver.is_domain_file(p))
        if not problems:
            raise ValueError(f"no benchmark problems in {directory}")
        indices = sorted(set((0, len(problems) // 2, len(problems) - 1)))
        entries.extend(f"{domain}:{problems[i].name}" for i in indices)
    if args.regression:
        entries.extend({"task": task, "config": label, "options": options}
                       for task in resolver.OPTION_TASKS
                       for label, options in resolver.CONFIGS)
    tasks = []
    seen = set()
    for entry in entries:
        if isinstance(entry, str):
            entry = {"task": entry}
        label = entry.get("task", entry.get("name"))
        if "problem" in entry:
            problem = Path(entry["problem"])
            if not problem.is_absolute():
                problem = args.benchmarks / problem
        elif label:
            problem = args.benchmarks / label.replace(":", "/", 1)
        else:
            raise ValueError(f"task has no name or problem: {entry}")
        problem = problem.resolve()
        domain = Path(entry["domain"]).resolve() if "domain" in entry else resolver.resolve_domain(problem)
        if domain is None or not domain.is_file() or not problem.is_file():
            raise ValueError(f"could not resolve existing domain/problem for {entry}")
        options = list(entry.get("options", [])) + args.option
        if not all(isinstance(option, str) for option in options):
            raise ValueError("translator options must be strings")
        key = (str(domain), str(problem), tuple(options))
        if key in seen:
            continue
        seen.add(key)
        tasks.append({"name": label or f"{problem.parent.name}:{problem.name}",
                      "config": entry.get("config", "default"),
                      "domain": str(domain), "problem": str(problem),
                      "options": options})
    if not tasks:
        raise ValueError("select tasks with --suite, --tasks, --domains, or --regression")
    return tasks


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def append_json(path, record):
    with path.open("a") as stream:
        stream.write(json.dumps(record, sort_keys=True) + "\n")
        stream.flush()


def run_one(args, task, binary, variant, directory, index, block):
    directory.mkdir()
    log_path = directory / "translator.log"
    timing_path = directory / "time.txt"
    command = [str(binary), *task["options"], task["domain"], task["problem"]]
    measured = [str(args.time_binary), "-f", "%e %U %S %M %x", "-o",
                str(timing_path), "--", *command]

    def child_setup():
        os.sched_setaffinity(0, {args.cpu})
        memory = args.memory_mib * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (memory, memory))
        cpu_limit = math.ceil(args.timeout)
        resource.setrlimit(resource.RLIMIT_CPU, (cpu_limit, cpu_limit + 2))

    start = time.perf_counter()
    timed_out = False
    with log_path.open("wb") as log:
        process = subprocess.Popen(measured, cwd=directory, stdout=log, stderr=log,
                                   start_new_session=True, preexec_fn=child_setup)
        try:
            rc = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            # Give GNU time a chance to collect child metrics before killing
            # the whole group, including any descendants of the translator.
            try:
                children = Path(f"/proc/{process.pid}/task/{process.pid}/children").read_text().split()
                for child in children:
                    try:
                        os.kill(int(child), signal.SIGTERM)
                    except ProcessLookupError:
                        pass
            except FileNotFoundError:
                pass
            try:
                rc = process.wait(timeout=0.25)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                rc = process.wait()
    actual_wall = time.perf_counter() - start
    record = {"type": "run", "task": task["name"], "config": task["config"],
              "options": task["options"], "variant": variant, "block": block,
              "index": index, "binary": str(binary), "command": command,
              "directory": str(directory), "rc": rc, "timeout": timed_out,
              "observed_wall_s": actual_wall, "cpu_s": None, "wall_s": None,
              "rss_kb": None, "sha256": None, "output_bytes": 0}
    if timing_path.exists():
        # Signal diagnostics may precede the five numeric fields.
        for line in reversed(timing_path.read_text().splitlines()):
            fields = line.split()
            if len(fields) != 5:
                continue
            try:
                wall, user, system = map(float, fields[:3])
                rss, measured_rc = map(int, fields[3:])
            except ValueError:
                continue
            record.update(wall_s=wall, cpu_s=user + system, user_s=user,
                          system_s=system, rss_kb=rss, measured_rc=measured_rc)
            break
    output = directory / "output.sas"
    if output.is_file():
        record["sha256"] = file_sha256(output)
        record["output_bytes"] = output.stat().st_size
    contents = log_path.read_text(errors="replace")
    # A wall-clock cut-off can produce different invariant sets even when
    # translation finishes. Such output cannot support deterministic A/B tests.
    record["invariant_time_limit_reached"] = (
        "Time limit reached, aborting invariant generation" in contents)
    record["phases"] = {
        match.group(1): {"cpu_s": float(match.group(2)), "wall_s": float(match.group(3))}
        for match in re.finditer(r"^(.+?): \[([0-9.]+)s CPU, ([0-9.]+)s wall-clock\]$", contents, re.MULTILINE)
    }
    record["success"] = (rc == 0 and not timed_out
                         and not record["invariant_time_limit_reached"]
                         and record["sha256"] is not None)
    append_json(args.out / "runs.jsonl", record)
    return record


def make_pair(a, b, task_index, expected_sha):
    errors = []
    for record in (a, b):
        if not record["success"]:
            errors.append(f"{record['variant']} failed (rc={record['rc']}, timeout={record['timeout']}, SAS={record['sha256'] is not None})")
            if record.get("invariant_time_limit_reached"):
                errors.append(f"{record['variant']} reached the nondeterministic invariant-generation time limit")
        elif expected_sha is not None and record["sha256"] != expected_sha:
            errors.append(f"{record['variant']} differs from this task's first successful baseline output")
    if a["rc"] != b["rc"]:
        errors.append("return codes differ")
    if a["sha256"] != b["sha256"]:
        errors.append("SAS SHA256 differs")
    ratios = {}
    if not errors:
        for metric in METRICS:
            if a[metric] is None or b[metric] is None or a[metric] <= 0 or b[metric] <= 0:
                errors.append(f"{metric} measurement missing or below external timer resolution")
            else:
                ratios[metric] = b[metric] / a[metric]
    return {"type": "pair", "task": a["task"], "config": a["config"],
            "task_index": task_index, "block": a["block"], "A_index": a["index"],
            "B_index": b["index"], "success": not errors, "errors": errors,
            "ratios": ratios, "output_equal": bool(a["success"] and b["success"]
                                                       and a["sha256"] == b["sha256"])}


def geometric_mean(values):
    return math.exp(statistics.fmean(math.log(value) for value in values))


def confidence_intervals(groups, bootstrap, seed):
    rng = random.Random(seed)
    samples = {metric: [] for metric in METRICS}
    for _ in range(bootstrap):
        # Resample paired measurements within each task and weight each task
        # equally, so many repeats of a tiny task cannot dominate the suite.
        chosen = [rng.choices(group, k=len(group)) for group in groups]
        for metric in METRICS:
            samples[metric].append(math.exp(statistics.fmean(
                statistics.fmean(math.log(pair["ratios"][metric]) for pair in group)
                for group in chosen)))
    intervals = {}
    for metric, values in samples.items():
        values.sort()
        if values:
            intervals[metric] = [values[int(0.025 * (len(values) - 1))],
                                 values[int(0.975 * (len(values) - 1))]]
    return intervals


def summarize_ab(tasks, pairs, args):
    summaries = []
    groups = []
    for task_index, task in enumerate(tasks):
        selected = [pair for pair in pairs if pair["task_index"] == task_index]
        valid = [pair for pair in selected if pair["success"]]
        # Never report a partially failing task as a successful measurement.
        successful = bool(selected) and len(valid) == len(selected)
        ratios = {metric: geometric_mean(pair["ratios"][metric] for pair in valid)
                  for metric in METRICS} if successful else {}
        summaries.append({**task, "success": successful,
                          "pairs": len(selected), "valid_pairs": len(valid),
                          "ratios": ratios,
                          "errors": [error for pair in selected for error in pair["errors"]]})
        if successful:
            groups.append(valid)
    aggregate = {metric: geometric_mean(task["ratios"][metric] for task in summaries if task["success"])
                 for metric in METRICS} if groups else {}
    return {"mode": args.mode, "success": all(task["success"] for task in summaries),
            "ratio_direction": "B / A; below 1 improves",
            "tasks": summaries, "aggregate_ratios": aggregate,
            "aggregate_valid_tasks": len(groups), "aggregate_total_tasks": len(tasks),
            "paired_bootstrap_ci95": confidence_intervals(groups, args.bootstrap, args.seed) if groups else {},
            "bootstrap_note": "Within-task paired resampling; equal task weights. Failed tasks excluded from ratios but fail the suite."}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=("probe", "ab", "validate"))
    parser.add_argument("--repo", type=Path, default=DEFAULT_REPO)
    parser.add_argument("--benchmarks", type=Path,
                        default=Path(os.environ.get("DOWNWARD_BENCHMARKS",
                                                    str(DEFAULT_REPO.parent / "downward-benchmarks"))))
    parser.add_argument("--suite", type=Path)
    parser.add_argument("--tasks", nargs="*", default=[])
    parser.add_argument("--domains", nargs="*", default=[], help="probe first, median and last filename per domain")
    parser.add_argument("--regression", action="store_true", help="all maintained OPTION_TASKS × CONFIGS")
    parser.add_argument("--option", action="append", default=[], help="extra translator argument; use --option=--flag")
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--time-binary", type=Path,
                        default=Path(shutil.which("time") or "/usr/bin/time"),
                        help="GNU time executable (must support -f and -o)")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--memory-mib", type=int, default=6144)
    parser.add_argument("--repetitions", "--blocks", type=int, default=3, help="ABBA/BAAB blocks per task (two runs of each variant per block)")
    parser.add_argument("--bootstrap", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=20261003)
    parser.add_argument("--quiet", action="store_true",
                        help="suppress routine run output; print failures and final summary")
    args = parser.parse_args()
    if args.mode != "probe" and args.candidate is None:
        parser.error("--candidate is required for ab and validate")
    if args.cpu not in os.sched_getaffinity(0):
        parser.error(f"CPU {args.cpu} is unavailable; allowed: {sorted(os.sched_getaffinity(0))}")
    if args.timeout <= 0 or args.memory_mib <= 0 or args.repetitions <= 0 or args.bootstrap < 0:
        parser.error("timeout, memory and repetitions must be positive; bootstrap must be nonnegative")
    args.repo = args.repo.resolve()
    args.benchmarks = args.benchmarks.resolve()
    args.baseline = args.baseline.resolve()
    args.candidate = args.candidate.resolve() if args.candidate else None
    args.time_binary = args.time_binary.resolve()
    for binary in (args.baseline, args.candidate, args.time_binary):
        if binary is not None and (not binary.is_file() or not os.access(binary, os.X_OK)):
            parser.error(f"binary does not exist or is not executable: {binary}")
    try:
        tasks = load_tasks(args, read_resolver(args.repo))
    except (ValueError, KeyError, OSError) as error:
        parser.error(str(error))
    args.out = args.out.resolve()
    if args.out.exists() and any(args.out.iterdir()):
        parser.error(f"result directory must be empty to avoid mixing campaigns: {args.out}")
    args.out.mkdir(parents=True, exist_ok=True)
    metadata = {"argv": sys.argv, "cpu": args.cpu, "timeout_s": args.timeout,
                "memory_mib": args.memory_mib, "tasks": tasks,
                "time_binary": str(args.time_binary),
                "baseline_sha256": file_sha256(args.baseline),
                "candidate_sha256": file_sha256(args.candidate) if args.candidate else None,
                "repetitions": args.repetitions, "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    (args.out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    runs = []
    pairs = []
    index = 0
    for task_index, task in enumerate(tasks):
        expected_sha = None
        blocks = args.repetitions if args.mode == "ab" else 1
        for block in range(blocks):
            sequence = "A" if args.mode == "probe" else ("AB" if args.mode == "validate" else ("ABBA" if (task_index + block) % 2 == 0 else "BAAB"))
            block_runs = []
            for variant in sequence:
                binary = args.baseline if variant == "A" else args.candidate
                directory = args.out / f"run-{index:05d}-task-{task_index:03d}-{variant}"
                record = run_one(args, task, binary, variant, directory, index, block)
                runs.append(record)
                block_runs.append(record)
                if variant == "A" and record["success"] and expected_sha is None:
                    expected_sha = record["sha256"]
                if not args.quiet or not record["success"]:
                    print(f"{index:05d} {variant} {task['name']} [{task['config']}] rc={record['rc']} cpu={record['cpu_s']} wall={record['wall_s']} rss={record['rss_kb']} invariant_limit={record['invariant_time_limit_reached']}", flush=True)
                index += 1
            if args.mode != "probe":
                # Pair adjacent measurements, retaining which direction ran first.
                for offset in range(0, len(block_runs), 2):
                    adjacent = block_runs[offset:offset + 2]
                    a = next(run for run in adjacent if run["variant"] == "A")
                    b = next(run for run in adjacent if run["variant"] == "B")
                    pair = make_pair(a, b, task_index, expected_sha)
                    if args.mode == "validate" and pair["output_equal"] and a["rc"] == b["rc"]:
                        # Correctness validation is meaningful even on tiny tasks
                        # below the external timer's 10ms resolution.
                        pair["errors"] = [error for error in pair["errors"] if "timer resolution" not in error]
                        pair["success"] = not pair["errors"]
                    pairs.append(pair)
                    append_json(args.out / "pairs.jsonl", pair)
                    if not pair["success"]:
                        print("  FAIL: " + "; ".join(pair["errors"]), flush=True)
    if args.mode == "probe":
        summary = {"mode": "probe", "success": all(run["success"] for run in runs),
                   "runs": runs}
    elif args.mode == "validate":
        summary = {"mode": "validate", "success": all(pair["success"] for pair in pairs),
                   "tasks": len(tasks), "valid_tasks": sum(pair["success"] for pair in pairs),
                   "failures": [pair for pair in pairs if not pair["success"]]}
    else:
        summary = summarize_ab(tasks, pairs, args)
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: value for key, value in summary.items() if key not in ("tasks", "runs")}, indent=2), flush=True)
    print(f"Results: {args.out / 'summary.json'}", flush=True)
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
