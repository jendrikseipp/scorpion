# Scorpion

Scorpion is an optimal classical planner that uses saturated cost
partitioning. It is based on the Fast Downward planning system
(https://github.com/aibasel/downward), which is described below. We
regularly port the latest changes from Fast Downward to Scorpion.

Please use the following reference when citing Scorpion:

Jendrik Seipp, Thomas Keller and Malte Helmert. [Saturated Cost
Partitioning for Optimal Classical
Planning](https://www.jair.org/index.php/jair/article/view/11673). Journal
of Artificial Intelligence Research 67, pp. 129-167. 2020.

The code for saturated cost partitioning (SCP) can be found in the
`src/search/cost_saturation` directory.


## Instructions

After installing the requirements (see below), compile the planner with

    ./build.py

and see the available options with

    ./fast-downward.py --help

For more details (including build instructions for Windows), see the
documentation about
[compiling](http://www.fast-downward.org/ObtainingAndRunningFastDownward)
and [running](http://www.fast-downward.org/PlannerUsage) the planner.

### Recommended Configuration

We recommend the following configuration, which is similar to one Scorpion
used in the IPC 2018. It prunes irrelevant operators in a preprocessing
step, uses partial order reduction, and maximizes over multiple diverse
SCP heuristics computed for projections and Cartesian abstractions:

```
./fast-downward.py --transform-task builds/release/bin/preprocess-h2
  ../benchmarks/gripper/prob01.pddl
  --search "astar(scp([
    projections(hillclimbing(max_generated_patterns=200, random_seed=0)),
    projections(systematic(2)),
    cartesian()],
    max_orders=infinity, max_time=200, max_optimization_time=2, diversify=true,
    orders=greedy_orders(random_seed=0), random_seed=0),
    pruning=atom_centric_stubborn_sets(min_required_pruning_ratio=0.2))"
```

(In Downward Lab you can use the `driver_options` argument of
`add_algorithm` to specify the `--transform-task` argument.)

If you want to run exactly the same Scorpion version as in IPC 2018, we
recommend using the [Scorpion IPC
repo](https://bitbucket.org/ipc2018-classical/team44/src/ipc-2018-seq-opt/).
It also includes a Singularity image.

# Fast Downward

Fast Downward is a domain-independent classical planning system.

Copyright 2003-2020 Fast Downward contributors (see below).

For further information:
- Fast Downward website: <http://www.fast-downward.org>
- Report a bug or file an issue: <http://issues.fast-downward.org>
- Fast Downward mailing list: <https://groups.google.com/forum/#!forum/fast-downward>
- Fast Downward main repository: <https://github.com/aibasel/downward>


## Tested software versions

This version of Fast Downward has been tested with the following software versions:

| OS           | Python | C++ compiler                                                     | CMake |
| ------------ | ------ | ---------------------------------------------------------------- | ----- |
| Ubuntu 20.04 | 3.8    | GCC 9, GCC 10, Clang 10, Clang 11                                | 3.16  |
| Ubuntu 18.04 | 3.6    | GCC 7, Clang 6                                                   | 3.10  |
| macOS 10.15  | 3.6    | AppleClang 12                                                    | 3.19  |
| Windows 10   | 3.6    | Visual Studio Enterprise 2017 (MSVC 19.16) and 2019 (MSVC 19.28) | 3.19  |

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

- 2003-2020 Malte Helmert
- 2008-2016, 2018-2020 Gabriele Roeger
- 2010-2020 Jendrik Seipp
- 2010-2011, 2013-2020 Silvan Sievers
- 2012-2020 Florian Pommerening
- 2013, 2015-2020 Salome Eriksson
- 2016-2020 Cedric Geissmann
- 2017-2020 Guillem Francès
- 2018-2020 Augusto B. Corrêa
- 2018-2020 Patrick Ferber
- 2015-2019 Manuel Heusner
- 2017 Daniel Killenberger
- 2016 Yusra Alkhazraji
- 2016 Martin Wehrle
- 2014-2015 Patrick von Reth
- 2015 Thomas Keller
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
