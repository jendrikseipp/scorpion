# Scorpion

Scorpion is an optimal classical planner based on saturated cost
partitioning. It is based on version 20.06 of the Fast Downward planning
system (https://github.com/aibasel/downward), which is described below.

Please use the following reference when citing Scorpion:

Jendrik Seipp, Thomas Keller and Malte Helmert. [Saturated Cost
Partitioning for Optimal Classical
Planning](https://www.jair.org/index.php/jair/article/view/11673). Journal
of Artificial Intelligence Research 67, pp. 129-167. 2020.

The code for saturated cost partitioning (SCP) can be found in the
`src/search/cost_saturation` directory. Please see
http://www.fast-downward.org for instructions on how to compile the
planner.

The following configuration uses the strongest heuristic
(h<sup>SCP</sup>-div) from the journal article, i.e., it maximizes over
multiple diverse SCP heuristics:

```
--search "astar(scp([
    projections(hillclimbing(max_generated_patterns=200, random_seed=0)),
    projections(systematic(2)),
    cartesian()],
    max_orders=infinity, max_time=1000, max_optimization_time=100, diversify=true,
    orders=greedy_orders(random_seed=0), random_seed=0))"
```

The version of Scorpion that participated in the IPC 2018 is slightly
different: it uses different timeouts for hill climbing, diversification
and optimization, prunes irrelevant operators in a preprocessing step and
uses partial order reduction during the A* search:

```
--search "astar(scp([
    projections(hillclimbing(max_time=100, random_seed=0)),
    projections(systematic(2)),
    cartesian()],
    max_orders=infinity, max_time=200, max_optimization_time=2, diversify=true,
    orders=greedy_orders(random_seed=0), random_seed=0),
    pruning=stubborn_sets_simple(min_required_pruning_ratio=0.2))"
```

Note that for operator pruning you need to make the [h^2
preprocessor](https://people.cs.aau.dk/~alto/software.html#section1)
available on your `PATH` (e.g., using the name "h2-mutexes") and then pass
`--transform-task h2-mutexes` to the `fast-downward.py` script (in
Downward Lab you can use the `driver_options` argument of `add_algorithm`
for this).

We recommend using the h<sup>SCP</sup>-div configuration when evaluating
changes to Scorpion itself and the Scorpion IPC 2018 configuration when
comparing different planners.

If you want to run exactly the same Scorpion version as in IPC 2018, we
recommend using the Scorpion IPC repo at
https://bitbucket.org/ipc2018-classical/team44/src/ipc-2018-seq-opt/ to
build and run the Scorpion IPC 2018 Singularity image.

# Fast Downward

Fast Downward is a domain-independent classical planning system.

Copyright 2003-2020 Fast Downward contributors (see below).

For further information:
- Fast Downward website: <http://www.fast-downward.org>
- Report a bug or file an issue: <http://issues.fast-downward.org>
- Fast Downward mailing list: <https://groups.google.com/forum/#!forum/fast-downward>
- Fast Downward main repository: <https://github.com/aibasel/downward>


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
