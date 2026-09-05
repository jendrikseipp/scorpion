"""Check that the C++ translator supports the same options as the Python one.

The C++ translator (src/translate-cpp/) is a port of the Python translator
(src/translate/fast_downward/translate/) and must stay a drop-in replacement
for it. These tests therefore check two things:

  - Option surface: every command-line option of the Python translator is also
    accepted by the C++ translator (CPP_ONLY_OPTIONS lists the few extra
    options the C++ port has).
  - Option behaviour: for the bundled benchmark tasks, both translators produce
    the same output.sas, predicates.txt and static-atoms.txt under the options
    that the byte-for-byte equivalence test (test-translator-equivalence.py)
    does not cover because they do not affect output.sas.

Unlike test-translator-equivalence.py, these tests only use the benchmarks
bundled in misc/tests/benchmarks/, so they need no downward-benchmarks
checkout. They do need a built translator:

    ./build.py release
"""

import subprocess
import sys
from pathlib import Path

import pytest

DIR = Path(__file__).resolve().parent
REPO = DIR.parents[1]
DRIVER = REPO / "fast-downward.py"
BENCHMARKS_DIR = DIR / "benchmarks"
TRANSLATE_PACKAGE_DIR = REPO / "src" / "translate"

# Options that only make sense for the C++ port and thus have no Python
# counterpart.
CPP_ONLY_OPTIONS = {
    # Selects the CPython-compatible RNG that makes the C++ translator's
    # invariant synthesis match the Python translator's.
    "--no-cpython-rng",
}

CONDITION_NORMALIZATION_STRATEGIES = [
    "dnf",
    "axiomatize_disjunctions",
    "axiomatize_disjunctions_existentials",
]

# Tasks whose conditions exercise the different normalization strategies, plus
# one plain STRIPS task as a sanity check.
TASKS = [
    "adl-conditions/p01.pddl",
    "miconic-simpleadl/s1-0.pddl",
    "philosophers/p01-phil2.pddl",
    "gripper/prob01.pddl",
]


def get_task(rel_path):
    problem = BENCHMARKS_DIR / rel_path
    domain = problem.parent / "domain.pddl"
    if not domain.is_file():
        domain = problem.parent / f"{problem.stem}-domain.pddl"
    assert domain.is_file(), f"no domain for {problem}"
    return domain, problem


def translate(translator, task, cwd, options):
    domain, problem = task
    cmd = [sys.executable, str(DRIVER), "--translator", translator,
           "--translate", str(domain), str(problem)]
    if options:
        cmd += ["--translate-options", *options]
    return subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, encoding="utf-8")


def translate_with_both(task, tmp_path, options):
    """Translate task with both variants and return their output directories."""
    dirs = {}
    for translator in ["py", "cpp"]:
        out_dir = tmp_path / translator
        out_dir.mkdir()
        proc = translate(translator, task, out_dir, options)
        assert proc.returncode == 0, (
            f"{translator} translator failed on {task[1].name} "
            f"with {options}:\n{proc.stderr}")
        dirs[translator] = out_dir
    return dirs["py"], dirs["cpp"]


def read_lines(path):
    return path.read_text().splitlines()


def get_python_option_strings():
    sys.path.insert(0, str(TRANSLATE_PACKAGE_DIR))
    from fast_downward.translate import options
    result = set()
    for action in options.get_arg_parser()._actions:
        result.update(action.option_strings)
    return result


def get_cpp_binary():
    binary = REPO / "builds" / "release" / "bin" / "translate-cpp"
    if not binary.exists():
        binary = binary.with_suffix(".exe")
    assert binary.exists(), "C++ translator not built: run ./build.py release"
    return binary


def get_cpp_option_strings():
    """Options the C++ translator advertises in its usage message.

    We call the binary directly because the driver handles "--help" itself.
    """
    proc = subprocess.run(
        [str(get_cpp_binary()), "--help"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8")
    result = set()
    for token in proc.stdout.replace("[", " ").replace("]", " ").split():
        if token.startswith("--"):
            result.add(token)
    assert result, f"could not parse C++ usage message:\n{proc.stdout}"
    return result


def test_cpp_supports_all_python_options():
    python_options = get_python_option_strings()
    cpp_options = get_cpp_option_strings()
    # Short forms such as "-h" are not part of the comparison, and "--help" is
    # checked separately because it does not appear in the usage message.
    python_options = {
        opt for opt in python_options if opt.startswith("--")} - {"--help"}
    missing = python_options - cpp_options
    assert not missing, (
        "the C++ translator does not support these Python translator "
        f"options: {sorted(missing)}")
    unexpected = cpp_options - python_options - CPP_ONLY_OPTIONS
    assert not unexpected, (
        "the C++ translator advertises options the Python translator does not "
        f"have: {sorted(unexpected)}")


@pytest.mark.parametrize("rel_path", TASKS)
@pytest.mark.parametrize("strategy", CONDITION_NORMALIZATION_STRATEGIES)
def test_condition_normalization_strategies_agree(rel_path, strategy, tmp_path):
    task = get_task(rel_path)
    options = ["--condition-normalization-strategy", strategy]
    py_dir, cpp_dir = translate_with_both(task, tmp_path, options)
    assert (read_lines(py_dir / "output.sas") ==
            read_lines(cpp_dir / "output.sas"))


@pytest.mark.parametrize("rel_path", TASKS)
def test_condition_normalization_strategies_differ(rel_path, tmp_path):
    """The strategies must actually be wired up, not silently ignored.

    Only the ADL task is guaranteed to contain conditions that the strategies
    treat differently, so we only require a difference there.
    """
    task = get_task(rel_path)
    outputs = {}
    for strategy in CONDITION_NORMALIZATION_STRATEGIES:
        out_dir = tmp_path / strategy
        out_dir.mkdir()
        proc = translate("cpp", task, out_dir,
                         ["--condition-normalization-strategy", strategy])
        assert proc.returncode == 0, proc.stderr
        outputs[strategy] = read_lines(out_dir / "output.sas")
    if rel_path == "adl-conditions/p01.pddl":
        assert len({tuple(lines) for lines in outputs.values()}) == 3


@pytest.mark.parametrize("rel_path", TASKS)
def test_dump_predicates_agrees(rel_path, tmp_path):
    task = get_task(rel_path)
    py_dir, cpp_dir = translate_with_both(task, tmp_path, ["--dump-predicates"])
    py_predicates = read_lines(py_dir / "predicates.txt")
    assert py_predicates == read_lines(cpp_dir / "predicates.txt")
    assert py_predicates, "expected at least one predicate"


@pytest.mark.parametrize("rel_path", TASKS)
def test_dump_static_atoms_agrees(rel_path, tmp_path):
    task = get_task(rel_path)
    py_dir, cpp_dir = translate_with_both(
        task, tmp_path, ["--dump-static-atoms"])
    # The Python translator writes the atoms of non-static predicates in the
    # iteration order of a set, which is not reproducible, so we compare the
    # files as sets of lines.
    py_atoms = set(read_lines(py_dir / "static-atoms.txt"))
    assert py_atoms == set(read_lines(cpp_dir / "static-atoms.txt"))
    assert py_atoms, "expected at least one static atom"


@pytest.mark.parametrize("rel_path", TASKS)
def test_stop_after_parsing_pddl(rel_path, tmp_path):
    task = get_task(rel_path)
    options = ["--stop-after-parsing-pddl", "--dump-predicates"]
    py_dir, cpp_dir = translate_with_both(task, tmp_path, options)
    for out_dir in [py_dir, cpp_dir]:
        assert not (out_dir / "output.sas").exists()
        assert (out_dir / "predicates.txt").exists()
    assert (read_lines(py_dir / "predicates.txt") ==
            read_lines(cpp_dir / "predicates.txt"))


def test_cpp_help():
    binary = get_cpp_binary()
    proc = subprocess.run([str(binary), "--help"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, encoding="utf-8")
    assert proc.returncode == 0
    assert proc.stdout.startswith("usage:")


def test_invalid_condition_normalization_strategy(tmp_path):
    task = get_task("gripper/prob01.pddl")
    for translator in ["py", "cpp"]:
        proc = translate(
            translator, task, tmp_path,
            ["--condition-normalization-strategy", "no-such-strategy"])
        assert proc.returncode != 0
