#! /usr/bin/env python

"""
Compare the Python and C++ translators (this branch's latest revision) on all
of downward-benchmarks, htg-domains, and Autoscale-Sat.

Both translators are run through fast-downward.py (--translator py / cpp) on
byte-identical input; the C++ translator is builds/release/bin/translate-cpp of
the current checkout. Per task we record the translator's own CPU time, peak
RSS (external /usr/bin/time, uniform), and the sha256 of output.sas. The
equivalence step then checks, per task, whether py and cpp produced
byte-identical SAS output.

Domain names collide across the three collections (e.g. satellite exists in
both downward-benchmarks and Autoscale), so every domain is prefixed with its
collection to keep tasks distinct.
"""

import json
import os
import sys
from collections import defaultdict
from pathlib import Path

import custom_parser
import project

from downward import suites
from lab.experiment import Experiment

REPO = project.get_repo_base()
DRIVER = str(REPO / "fast-downward.py")
WRAPPER = str(project.DIR / "run-translate-cmp.sh")

# htg-domains has no established env var; default it so the grid-step jobs (which
# re-run this script) can enumerate it even when only PATH is exported. An
# explicit HTG_BENCHMARKS still wins. DOWNWARD_BENCHMARKS / AUTOSCALE_BENCHMARKS_SAT
# come from the user's profile.
os.environ.setdefault("HTG_BENCHMARKS", "/home/x_jense/projects/htg-benchmarks-flattened")

# (prefix, env var) for each benchmark collection.
COLLECTIONS = [
    ("downward", "DOWNWARD_BENCHMARKS"),
    ("htg", "HTG_BENCHMARKS"),
    ("autoscale", "AUTOSCALE_BENCHMARKS_SAT"),
]
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


def collection_domains(root: Path) -> list[str]:
    return sorted(
        p.name
        for p in root.iterdir()
        if p.is_dir() and any(p.glob("*.pddl"))
    )


exp = Experiment(environment=ENV)

n_tasks = 0
for prefix, env in COLLECTIONS:
    try:
        root = Path(os.environ[env]).resolve()
    except KeyError:
        sys.exit(f"{env} must point to the {prefix} benchmark collection")
    assert root.is_dir(), f"{root} is not a directory"
    for task in suites.build_suite(root, collection_domains(root)):
        domain = f"{prefix}-{task.domain}"
        n_tasks += 1
        for impl in IMPLS:
            run = exp.add_run()
            run.add_command(
                "translate",
                [WRAPPER, DRIVER, impl, task.domain_file, task.problem_file],
                time_limit=1800,
                memory_limit=8000,
            )
            run.set_property("domain", domain)
            run.set_property("problem", task.problem)
            run.set_property("algorithm", impl)
            run.set_property("id", [impl, domain, task.problem])
print(f"{n_tasks} tasks x {len(IMPLS)} translators = {n_tasks * len(IMPLS)} runs")

exp.add_parser(custom_parser.get_parser())

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_fetcher(name="fetch")

# Runtime and peak-memory comparison, py vs cpp (no domain grouping: our domain
# names are collection-prefixed and not in project's rename table).
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
        "Byte-equivalence of py vs cpp output.sas (whole benchmark set)",
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
