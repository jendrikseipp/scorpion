# Scorpion

Scorpion is a classical planning system that extends [Fast
Downward](https://www.fast-downward.org). The main extensions are:

* novel state-of-the-art algorithms for optimal classical planning
* additional search algorithms
* several new plugin options and utilities

See [below](#differences-between-scorpion-and-fast-downward) for a detailed list
of extensions. We regularly port the latest changes from Fast Downward to
Scorpion and also integrate some features from Scorpion back into Fast Downward.

Please use the following reference when citing Scorpion:
Jendrik Seipp, Thomas Keller and Malte Helmert.
[Saturated Cost Partitioning for Optimal Classical Planning](
https://www.jair.org/index.php/jair/article/view/11673).
Journal of Artificial Intelligence Research 67, pp. 129-167. 2020.


## Instructions

Install the dependencies (the table below lists which versions are tested):

    sudo apt install cmake g++ git make python3

For plugins based on linear programming (e.g., `ocp()`, `pho()`) you need
to [add an LP solver](https://www.fast-downward.org/LPBuildInstructions). Then
compile the planner with

    ./build.py

and see the available options with

    ./fast-downward.py --help  # driver
    ./fast-downward.py --search -- --help  # search component

For more details (including build instructions for macOS and Windows), see the
documentation about
[compiling](https://www.fast-downward.org/ObtainingAndRunningFastDownward)
and [running](https://www.fast-downward.org/PlannerUsage) the planner. The
[plugin documentation](https://jendrikseipp.github.io/scorpion) shows
which plugins are available (heuristics, search algorithms, etc.) and how
to use them.


### Recommended configuration

We recommend using the following configuration:

```
./fast-downward.py \
  --transform-task preprocess-h2 \
  ../benchmarks/gripper/prob01.pddl \
  --search "astar(scp_online([
        projections(sys_scp(max_time=100, max_time_per_restart=10)),
        cartesian()],
        saturator=perimstar, max_time=1000, interval=10K, orders=greedy_orders()),
        pruning=limited_pruning(pruning=atom_centric_stubborn_sets(), min_required_pruning_ratio=0.2))"
```

The `preprocess-h2` call prunes irrelevant operators in a preprocessing
step. The search configuration uses [partial order
reduction](https://ojs.aaai.org/index.php/SOCS/article/view/18535) and
maximizes over
[diverse](https://www.jair.org/index.php/jair/article/view/11673),
[subset-saturated](https://ojs.aaai.org/index.php/ICAPS/article/view/3503)
cost partitioning heuristics computed
[online](https://ojs.aaai.org/index.php/ICAPS/article/view/15976/) during
the search. The underlying abstractions are [Sys-SCP pattern
databases](https://www.ijcai.org/proceedings/2019/780) and [Cartesian
abstractions](https://jair.org/index.php/jair/article/view/11217).

(In [Downward Lab](https://lab.readthedocs.io/) you can use
`add_algorithm(name="scorpion", repo="path/to/repo", rev="scorpion",
component_options=[], driver_options=["--transform-task", "preprocess-h2",
"--alias", "scorpion"]` to run the recommended Scorpion configuration.)

#### Apptainer image

To simplify the installation process, we provide an executable
[Apptainer](https://apptainer.org/) container (formerly known as Singularity).
It accepts the same arguments as the `fast-downward.py` script (see above).

    # Download the image,
    apptainer pull scorpion.sif oras://ghcr.io/jendrikseipp/scorpion:latest

    # or build it yourself.
    apptainer build scorpion.sif Apptainer

    # Then run recommended configuration (available via "scorpion" alias).
    ./scorpion.sif --transform-task preprocess-h2 --alias scorpion PROBLEM_FILE

### IPC 2018 version

If you prefer to run the Scorpion version from IPC 2018 (which uses an
older Fast Downward version and different abstractions), we recommend
using the [Scorpion IPC
repo](https://bitbucket.org/ipc2018-classical/team44/src/ipc-2018-seq-opt/).


## Differences between Scorpion and Fast Downward

Diff between the latest merged version of Fast Downward and Scorpion:
https://github.com/jendrikseipp/scorpion/compare/main...scorpion

- Scorpion comes with the
  [h²-preprocessor](https://ojs.aaai.org/index.php/ICAPS/article/view/13708)
  by Vidal Alcázar and Álvaro Torralba that prunes irrelevant operators.
  Pass `--transform-task preprocess-h2` to use it.
- The `--transform-task` command allows you to run arbitrary preprocessing
  commands that transform the SAS+ output from the translator before
  passing it to the search.
- Scorpion uses a
  [phmap::flat_hash_set](https://github.com/greg7mdp/parallel-hashmap) to check
  for duplicate states, which often drastically reduces the peak memory usage,
  compared to Fast Downward's `IntHashSet`.
- If [ccache](https://ccache.dev/) is installed (recommended), Scorpion
  uses it to cache compilation files.

### New translator options

- Use `--dump-predicates` and `--dump-static-atoms` to write files with
  information that's useful for learning domain control knowledge.


### New plugin options

- `{cegar/cartesian}(..., search_strategy=incremental)`: use [incremental search for
  Cartesian abstraction
  refinement](https://ojs.aaai.org/index.php/ICAPS/article/view/6667)
  (default).

- `{cegar/cartesian}(..., pick_flawed_abstract_state={batch_min_h, ...})`:
  find all current flaws, then iteratively repair the flaw that's closest to the goal
  ([paper](https://ojs.aaai.org/index.php/ICAPS/article/view/19819), default=`batch_min_h`).

- `{cegar/cartesian}(..., pick_split={max_cover, ...}, tiebreak_split={max_refined, ...})`:
  smarter strategies for splitting a flawed abstract state
  ([paper](https://ojs.aaai.org/index.php/ICAPS/article/view/19819), default=`max_cover`
  and `max_refined` for tiebreaking).

- `{cegar,cartesian}(..., dot_graph_verbosity={silent, write_to_console, write_to_file})`:
  write intermediate abstractions as Graphviz dot files to stdout or to files (default=`silent`).

- `hillclimbing(..., max_generated_patterns=200)`: limit the number of
  patterns generated by hill climbing.

- `systematic(..., pattern_type=interesting_general)`: compute interesting
  patterns for general cost partitioning.


### New cost partitioning algorithms for abstraction heuristics

We use Cartesian abstractions in the example configurations below
(`[cartesian()]`). You can also use pattern database heuristics, e.g.,
`[projections(systematic(2))]`, or mix abstractions, e.g.,
`[projections(systematic(3)), cartesian()]`. Some of the algorithms below
are also part of vanilla Fast Downward, but are only implemented for PDB
heuristics.

- Optimal cost partitioning:
  `ocp([cartesian()])`
- Canonical heuristic:
  `canonical_heuristic([cartesian()])`
- Uniform cost partitioning:
  `ucp([cartesian()], opportunistic=false)`
- Opportunistic uniform cost partitioning:
  `ucp([cartesian()], ..., opportunistic=true)`
- Greedy zero-one cost partitioning:
  `gzocp([cartesian()], ...)`
- Saturated cost partitioning:
  `scp([cartesian()], ...)` (offline), `scp_online([cartesian()], ...)` (online)
- (Saturated) post-hoc optimization:
  `pho([cartesian()], ..., saturated={false,true})` (offline),
  `operatorcounting([pho_abstraction_constraints([cartesian()], saturated={false,true})])` (online)

You can also compute the maximum over abstraction heuristics:

- `maximize([cartesian()])`

The plugin documentation shows all options for [cost partitioning
heuristics](https://jendrikseipp.github.io/scorpion/Evaluator/#cost_partitioning_heuristics).

## New pattern collection generators

- Systematic patterns with size limits:
  `sys_scp(max_pattern_size=X, max_pdb_size=Y, max_collection_size=Z, ..., saturate=false)`
- Sys-SCP patterns:
  `sys_scp(...)`


### New cost partitioning algorithms for landmark heuristics

Example using A* search and saturated cost partitioning over BJOLP
landmarks:

    --evaluator
      "lmc=lmcount(lm_merged([lm_rhw(), lm_hm(m=1)]),
      admissible=true, cost_partitioning=suboptimal, greedy=true,
      reuse_costs=true, scoring_function=max_heuristic_per_stolen_costs)"
    --search
      "astar(lmc, lazy_evaluator=lmc)"

Different cost partitioning algorithms (all need `admissible=true`):

- Optimal cost partitioning (part of vanilla Fast Downward):
  `lmcount(..., cost_partitioning=optimal)`
- Canonical heuristic:
  `lmcount(..., cost_partitioning=canonical)`
- Post-hoc optimization:
  `lmcount(..., cost_partitioning=pho)`
- Uniform cost partitioning:
  `lmcount(..., cost_partitioning=suboptimal, greedy=false, reuse_costs=false)`
- Opportunistic uniform cost partitioning (part of vanilla Fast Downward):
  `lmcount(..., cost_partitioning=suboptimal, greedy=false, reuse_costs=true, scoring_function=min_stolen_costs)`
- Greedy zero-one cost partitioning:
  `lmcount(..., cost_partitioning=suboptimal, greedy=true, reuse_costs=false, scoring_function=max_heuristic)`
- Saturated cost partitioning:
  `lmcount(..., cost_partitioning=suboptimal, greedy=true, reuse_costs=true, scoring_function=max_heuristic_per_stolen_costs)`


## New search engines

- Breadth-first search (without overhead of the more general `eager()` search):
  `brfs()`
- Depth-first search:
  `dfs()`
- Exhaustive search (useful for dumping the reachable state space of small input tasks):
  `dump_reachable_search_space()`
- IDA* search:
  `idastar(cegar(cache_estimates=false))`
- Iterative width search:
  `iw(width=2)`

---


<img src="misc/images/fast-downward.svg" width="800" alt="Fast Downward">

Fast Downward is a domain-independent classical planning system.

Copyright 2003-2022 Fast Downward contributors (see below).

For further information:
- Fast Downward website: <https://www.fast-downward.org>
- Report a bug or file an issue: <https://issues.fast-downward.org>
- Fast Downward mailing list: <https://groups.google.com/forum/#!forum/fast-downward>
- Fast Downward main repository: <https://github.com/aibasel/downward>


## Tested software versions

This version of Fast Downward has been tested with the following software versions:

| OS           | Python | C++ compiler                                                     | CMake |
| ------------ | ------ | ---------------------------------------------------------------- | ----- |
| Ubuntu 22.04 | 3.10   | GCC 11, GCC 12, Clang 14                                         | 3.22  |
| Ubuntu 20.04 | 3.8    | GCC 9, GCC 10, Clang 10, Clang 11                                | 3.16  |
| macOS 12     | 3.10   | AppleClang 14                                                    | 3.24  |
| macOS 11     | 3.8    | AppleClang 13                                                    | 3.24  |
| Windows 10   | 3.8    | Visual Studio Enterprise 2019 (MSVC 19.29) and 2022 (MSVC 19.31) | 3.22  |

We test LP support with CPLEX 12.9, SoPlex 3.1.1 and Osi 0.107.9.
On Ubuntu, we test both CPLEX and SoPlex. On Windows, we currently
only test CPLEX, and on macOS, we do not test LP solvers (yet).


## Contributors

The following list includes all people that actively contributed to
Fast Downward, i.e. all people that appear in some commits in Fast
Downward's history (see below for a history on how Fast Downward
emerged) or people that influenced the development of such commits.
Currently, this list is sorted by the last year the person has been
active, and in case of ties, by the earliest year the person started
contributing, and finally by last name.

- 2003-2022 Malte Helmert
- 2008-2016, 2018-2022 Gabriele Roeger
- 2010-2022 Jendrik Seipp
- 2010-2011, 2013-2022 Silvan Sievers
- 2012-2022 Florian Pommerening
- 2013, 2015-2022 Salomé Eriksson
- 2015, 2021-2022 Thomas Keller
- 2018-2022 Patrick Ferber
- 2021-2022 Clemens Büchner
- 2021-2022 Dominik Drexler
- 2022 Remo Christen
- 2016-2020 Cedric Geissmann
- 2017-2020 Guillem Francès
- 2018-2020 Augusto B. Corrêa
- 2020 Rik de Graaff
- 2015-2019 Manuel Heusner
- 2017 Daniel Killenberger
- 2016 Yusra Alkhazraji
- 2016 Martin Wehrle
- 2014-2015 Patrick von Reth
- 2009-2014 Erez Karpas
- 2014 Robert P. Goldman
- 2010-2012 Andrew Coles
- 2010, 2012 Patrik Haslum
- 2003-2011 Silvia Richter
- 2009-2011 Emil Keyder
- 2010-2011 Moritz Gronbach
- 2010-2011 Manuela Ortlieb
- 2011 Vidal Alcázar Saiz
- 2011 Michael Katz
- 2011 Raz Nissim
- 2010 Moritz Goebelbecker
- 2007-2009 Matthias Westphal
- 2009 Christian Muise


## History

The current version of Fast Downward is the merger of three different
projects:

- the original version of Fast Downward developed by Malte Helmert
  and Silvia Richter
- LAMA, developed by Silvia Richter and Matthias Westphal based on
  the original Fast Downward
- FD-Tech, a modified version of Fast Downward developed by Erez
  Karpas and Michael Katz based on the original code

In addition to these three main sources, the codebase incorporates
code and features from numerous branches of the Fast Downward codebase
developed for various research papers. The main contributors to these
branches are Malte Helmert, Gabi Röger and Silvia Richter.


## License

The following directory is not part of Fast Downward as covered by
this license:

- ./src/search/ext

For the rest, the following license applies:

```
Fast Downward is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

Fast Downward is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
```
