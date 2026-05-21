#!/usr/bin/env python3
"""Canonical-form diff for two output.sas files.

The Python translator and the C++ port may differ in:
  - the order of variables (causal-graph reorder uses different tie-breaking),
  - the order of operators (lexicographic sorts are stable but on different
    inputs in edge cases),
  - the names of internal variables ("var0", "var1", ...).

This script normalizes both files by:
  - sorting mutex groups by their facts,
  - sorting operators by their canonical encoding,
  - rebuilding the var name to a position-agnostic key based on the
    sorted set of value names per variable.

The script reports:
  - which sections match exactly after normalization,
  - the variable count, operator count, axiom count, and goal-fact count
    in each file.

Usage:
  canonical_diff.py path/to/python_output.sas path/to/cpp_output.sas
"""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path


def parse_sas(path: Path):
    with path.open() as f:
        tokens = f.read().splitlines()

    i = 0

    def expect(s):
        nonlocal i
        if tokens[i] != s:
            raise ValueError(f"expected {s!r} at line {i + 1}, got {tokens[i]!r}")
        i += 1

    def read_int():
        nonlocal i
        v = int(tokens[i])
        i += 1
        return v

    expect("begin_version")
    version = read_int()
    expect("end_version")
    expect("begin_metric")
    metric = read_int()
    expect("end_metric")

    num_vars = read_int()
    variables = []
    for _ in range(num_vars):
        expect("begin_variable")
        name = tokens[i]; i += 1
        axiom_layer = read_int()
        rng = read_int()
        values = []
        for _ in range(rng):
            values.append(tokens[i]); i += 1
        expect("end_variable")
        variables.append({"name": name, "layer": axiom_layer,
                          "range": rng, "values": values})

    num_mutex = read_int()
    mutexes = []
    for _ in range(num_mutex):
        expect("begin_mutex_group")
        n = read_int()
        facts = []
        for _ in range(n):
            a, b = tokens[i].split(); i += 1
            facts.append((int(a), int(b)))
        expect("end_mutex_group")
        mutexes.append(sorted(facts))

    expect("begin_state")
    init = []
    for _ in range(num_vars):
        init.append(read_int())
    expect("end_state")

    expect("begin_goal")
    n = read_int()
    goal = []
    for _ in range(n):
        a, b = tokens[i].split(); i += 1
        goal.append((int(a), int(b)))
    expect("end_goal")

    num_ops = read_int()
    operators = []
    for _ in range(num_ops):
        expect("begin_operator")
        name = tokens[i]; i += 1
        n = read_int()
        prevail = []
        for _ in range(n):
            a, b = tokens[i].split(); i += 1
            prevail.append((int(a), int(b)))
        m = read_int()
        pre_post = []
        for _ in range(m):
            parts = tokens[i].split(); i += 1
            k = int(parts[0])
            cond = [(int(parts[1 + 2 * j]), int(parts[2 + 2 * j])) for j in range(k)]
            v = int(parts[1 + 2 * k])
            pre = int(parts[2 + 2 * k])
            post = int(parts[3 + 2 * k])
            pre_post.append((v, pre, post, cond))
        cost = read_int()
        expect("end_operator")
        operators.append({"name": name, "prevail": prevail,
                          "pre_post": pre_post, "cost": cost})

    num_ax = read_int()
    axioms = []
    for _ in range(num_ax):
        expect("begin_rule")
        n = read_int()
        cond = []
        for _ in range(n):
            a, b = tokens[i].split(); i += 1
            cond.append((int(a), int(b)))
        parts = tokens[i].split(); i += 1
        axioms.append({"condition": cond,
                       "effect": (int(parts[0]), int(parts[1]), int(parts[2]))})
        expect("end_rule")

    return {
        "version": version, "metric": metric, "variables": variables,
        "mutexes": mutexes, "init": init, "goal": goal,
        "operators": operators, "axioms": axioms,
    }


def variable_signature(var, init_val):
    return (tuple(sorted(var["values"])), var["layer"], var["range"], init_val)


def variable_remap(a, b):
    """Try to remap a's variable indices to match b's by matching
    (sorted value names, layer, range, init value). Return remap dict or None
    if there's no bijection."""
    if len(a["variables"]) != len(b["variables"]):
        return None
    sig_to_b = collections.defaultdict(list)
    for j, vb in enumerate(b["variables"]):
        sig_to_b[variable_signature(vb, b["init"][j])].append(j)
    remap = {}
    for i, va in enumerate(a["variables"]):
        sig = variable_signature(va, a["init"][i])
        if not sig_to_b[sig]:
            return None
        remap[i] = sig_to_b[sig].pop(0)
    return remap


def remap_pair(pair, var_map):
    v, val = pair
    return (var_map.get(v, v), val)


def remap_cond(cond, var_map):
    return tuple(sorted(remap_pair(p, var_map) for p in cond))


def remap_pre_post(pre_post, var_map):
    out = []
    for v, pre, post, cond in pre_post:
        nv = var_map.get(v, v)
        nc = remap_cond(cond, var_map)
        out.append((nv, pre, post, nc))
    return tuple(sorted(out))


def normalize(task, var_map):
    init = [task["init"][i] for i in sorted(var_map.keys(),
                                              key=lambda x: var_map[x])]
    norm_mutexes = sorted(tuple(sorted(remap_pair(f, var_map) for f in m))
                          for m in task["mutexes"])
    norm_goal = tuple(sorted(remap_pair(p, var_map) for p in task["goal"]))
    norm_ops = sorted((op["name"], remap_cond(op["prevail"], var_map),
                       remap_pre_post(op["pre_post"], var_map), op["cost"])
                      for op in task["operators"])
    norm_axioms = sorted((remap_cond(ax["condition"], var_map),
                          (var_map.get(ax["effect"][0], ax["effect"][0]),
                           ax["effect"][1], ax["effect"][2]))
                         for ax in task["axioms"])
    return {
        "init": tuple(init), "mutexes": tuple(norm_mutexes),
        "goal": norm_goal, "operators": tuple(norm_ops),
        "axioms": tuple(norm_axioms),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("python_sas", type=Path)
    p.add_argument("cpp_sas", type=Path)
    args = p.parse_args()
    a = parse_sas(args.python_sas)
    b = parse_sas(args.cpp_sas)

    print(f"python: vars={len(a['variables'])} mutexes={len(a['mutexes'])} "
          f"ops={len(a['operators'])} axioms={len(a['axioms'])} "
          f"goal_facts={len(a['goal'])}")
    print(f"cpp:    vars={len(b['variables'])} mutexes={len(b['mutexes'])} "
          f"ops={len(b['operators'])} axioms={len(b['axioms'])} "
          f"goal_facts={len(b['goal'])}")

    if a["metric"] != b["metric"]:
        print("METRIC differs")
        return 1

    var_map = variable_remap(a, b)
    if var_map is None:
        print("Variable signatures don't match (cannot remap variables).")
        return 1
    na = normalize(a, var_map)
    identity = {i: i for i in range(len(b["variables"]))}
    nb = normalize(b, identity)

    ok = True
    for section in ("init", "goal", "mutexes", "operators", "axioms"):
        if na[section] == nb[section]:
            print(f"{section}: match")
        else:
            print(f"{section}: DIFFER")
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
