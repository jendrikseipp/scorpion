#! /usr/bin/env python

"""
Compare the Python and C++ translators (this branch's latest revision) on the
IPC 2023 learning track *test* set (10 domains x 90 testing tasks = 900 tasks).

Layout of the benchmark collection differs from downward-benchmarks: each
domain has a single <domain>/domain.pddl and its test problems live under
<domain>/testing/{easy,medium,hard}/pNN.pddl. The same problem name (pNN)
recurs in every difficulty bucket, so the difficulty is folded into the
problem name to keep tasks distinct.

Both translators run through fast-downward.py (--translator py / cpp) on
byte-identical input; the C++ translator is builds/release/bin/translate-cpp of
the current checkout. Per task we record the translator's own CPU time, peak
RSS (external /usr/bin/time, uniform), and the sha256 of output.sas. The
equivalence step then checks, per task, whether py and cpp produced
byte-identical SAS output.
"""

import json
import os
import sys
from collections import defaultdict
from pathlib import Path

import custom_parser
import project

from lab.experiment import Experiment

REPO = project.get_repo_base()
DRIVER = str(REPO / "fast-downward.py")
WRAPPER = str(project.DIR / "run-translate-cmp.sh")

# IPC 2023 learning benchmarks. No established env var; default it so the
# grid-step jobs (which re-run this script) can find it even when only PATH is
# exported. An explicit IPC2023_LEARNING_BENCHMARKS still wins.
os.environ.setdefault(
    "IPC2023_LEARNING_BENCHMARKS",
    "/proj/dfsplan/users/x_jense/ipc2023-learning/benchmarks",
)

DOMAINS = [
    "blocksworld", "childsnack", "ferry", "floortile", "miconic",
    "rovers", "satellite", "sokoban", "spanner", "transport",
]
DIFFICULTIES = ["easy", "medium", "hard"]
IMPLS = ["py", "cpp"]

if project.REMOTE:
    ENV = project.TetralithEnvironment(
        memory_per_cpu="8G",
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-382",
    )
    LIMIT = True
else:
    ENV = project.LocalEnvironment(processes=4)
    LIMIT = False

ATTRIBUTES = [
    "error",
    "coverage",
    "translator_time",
    "translator_wall_time",
    "peak_memory_kb",
    "translator_task_size",
    "sas_sha256",
]


def ipc2023_tasks(root: Path):
    """Yield (domain, problem_label, domain_file, problem_file) tuples."""
    for domain in DOMAINS:
        domain_file = root / domain / "domain.pddl"
        assert domain_file.is_file(), f"missing {domain_file}"
        for difficulty in DIFFICULTIES:
            bucket = root / domain / "testing" / difficulty
            if not bucket.is_dir():
                continue
            for problem_file in sorted(bucket.glob("p*.pddl")):
                label = f"{difficulty}-{problem_file.stem}"
                yield domain, label, str(domain_file), str(problem_file)


try:
    ROOT = Path(os.environ["IPC2023_LEARNING_BENCHMARKS"]).resolve()
except KeyError:
    sys.exit("IPC2023_LEARNING_BENCHMARKS must point to the benchmark collection")
assert ROOT.is_dir(), f"{ROOT} is not a directory"

exp = Experiment(environment=ENV)

n_tasks = 0
for domain, problem, domain_file, problem_file in ipc2023_tasks(ROOT):
    n_tasks += 1
    for impl in IMPLS:
        run = exp.add_run()
        run.add_command(
            "translate",
            [WRAPPER, DRIVER, impl, domain_file, problem_file],
            time_limit=1800,
            memory_limit=8000,
        )
        run.set_property("domain", domain)
        run.set_property("problem", problem)
        run.set_property("algorithm", impl)
        run.set_property("id", [impl, domain, problem])
print(f"{n_tasks} tasks x {len(IMPLS)} translators = {n_tasks * len(IMPLS)} runs")

exp.add_parser(custom_parser.get_parser())

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_fetcher(name="fetch")

project.add_absolute_report(exp, attributes=ATTRIBUTES)
project.add_comparative_report(
    exp,
    [("py", "cpp")],
    attributes=["translator_time", "peak_memory_kb", "translator_task_size"],
    name=f"{exp.name}-py-vs-cpp",
)


def check_byte_equivalence():
    """Compare py vs cpp output.sas sha256 per task; write a report."""
    props = json.load(open(Path(exp.eval_dir) / "properties"))
    by_task = defaultdict(dict)
    for run in props.values():
        by_task[(run["domain"], run["problem"])][run["algorithm"]] = run

    identical = differ = only_py = only_cpp = neither = 0
    diffs = []
    for (domain, problem), runs in sorted(by_task.items()):
        py = runs.get("py", {})
        cpp = runs.get("cpp", {})
        ph, ch = py.get("sas_sha256"), cpp.get("sas_sha256")
        if ph and ch:
            if ph == ch:
                identical += 1
            else:
                differ += 1
                diffs.append(f"{domain}:{problem}")
        elif ph and not ch:
            only_py += 1
        elif ch and not ph:
            only_cpp += 1
        else:
            neither += 1

    lines = [
        "Byte-equivalence of py vs cpp output.sas (IPC 2023 learning test set)",
        "=" * 62,
        f"both translated, byte-identical : {identical}",
        f"both translated, DIFFER         : {differ}",
        f"only py translated              : {only_py}",
        f"only cpp translated             : {only_cpp}",
        f"neither translated              : {neither}",
    ]
    if diffs:
        lines.append("")
        lines.append("Tasks with differing output:")
        lines.extend(f"  {d}" for d in diffs)
    report = "\n".join(lines)
    print(report)
    out = Path(exp.eval_dir) / "byte-equivalence.txt"
    out.write_text(report + "\n")
    print(f"\nWrote {out}")


exp.add_step("equivalence", check_byte_equivalence)

exp.run_steps()
