import os.path
import subprocess
import sys

DIR = os.path.dirname(os.path.abspath(__file__))
# Directory holding the "fast_downward" package.
PACKAGE_DIR = os.path.dirname(DIR)
REPO = os.path.abspath(os.path.join(DIR, "..", "..", ".."))
BENCHMARKS = os.path.join(REPO, "misc", "tests", "benchmarks")
DOMAIN = os.path.join(BENCHMARKS, "gripper", "domain.pddl")
PROBLEM = os.path.join(BENCHMARKS, "gripper", "prob01.pddl")
# Modules that can be run standalone to inspect an intermediate result.
MODULES = [
    "build_model",
    "graph",
    "instantiate",
    "invariant_finder",
    "normalize",
    "pddl_to_prolog",
    "",  # The package itself runs the whole translator.
]

def test_scripts(tmp_path):
    env = dict(os.environ, PYTHONPATH=PACKAGE_DIR)
    for module in MODULES:
        name = "fast_downward.translate"
        if module:
            name += "." + module
        assert subprocess.check_call(
            [sys.executable, "-m", name, DOMAIN, PROBLEM],
            cwd=tmp_path, env=env) == 0
