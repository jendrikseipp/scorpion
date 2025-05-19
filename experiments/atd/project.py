from collections import namedtuple, defaultdict
from pathlib import Path
from lab import tools
from downward.reports.absolute import AbsoluteReport
from downward.reports.taskwise import TaskwiseReport
from downward.reports.compare import ComparativeReport
from downward.reports import PlanningReport
from lab.reports import Attribute, geometric_mean
from lab.environments import TetralithEnvironment
import tarfile
import getpass
import platform
import os
import re
import subprocess
import shutil
import sys

User = namedtuple("User", ["scp_login", "remote_repo"])

dfsplan = User(
    scp_login="x_winph@tetralith.nsc.liu.se",
    remote_repo="/proj/dfsplan/users/x_winph",
)

report_names = {
    AbsoluteReport: "abs",
    TaskwiseReport: "taskw",
    ComparativeReport: "comp",
}


def get_repo_base() -> Path:
    """Get base directory of the repository, as an absolute path.

    Search upwards in the directory tree from the main script until a
    directory with a subdirectory named ".git" is found.

    Abort if the repo base cannot be found."""
    path = Path(tools.get_script_path())
    while path.parent != path:
        if (path / ".git").is_dir():
            return path
        path = path.parent
    sys.exit("repo base could not be found")


try:
    DOWNWARD_DIR = Path(os.environ["DOWNWARD_REPO"])
except KeyError:
    sys.exit(
        "Error: the DOWNWARD_REPO environment variable must point to a Fast Downward"
        " repo."
    )

SCORPION_DIR = get_repo_base()


def load_env_var(env_var):
    try:
        return os.environ[env_var]
    except KeyError:
        sys.exit(f"Error: the environment variable {env_var} is not defined")


DOMAINS_DIR = load_env_var("DOWNWARD_BENCHMARKS")


DIR = Path(__file__).resolve().parent
NODE = platform.node()
REMOTE = TetralithEnvironment.is_present()

EXP_NAME = Path(__file__).stem

SUITE_TEST_NEW_DOMAINS = [
    "folding-opt23-adl-norm:p01.pddl",
    "recharging-robots-opt23-adl-norm:p01.pddl",
    "rubiks-cube-opt23-adl-norm:p01.pddl",
    "slitherlink-opt23-adl-norm:p01.pddl",
]

SUITE_OPTIMAL_STRIPS = [
    "agricola-opt18-strips",
    "airport",
    "barman-opt11-strips",
    "barman-opt14-strips",
    "blocks",
    "childsnack-opt14-strips",
    "data-network-opt18-strips",
    "depot",
    "driverlog",
    "elevators-opt08-strips",
    "elevators-opt11-strips",
    "floortile-opt11-strips",
    "floortile-opt14-strips",
    "folding-opt23-adl-norm",
    "freecell",
    "ged-opt14-strips",
    "grid",
    "gripper",
    "hiking-opt14-strips",
    "logistics00",
    "logistics98",
    "miconic",
    "movie",
    "mprime",
    "mystery",
    "nomystery-opt11-strips",
    "openstacks-opt08-strips",
    "openstacks-opt11-strips",
    "openstacks-opt14-strips",
    "openstacks-strips",
    "organic-synthesis-opt18-strips",
    "organic-synthesis-split-opt18-strips",
    "parcprinter-08-strips",
    "parcprinter-opt11-strips",
    "parking-opt11-strips",
    "parking-opt14-strips",
    "pathways",
    "pegsol-08-strips",
    "pegsol-opt11-strips",
    "petri-net-alignment-opt18-strips",
    "pipesworld-notankage",
    "pipesworld-tankage",
    "psr-small",
    "quantum-layout-opt23-strips",
    "recharging-robots-opt23-adl-norm",
    "rovers",
    # "rubiks-cube-opt23-adl-norm", contains conditional effects
    "satellite",
    "scanalyzer-08-strips",
    "scanalyzer-opt11-strips",
    "slitherlink-opt23-adl-norm",
    "snake-opt18-strips",
    "sokoban-opt08-strips",
    "sokoban-opt11-strips",
    "spider-opt18-strips",
    "storage",
    "termes-opt18-strips",
    "tetris-opt14-strips",
    "tidybot-opt11-strips",
    "tidybot-opt14-strips",
    "tpp",
    "transport-opt08-strips",
    "transport-opt11-strips",
    "transport-opt14-strips",
    "trucks-strips",
    "visitall-opt11-strips",
    "visitall-opt14-strips",
    "woodworking-opt08-strips",
    "woodworking-opt11-strips",
    "zenotravel",
]

SUITE_OPTIMAL_STRIPS_DEBUG_GRIPPER = [
    "gripper:prob01.pddl",
    "gripper:prob02.pddl",
    "gripper:prob03.pddl",
    "gripper:prob04.pddl",
    "gripper:prob05.pddl",
    "gripper:prob06.pddl",
    "gripper:prob07.pddl",
    "gripper:prob08.pddl",
    "gripper:prob09.pddl",
    "gripper:prob10.pddl",
    "gripper:prob11.pddl",
    "gripper:prob12.pddl",
    "gripper:prob13.pddl",
    "gripper:prob14.pddl",
    "gripper:prob15.pddl",
    "gripper:prob16.pddl",
    "gripper:prob17.pddl",
    "gripper:prob18.pddl",
    "gripper:prob19.pddl",
    "gripper:prob20.pddl",
]

SUITE_OPTIMAL_STRIPS_DEBUG = [
    "airport:p01-airport1-p1.pddl",
    "blocks:probBLOCKS-4-0.pddl",
    "data-network-opt18-strips:p01.pddl",
    "depot:p01.pddl",
    "driverlog:p01.pddl",
    "elevators-opt11-strips:p01.pddl",
    "freecell:p01.pddl",
    "ged-opt14-strips:d-1-2.pddl",
    "grid:prob01.pddl",
    "gripper:prob01.pddl",
    "hiking-opt14-strips:ptesting-1-2-3.pddl",
    "logistics00:probLOGISTICS-4-0.pddl",
    "miconic:s1-0.pddl",
    "mprime:prob01.pddl",
    "mystery:prob01.pddl",
    "organic-synthesis-opt18-strips:p01.pddl",
    "parcprinter-opt11-strips:p01.pddl",
    "pathways:p01.pddl",
    "pegsol-opt11-strips:p01.pddl",
    "petri-net-alignment-opt18-strips:p01.pddl",
    "pipesworld-notankage:p01-net1-b6-g2.pddl",
    "pipesworld-tankage:p01-net1-b6-g2-t50.pddl",
    "psr-small:p01-s2-n1-l2-f50.pddl",
    "rovers:p01.pddl",
    "satellite:p01-pfile1.pddl",
    "scanalyzer-opt11-strips:p01.pddl",
    "sokoban-opt11-strips:p01.pddl",
    "spider-opt18-strips:p01.pddl",
    "storage:p01.pddl",
    "termes-opt18-strips:p01.pddl",
    "tpp:p01.pddl",
    "transport-opt14-strips:p01.pddl",
    "trucks-strips:p01.pddl",
    "visitall-opt14-strips:p-1-5.pddl",
    "woodworking-opt08-strips:p01.pddl",
    "woodworking-opt11-strips:p01.pddl",
    "zenotravel:p01.pddl",
]

SUITE_OPTIMAL_STRIPS_DEBUG_EXTENDED = [
    "airport:p01-airport1-p1.pddl",
    "airport:p02-airport1-p1.pddl",
    "barman-opt11-strips:pfile01-001.pddl",
    "barman-opt11-strips:pfile01-002.pddl",
    "blocks:probBLOCKS-4-0.pddl",
    "blocks:probBLOCKS-4-1.pddl",
    "data-network-opt18-strips:p01.pddl",
    "data-network-opt18-strips:p02.pddl",
    "depot:p01.pddl",
    "depot:p02.pddl",
    "driverlog:p01.pddl",
    "driverlog:p02.pddl",
    "elevators-opt08-strips:p01.pddl",
    "elevators-opt08-strips:p02.pddl",
    "elevators-opt11-strips:p01.pddl",
    "elevators-opt11-strips:p02.pddl",
    "freecell:p01.pddl",
    "freecell:p02.pddl",
    "ged-opt14-strips:d-1-2.pddl",
    "ged-opt14-strips:d-1-3.pddl",
    "grid:prob01.pddl",
    "grid:prob02.pddl",
    "gripper:prob01.pddl",
    "gripper:prob02.pddl",
    "hiking-opt14-strips:ptesting-1-2-3.pddl",
    "hiking-opt14-strips:ptesting-1-2-4.pddl",
    "logistics00:probLOGISTICS-4-0.pddl",
    "logistics00:probLOGISTICS-4-1.pddl",
    "miconic:s1-0.pddl",
    "miconic:s1-1.pddl",
    "mprime:prob01.pddl",
    "mprime:prob02.pddl",
    "mystery:prob01.pddl",
    "mystery:prob02.pddl",
    "nomystery-opt11-strips:p01.pddl",
    "nomystery-opt11-strips:p02.pddl",
    "openstacks-opt08-strips:p01.pddl",
    "openstacks-opt08-strips:p02.pddl",
    "openstacks-opt11-strips:p01.pddl",
    "openstacks-opt11-strips:p02.pddl",
    "openstacks-opt14-strips:p20_1.pddl",
    "openstacks-opt14-strips:p20_2.pddl",
    "openstacks-strips:p01.pddl",
    "openstacks-strips:p02.pddl",
    "organic-synthesis-opt18-strips:p01.pddl",
    "organic-synthesis-opt18-strips:p02.pddl",
    "organic-synthesis-split-opt18-strips:p01.pddl",
    "organic-synthesis-split-opt18-strips:p02.pddl",
    "parcprinter-08-strips:p01.pddl",
    "parcprinter-08-strips:p02.pddl",
    "parcprinter-opt11-strips:p01.pddl",
    "parcprinter-opt11-strips:p02.pddl",
    "pathways:p01.pddl",
    "pathways:p02.pddl",
    "pegsol-08-strips:p01.pddl",
    "pegsol-08-strips:p02.pddl",
    "pegsol-opt11-strips:p01.pddl",
    "pegsol-opt11-strips:p02.pddl",
    "petri-net-alignment-opt18-strips:p01.pddl",
    "petri-net-alignment-opt18-strips:p02.pddl",
    "pipesworld-notankage:p01-net1-b6-g2.pddl",
    "pipesworld-notankage:p02-net1-b6-g4.pddl",
    "pipesworld-tankage:p01-net1-b6-g2-t50.pddl",
    "pipesworld-tankage:p02-net1-b6-g4-t50.pddl",
    "psr-small:p01-s2-n1-l2-f50.pddl",
    "psr-small:p02-s5-n1-l3-f30.pddl",
    "rovers:p01.pddl",
    "rovers:p02.pddl",
    "satellite:p01-pfile1.pddl",
    "satellite:p02-pfile2.pddl",
    "scanalyzer-08-strips:p01.pddl",
    "scanalyzer-08-strips:p02.pddl",
    "scanalyzer-opt11-strips:p01.pddl",
    "scanalyzer-opt11-strips:p02.pddl",
    "sokoban-opt08-strips:p01.pddl",
    "sokoban-opt08-strips:p02.pddl",
    "sokoban-opt11-strips:p01.pddl",
    "sokoban-opt11-strips:p02.pddl",
    "spider-opt18-strips:p01.pddl",
    "spider-opt18-strips:p02.pddl",
    "storage:p01.pddl",
    "storage:p02.pddl",
    "termes-opt18-strips:p01.pddl",
    "termes-opt18-strips:p02.pddl",
    "tidybot-opt11-strips:p01.pddl",
    "tidybot-opt11-strips:p02.pddl",
    "tpp:p01.pddl",
    "tpp:p02.pddl",
    "transport-opt08-strips:p01.pddl",
    "transport-opt08-strips:p02.pddl",
    "transport-opt11-strips:p01.pddl",
    "transport-opt11-strips:p02.pddl",
    "transport-opt14-strips:p01.pddl",
    "transport-opt14-strips:p02.pddl",
    "trucks-strips:p01.pddl",
    "trucks-strips:p02.pddl",
    "visitall-opt11-strips:problem02-full.pddl",
    "visitall-opt11-strips:problem02-half.pddl",
    "visitall-opt14-strips:p-1-5.pddl",
    "visitall-opt14-strips:p-1-6.pddl",
    "woodworking-opt08-strips:p01.pddl",
    "woodworking-opt08-strips:p02.pddl",
    "woodworking-opt11-strips:p01.pddl",
    "woodworking-opt11-strips:p02.pddl",
    "zenotravel:p01.pddl",
    "zenotravel:p02.pddl",
]


def open_report(exp, outfile: Path):
    outfile = Path(exp.eval_dir) / outfile
    exp.add_step(
        f"open-{outfile.name.split('.')[0]}", subprocess.call, ["xdg-open", outfile]
    )


def _get_exp_dir_relative_to_repo():
    repo_name = get_repo_base().name
    script = Path(tools.get_script_path())
    script_dir = script.parent
    rel_script_dir = script_dir.relative_to(get_repo_base())
    expname = script.stem
    return repo_name / rel_script_dir / "data" / expname


def add_evaluations_per_time(run):
    evaluations = run.get("evaluations")
    time = run.get("search_time")
    if evaluations is not None and evaluations >= 100 and time >= 1:
        run["evaluations_per_time"] = evaluations / time
    return run


def add_difference(diff, val1, val2):
    def diff_func(run):
        if run.get(val1) is None or run.get(val2) is None:
            diff_val = None
        else:
            diff_val = run.get(val1) - run.get(val2)
        run[diff] = diff_val
        return run

    return diff_func


def add_scp_step(exp, login, repos_dir, name="scp-eval-dir"):
    remote_exp = Path(repos_dir) / _get_exp_dir_relative_to_repo()
    exp.add_step(
        name,
        subprocess.call,
        [
            "rsync",
            "-Pavz",
            f"{login}:{remote_exp}-eval/",
            f"{exp.path}-cluster-eval/",
        ],
    )


def get_cluster_eval_dir(exp):
    return str(
        get_repo_base().parent
        / _get_exp_dir_relative_to_repo().parent
        / (_get_exp_dir_relative_to_repo().name + "-cluster-eval")
    )


def add_report(
    exp,
    report_type=AbsoluteReport,
    name=None,
    outfile=None,
    eval_dir=None,
    cluster=False,
    **kwargs,
):
    report = report_type(**kwargs)
    if name and not outfile:
        outfile = f"{name}.{report.output_format}"
    elif outfile and not name:
        name = Path(outfile).name
    elif not name and not outfile:
        name = f"{exp.name}-{report_names[report_type]}"
        outfile = f"{name}.{report.output_format}"
    if not Path(outfile).is_absolute():
        outfile = Path(exp.eval_dir) / outfile

    exp.add_report(report, name=name, outfile=outfile, eval_dir=eval_dir)
    if not REMOTE:
        exp.add_step(f"open-{name}", subprocess.call, ["xdg-open", outfile])
    # exp.add_step(f"publish-{name}", subprocess.call, ["publish", outfile])


def add_compress_exp_dir_step(exp):
    def compress_exp_dir():
        tar_file_path = Path(exp.path).parent / f"{exp.name}.tar.xz"
        exp_dir_path = Path(exp.path)

        with tarfile.open(tar_file_path, mode="w:xz", dereference=True) as tar:
            for file in exp_dir_path.rglob("*"):
                relpath = file.relative_to(exp_dir_path.parent)
                print(f"Adding {relpath}")
                tar.add(file, arcname=relpath)

        shutil.rmtree(exp_dir_path)

    exp.add_step("compress-exp-dir", compress_exp_dir)


def truncate_path(path, max_length):
    if len(path.parts) <= max_length:
        return path
    return Path(*path.parts[:max_length])


class Hardest10Report(PlanningReport):
    """
    Keep the 10 tasks from each domain that are solved by the fewest number of planners.
    """

    def get_text(self):
        solved_by = defaultdict(int)
        for run in self.props.values():
            if run.get("coverage"):
                solved_by[(run["domain"], run["problem"])] += 1
        hardest_tasks = {}
        for domain, problems in sorted(self.domains.items()):
            solved_problems = [
                problem for problem in problems if solved_by[(domain, problem)] > 0
            ]
            solved_problems.sort(key=lambda problem: solved_by[(domain, problem)])
            hardest_tasks[domain] = set(solved_problems[:10])
        for domain, problems in sorted(self.domains.items()):
            print(domain, len(problems), len(hardest_tasks[domain]))
        new_props = tools.Properties()
        for key, run in self.props.items():
            if run["problem"] in hardest_tasks[run["domain"]]:
                new_props[key] = run
        return str(new_props)


def domain_as_category(run1, run2):
    return run1["domain"]


# use with partial e.g, partial(filter_zero_to_nan, attribute="expansions_until_last_jump")
def filter_zero_to_nan(attribute, run):
    if attribute in run and run[attribute] == 0:
        run[attribute] = None
    return run
