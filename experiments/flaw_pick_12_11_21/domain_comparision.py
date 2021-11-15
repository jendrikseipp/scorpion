from collections import defaultdict
import itertools

from lab.reports import Table

from downward.reports import PlanningReport

DOMAIN_GROUPS = {
    "airport": ["airport"],
    "assembly": ["assembly"],
    "barman": ["barman", "barman-opt11-strips", "barman-opt14-strips", "barman-sat11-strips", "barman-sat14-strips"],
    "blocksworld": ["blocks", "blocksworld"],
    "cavediving": ["cavediving-14-adl"],
    "childsnack": ["childsnack-opt14-strips", "childsnack-sat14-strips"],
    "citycar": ["citycar-opt14-adl", "citycar-sat14-adl"],
    "depots": ["depot", "depots"],
    "driverlog": ["driverlog"],
    "elevators": ["elevators-opt08-strips", "elevators-opt11-strips", "elevators-sat08-strips", "elevators-sat11-strips"],
    "floortile": ["floortile-opt11-strips", "floortile-opt14-strips", "floortile-sat11-strips", "floortile-sat14-strips"],
    "freecell": ["freecell"],
    "ged": ["ged-opt14-strips", "ged-sat14-strips"],
    "grid": ["grid"],
    "gripper": ["gripper"],
    "hiking": ["hiking-opt14-strips", "hiking-sat14-strips"],
    "logistics": ["logistics98", "logistics00"],
    "maintenance": ["maintenance-opt14-adl", "maintenance-sat14-adl"],
    "miconic": ["miconic", "miconic-strips"],
    "miconic-fulladl": ["miconic-fulladl"],
    "miconic-simpleadl": ["miconic-simpleadl"],
    "movie": ["movie"],
    "mprime": ["mprime"],
    "mystery": ["mystery"],
    "nomystery": ["nomystery-opt11-strips", "nomystery-sat11-strips"],
    "openstacks": ["openstacks", "openstacks-strips", "openstacks-opt08-strips", "openstacks-opt11-strips", "openstacks-opt14-strips", "openstacks-sat08-adl", "openstacks-sat08-strips", "openstacks-sat11-strips", "openstacks-sat14-strips", "openstacks-opt08-adl", "openstacks-sat08-adl"],
    "optical-telegraphs": ["optical-telegraphs"],
    "parcprinter": ["parcprinter-08-strips", "parcprinter-opt11-strips", "parcprinter-sat11-strips"],
    "parking": ["parking-opt11-strips", "parking-opt14-strips", "parking-sat11-strips", "parking-sat14-strips"],
    "pathways": ["pathways"],
    "pathways-noneg": ["pathways-noneg"],
    "pegsol": ["pegsol-08-strips", "pegsol-opt11-strips", "pegsol-sat11-strips"],
    "philosophers": ["philosophers"],
    "pipes-nt": ["pipesworld-notankage"],
    "pipes-t": ["pipesworld-tankage"],
    "psr": ["psr-middle", "psr-large", "psr-small"],
    "rovers": ["rover", "rovers"],
    "satellite": ["satellite"],
    "scanalyzer": ["scanalyzer-08-strips", "scanalyzer-opt11-strips", "scanalyzer-sat11-strips"],
    "schedule": ["schedule"],
    "sokoban": ["sokoban-opt08-strips", "sokoban-opt11-strips", "sokoban-sat08-strips", "sokoban-sat11-strips"],
    "storage": ["storage"],
    "tetris": ["tetris-opt14-strips", "tetris-sat14-strips"],
    "thoughtful": ["thoughtful-sat14-strips"],
    "tidybot": ["tidybot-opt11-strips", "tidybot-opt14-strips", "tidybot-sat11-strips", "tidybot-sat14-strips"],
    "tpp": ["tpp"],
    "transport": ["transport-opt08-strips", "transport-opt11-strips", "transport-opt14-strips", "transport-sat08-strips", "transport-sat11-strips", "transport-sat14-strips"],
    "trucks": ["trucks", "trucks-strips"],
    "visitall": ["visitall-opt11-strips", "visitall-opt14-strips", "visitall-sat11-strips", "visitall-sat14-strips"],
    "woodworking": ["woodworking-opt08-strips", "woodworking-opt11-strips", "woodworking-sat08-strips", "woodworking-sat11-strips"],
    "zenotravel": ["zenotravel"],
    # IPC 2018:
    "agricola": ["agricola", "agricola-opt18-strips", "agricola-sat18-strips"],
    "caldera": ["caldera-opt18-adl", "caldera-sat18-adl"],
    "caldera-split": ["caldera-split-opt18-adl", "caldera-split-sat18-adl"],
    "data-network": ["data-network", "data-network-opt18-strips", "data-network-sat18-strips"],
    "flashfill": ["flashfill-sat18-adl"],
    "nurikabe": ["nurikabe-opt18-adl", "nurikabe-sat18-adl"],
    "organic-split": ["organic-synthesis-split", "organic-synthesis-split-opt18-strips", "organic-synthesis-split-sat18-strips"],
    "organic" : ["organic-synthesis", "organic-synthesis-opt18-strips", "organic-synthesis-sat18-strips"],
    "petri-net": ["petri-net-alignment", "petri-net-alignment-opt18-strips", "petri-net-alignment-sat18-strips"],
    "settlers": ["settlers-opt18-adl", "settlers-sat18-adl"],
    "snake": ["snake", "snake-opt18-strips", "snake-sat18-strips"],
    "spider": ["spider", "spider-opt18-strips", "spider-sat18-strips"],
    "termes": ["termes", "termes-opt18-strips", "termes-sat18-strips"],
}

DOMAIN_RENAMINGS = {}
for group_name, domains in DOMAIN_GROUPS.items():
    for domain in domains:
        DOMAIN_RENAMINGS[domain] = group_name
for group_name in DOMAIN_GROUPS:
    DOMAIN_RENAMINGS[group_name] = group_name


def group_domains(run):
    old_domain = run["domain"]
    run["domain"] = DOMAIN_RENAMINGS[old_domain]
    run["problem"] = old_domain + "-" + run["problem"]
    run["id"][2] = run["problem"]
    return run


class PerDomainComparison(PlanningReport):
    def __init__(self, sort=True, sstddev=None, digits=0, **kwargs):
        """
        If *sort* is True, sort algorithms from "weakest" to
        "strongest". The "strength" of an algorithm A is the number of
        other algorithms against which A "wins" in a per-domain
        comparison.

        If given, *stddev* must be a dictionary mapping from algorithm
        names to standard deviation values.

        *digits* is the decimal precision used for the coverage scores.

        """
        PlanningReport.__init__(self, **kwargs)
        self.sort = sort
        self.sstddev = sstddev or {}
        self.digits = digits
        self.attribute = self.attributes[0] if len(self.attributes) == 1 else "coverage"

    def get_markup(self):
        domain_and_algorithm_to_coverage = defaultdict(int)
        for (domain, problem), runs in self.problem_runs.items():
            for run in runs:
                domain_and_algorithm_to_coverage[(run["domain"], run["algorithm"])] += run[self.attribute]

        algorithms = self.algorithms
        domain_groups = sorted(set([group for group, _ in domain_and_algorithm_to_coverage.keys()]))
        print("{} domains: {}".format(len(domain_groups), domain_groups))

        num_domains_better = defaultdict(int)
        for algo1, algo2 in itertools.combinations(algorithms, 2):
            for domain in domain_groups:
                coverage1 = domain_and_algorithm_to_coverage[(domain, algo1)]
                coverage2 = domain_and_algorithm_to_coverage[(domain, algo2)]
                if coverage1 > coverage2:
                    num_domains_better[(algo1, algo2)] += 1
                elif coverage2 > coverage1:
                    num_domains_better[(algo2, algo1)] += 1

        def get_coverage(algo):
            return sum(domain_and_algorithm_to_coverage[(domain, algo)] for domain in domain_groups)

        def get_wins(algo1):
            num_wins = 0
            num_strict_wins = 0
            for algo2 in self.algorithms:
                if algo1 == algo2:
                    continue
                num_algo1_better = 0
                num_algo2_better = 0
                for domain in domain_groups:
                    coverage1 = domain_and_algorithm_to_coverage[(domain, algo1)]
                    coverage2 = domain_and_algorithm_to_coverage[(domain, algo2)]
                    if coverage1 > coverage2:
                        num_algo1_better += 1
                    elif coverage2 > coverage1:
                        num_algo2_better += 1

                if num_domains_better[(algo1, algo2)] >= num_domains_better[(algo2, algo1)]:
                    num_wins += 1
                if num_domains_better[(algo1, algo2)] > num_domains_better[(algo2, algo1)]:
                    num_strict_wins += 1
            return num_wins, num_strict_wins

        def get_wins_and_coverage(algo):
            return (get_wins(algo), get_coverage(algo))

        if self.sort:
            algorithms = sorted(algorithms, key=get_wins_and_coverage)

        comparison_table = Table()
        comparison_table.set_row_order(algorithms)
        comparison_table.set_column_order(algorithms + ["Coverage"])
        comparison_table.row_min_wins["Coverage"] = False
        for algo1, algo2 in itertools.permutations(algorithms, 2):
            num_algo1_better = num_domains_better[(algo1, algo2)]
            num_algo2_better = num_domains_better[(algo2, algo1)]
            if num_algo1_better >= num_algo2_better:
                if self.output_format == "tex":
                    content = r" ''\textbf{{{}}}''".format(num_algo1_better)
                else:
                    content = r" ''<b>{}</b>''".format(num_algo1_better)
            else:
                content = num_algo1_better
            comparison_table.add_cell(algo1, algo2, content)
        for algo in algorithms:
            comparison_table.add_cell(algo, algo, " ''--''")

        total_coverage = dict(
            (algo, sum(domain_and_algorithm_to_coverage[(domain, algo)] for domain in domain_groups))
            for algo in algorithms)

        def print_line(cells):
            print(" & ".join(str(c) for c in cells) + r" \\")

        include_sstddev = bool(self.sstddev)
        max_coverage = max(get_coverage(algo) for algo in algorithms)
        print(r"\newcommand{\bc}[1]{\textbf{#1}}")
        print(r"\newcommand{\rot}[1]{\rotatebox{90}{#1}}")
        print(r"\setlength{\tabcolsep}{3pt}")
        print(r"\centering")
        print(r"\begin{tabular}{l" + "r" * len(algorithms) + "cr}")
        line = [""] + [r"\rot{%s}" % c for c in algorithms] + ["", "\\rot{Coverage}"]
        if include_sstddev:
            line.append(r"\rot{Stddev.}")
        print_line(line)
        offsets = tuple([offset + len(algorithms) for offset in (1, 3, 4 if include_sstddev else 3)])
        print("\cmidrule[\lightrulewidth]{1-%d} \cmidrule[\lightrulewidth]{%d-%d}" % offsets)
        for algo1 in algorithms:
            total_coverage = get_coverage(algo1)
            if self.digits == 0:
                total_coverage = str(total_coverage)
            else:
                total_coverage = "{total_coverage:.{digits}f}".format(
                    total_coverage=float(total_coverage), digits=self.digits)
            if get_coverage(algo1) == max_coverage:
                total_coverage = r"\bc{{{}}}".format(total_coverage)
            sstddev = self.sstddev.get(algo1)
            line = []
            for algo2 in algorithms:
                num_algo1_better = 0
                num_algo2_better = 0
                for domain in domain_groups:
                    coverage1 = domain_and_algorithm_to_coverage[(domain, algo1)]
                    coverage2 = domain_and_algorithm_to_coverage[(domain, algo2)]
                    if coverage1 > coverage2:
                        num_algo1_better += 1
                    elif coverage2 > coverage1:
                        num_algo2_better += 1

                if algo1 == algo2:
                    entry = "--"
                elif num_algo1_better >= num_algo2_better:
                    entry = "\\bc{{{}}}".format(num_algo1_better)
                else:
                    entry = str(num_algo1_better)
                line.append(entry)
            line = [algo1] + line + ["", total_coverage]
            if include_sstddev:
                line.append("{:.2f}".format(sstddev) if sstddev is not None else "--")
            print_line(line)
        print(r"\end{tabular}")

        return str(comparison_table)
