#!/usr/bin/env python3
"""Run Scorpion with GBFS under the six BFWS partition functions f1-f6.

The evaluation functions follow Lipovetzky and Geffner, "Best-First Width
Search: Exploration and Exploitation in Classical Planning" (AAAI 2017). Each
function f is a lexicographic tuple <e_1, ..., e_n> (smaller is better), which we
realize with a "tiebreaking" open list inside greedy best-first search (eager
search without g, i.e. GBFS).

Building blocks (all available as Scorpion evaluators):
  h_add  = add()                 additive heuristic
  h_ff   = ff()                  FF/relaxed-plan heuristic (preferred operators)
  h_L    = landmark_sum(...)     landmark heuristic
  #g     = goalcount()           number of unachieved top-level goals
  #r     = subgoal_counting()    relaxed subgoal counter (unachieved subgoals
                                  from the last relaxed plan along the path)
  w_{X}  = novelty(width=2, evals=[X])    novelty relative to partition X
  help   = pref()                1 if the generating operator is helpful, else 0

Partition functions:
  f1 = <h_add, w_{h_add}>
  f2 = <w_{h_add}, h_add>
  f3 = <h_L, h_ff>
  f4 = <w_{h_L,h_ff}, h_L, h_ff>
  f5 = <w_{#g,#r}, #g>
  f6 = <w_{h_L,h_ff}, help, h_L, w'_{h_ff}, h_ff>
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent
FAST_DOWNWARD = REPO / "fast-downward.py"
BENCHMARKS = REPO / "misc" / "tests" / "benchmarks" / "miconic"
DOMAIN = BENCHMARKS / "domain.pddl"
PROBLEM = BENCHMARKS / "s1-0.pddl"

# Evaluator predefinitions (passed via --evaluator) shared by several configs.
HADD = "hadd=add()"
HFF = "hff=ff()"
HLM = (
    "hlm=landmark_sum(lm_factory=lm_reasonable_orders_hps(lm_rhw()), "
    "transform=adapt_costs(one), pref=false)"
)
HG = "hg=goalcount()"
HR = "hr=subgoal_counting()"

# Each entry: name -> (evaluator predefinitions needed, search string).
CONFIGS = {
    "f1": ([HADD], "eager(tiebreaking([hadd, novelty(width=2, evals=[hadd])]))"),
    "f2": ([HADD], "eager(tiebreaking([novelty(width=2, evals=[hadd]), hadd]))"),
    "f3": ([HFF, HLM], "eager(tiebreaking([hlm, hff]))"),
    "f4": ([HFF, HLM],
           "eager(tiebreaking([novelty(width=2, evals=[hlm, hff]), hlm, hff]))"),
    "f5": ([HG, HR],
           "eager(tiebreaking([novelty(width=2, evals=[hg, hr]), hg]))"),
    "f6": ([HFF, HLM],
           "eager(tiebreaking([novelty(width=2, evals=[hlm, hff]), pref(), hlm, "
           "novelty(width=2, evals=[hff]), hff]), preferred=[hff])"),
}


def run(name, evaluators, search):
    with tempfile.TemporaryDirectory(prefix=f"bfws-{name}-") as tmp:
        tmp = Path(tmp)
        plan_file = tmp / "sas_plan"
        cmd = [
            sys.executable,
            str(FAST_DOWNWARD),
            "--plan-file",
            str(plan_file),
            "--sas-file",
            str(tmp / "output.sas"),
            str(DOMAIN),
            str(PROBLEM),
        ]
        for ev in evaluators:
            cmd += ["--evaluator", ev]
        cmd += ["--search", search]
        proc = subprocess.run(
            cmd, cwd=tmp, capture_output=True, text=True
        )
        out = proc.stdout + proc.stderr
        solved = "Solution found!" in out
        length = None
        m = re.search(r"Plan length: (\d+) step", out)
        if m:
            length = int(m.group(1))
        expanded = None
        m = re.search(r"Expanded (\d+) state", out)
        if m:
            expanded = int(m.group(1))
        plan = plan_file.read_text() if plan_file.exists() else ""
        return solved, length, expanded, plan, out


def main():
    print(f"Task: {PROBLEM.relative_to(REPO)}\n")
    results = {}
    for name, (evaluators, search) in CONFIGS.items():
        print(f"=== {name}: {search}")
        solved, length, expanded, plan, out = run(name, evaluators, search)
        results[name] = (solved, length, expanded)
        if solved:
            print(f"    SOLVED  plan length={length}  expanded={expanded}")
            for line in plan.splitlines():
                if line and not line.startswith(";"):
                    print(f"      {line}")
        else:
            print("    FAILED")
            for line in out.splitlines():
                if re.search(r"error|exception|caught|Unknown|not solvable", line, re.I):
                    print(f"      {line}")
        print()

    print("=" * 50)
    print(f"{'config':<8}{'solved':<10}{'length':<10}{'expanded':<10}")
    all_ok = True
    for name, (solved, length, expanded) in results.items():
        print(f"{name:<8}{str(solved):<10}{str(length):<10}{str(expanded):<10}")
        all_ok = all_ok and solved
    print("=" * 50)
    if all_ok:
        print("All six BFWS partition functions found a plan.")
        return 0
    print("Some configurations failed.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
