import os
from pathlib import Path
import re
import subprocess

import pytest


REPO = Path(__file__).resolve().parents[2]
GRAPHS = {
    "initial-goal": (1, []),
    "chain": (4, [(0, 1, 1), (1, 2, 2), (2, 3, 1)]),
    "cheaper-path": (4, [(0, 1, 5), (0, 2, 1), (2, 1, 1), (1, 3, 100)]),
}


@pytest.fixture(params=["release", "debug"])
def downward(request):
    binary_name = "downward.exe" if os.name == "nt" else "downward"
    binary = REPO / "builds" / request.param / "bin" / binary_name
    assert binary.is_file(), f"Build {request.param} before running these tests."
    return binary


def graph_task(name):
    """Encode a directed graph as a task with one finite-domain variable."""
    num_states, edges = GRAPHS[name]
    lines = [
        "begin_version", "3", "end_version",
        "begin_metric", "1", "end_metric",
        "1", "begin_variable", "vertex", "-1", str(num_states),
        *[f"Atom vertex{i}()" for i in range(num_states)],
        "end_variable", "0",
        "begin_state", "0", "end_state",
        "begin_goal", "1", f"0 {num_states - 1}", "end_goal", str(len(edges)),
    ]
    for source, target, cost in edges:
        lines.extend([
            "begin_operator", f"move-{source}-{target}", "0", "1",
            f"0 0 {source} {target}", str(cost), "end_operator",
        ])
    return "\n".join(lines + ["0", ""])


def run_search(downward, tmp_path, name, graph, search):
    directory = tmp_path / name
    directory.mkdir()
    plan = directory / "sas_plan"
    result = subprocess.run(
        [str(downward), "--search", search, "--internal-plan-file", str(plan)],
        input=graph_task(graph), cwd=directory, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == 0, result.stdout
    return result.stdout, plan.read_text()


def statistics(output):
    result = {}
    for label in ["Expanded", "Reopened", "Evaluated", "Generated", "Dead ends"]:
        match = re.search(rf"\b{label}:? (\d+) state\(s\)\.", output)
        assert match, output
        result[label] = int(match.group(1))
    for label in ["Evaluations", "Plan cost"]:
        match = re.search(rf"\b{label}: (\d+)", output)
        assert match, output
        result[label] = int(match.group(1))
    return result


def f_values(output):
    return [int(value) for value in re.findall(r"\bf = (\d+),", output)]


@pytest.mark.parametrize("graph, cost_type, evaluations, jumps", [
    ("initial-goal", "normal", 1, [0]),
    ("chain", "normal", 4, [1, 2, 4]),
    ("chain", "one", 4, [1, 2, 3]),
    ("chain", "plusone", 4, [1, 3, 6, 7]),
    ("cheaper-path", "normal", 5, [1, 2, 3, 102]),
])
def test_astar_statistics_do_not_reevaluate_uncached_heuristic(
        downward, tmp_path, graph, cost_type, evaluations, jumps):
    heuristic = "blind(cache_estimates=false)"
    actual, actual_plan = run_search(
        downward, tmp_path, "astar", graph,
        f"astar({heuristic},cost_type={cost_type})")
    reference, reference_plan = run_search(
        downward, tmp_path, "reference", graph,
        f"let(h,{heuristic},eager("
        "tiebreaking([sum([g(),h]),h],unsafe_pruning=false),"
        f"reopen_closed=true,cost_type={cost_type}))")
    assert actual_plan == reference_plan
    assert statistics(actual) == statistics(reference)
    assert statistics(actual)["Evaluations"] == evaluations
    assert f_values(actual) == jumps


def test_astar_statistics_handle_reopened_nodes(downward, tmp_path):
    # Unit-cost hmax times ten is inconsistent with the original action costs.
    # Vertex 1 closes at g=5, then reopens at g=2 through vertex 2.
    heuristic = (
        "weight(eval_modify_costs(hmax(cache_estimates=false),cost_type=one),10)")
    actual, actual_plan = run_search(
        downward, tmp_path, "astar", "cheaper-path", f"astar({heuristic})")
    reference, reference_plan = run_search(
        downward, tmp_path, "reference", "cheaper-path",
        f"let(h,{heuristic},eager("
        "tiebreaking([sum([g(),h]),h],unsafe_pruning=false),"
        "reopen_closed=true))")
    assert actual_plan == reference_plan
    assert statistics(actual) == statistics(reference)
    assert statistics(actual) == {
        "Expanded": 5, "Reopened": 1, "Evaluated": 4, "Generated": 5,
        "Dead ends": 0, "Evaluations": 6, "Plan cost": 102,
    }
    assert f_values(actual) == [20, 21, 102]


def test_general_eager_statistics_evaluate_custom_f(downward, tmp_path):
    output, _ = run_search(
        downward, tmp_path, "custom-f", "chain",
        "let(h,blind(cache_estimates=false),eager("
        "tiebreaking([sum([g(),h]),h],unsafe_pruning=false),"
        "reopen_closed=true,f_eval=blind(cache_estimates=false)))")
    # Four queue evaluations plus one initial and four expansion evaluations
    # of the separate reporting heuristic, including the goal state.
    assert statistics(output)["Evaluations"] == 9
    assert statistics(output)["Plan cost"] == 4
    assert f_values(output) == [1]


def test_general_eager_statistics_use_expansion_context(downward, tmp_path):
    output, _ = run_search(
        downward, tmp_path, "preferred-f", "chain",
        "let(p,pref(),eager(tiebreaking([p,g()]),f_eval=p))")
    # Initial insertion is preferred, but its expansion context is not.
    jumps = re.findall(r"\bf = (\d+), \d+ evaluated, (\d+) expanded", output)
    assert jumps == [("0", "0"), ("1", "0")]
    assert statistics(output)["Plan cost"] == 4


def test_lazy_astar_statistics_keep_expansion_evaluation(downward, tmp_path):
    output, _ = run_search(
        downward, tmp_path, "lazy", "chain",
        "let(h,blind(),astar(sum([h,blind(cache_estimates=false)]),"
        "lazy_evaluator=h))")
    # Each state evaluates both queue heuristics once. Expansion statistics
    # still recompute the uncached heuristic when lazy evaluation is enabled.
    assert statistics(output)["Evaluations"] == 12
    assert statistics(output)["Plan cost"] == 4
    assert f_values(output) == [2, 3, 5]
