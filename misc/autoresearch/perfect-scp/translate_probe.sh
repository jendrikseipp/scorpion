#!/bin/bash
# Translate probe tasks to .autoresearch/sas/<name>.sas
set -uo pipefail
B=/home/x_jense/projects/benchmarks
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SAS="$ROOT/.autoresearch/sas"
mkdir -p "$SAS"

translate() {
    name=$1; shift
    [ -f "$SAS/$name.sas" ] && return 0
    tmp=$(mktemp -d)
    (cd "$tmp" && python3 "$ROOT/fast-downward.py" --translate "$@" >translate.log 2>&1 \
        && mv output.sas "$SAS/$name.sas") || echo "FAILED $name"
    rm -rf "$tmp"
}

translate airport      $B/airport/p16-*.pddl &
translate depot        $B/depot/p07.pddl &
translate driverlog    $B/driverlog/p11.pddl &
translate elevators    $B/elevators-opt08-strips/p11.pddl &
translate freecell     $B/freecell/p03.pddl &
translate logistics    $B/logistics00/probLOGISTICS-9-0.pddl &
translate mprime       $B/mprime/prob07.pddl &
translate pipesworld   $B/pipesworld-notankage/p08-*.pddl &
translate rovers       $B/rovers/p07.pddl &
translate satellite    $B/satellite/p07-*.pddl &
translate scanalyzer   $B/scanalyzer-08-strips/p06.pddl &
translate tpp          $B/tpp/p08.pddl &
translate woodworking  $B/woodworking-opt08-strips/p13.pddl &
translate parcprinter  $B/parcprinter-08-strips/p10.pddl &
wait
ls -la "$SAS"
