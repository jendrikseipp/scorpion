begin_version
3
end_version
begin_metric
0
end_metric
14
begin_variable
var0
-1
3
Atom at(truck1, depot0)
Atom at(truck1, distributor0)
Atom at(truck1, distributor1)
end_variable
begin_variable
var1
-1
3
Atom at(truck0, depot0)
Atom at(truck0, distributor0)
Atom at(truck0, distributor1)
end_variable
begin_variable
var2
-1
2
Atom clear(pallet0)
NegatedAtom clear(pallet0)
end_variable
begin_variable
var3
-1
2
Atom clear(pallet1)
NegatedAtom clear(pallet1)
end_variable
begin_variable
var4
-1
2
Atom clear(pallet2)
NegatedAtom clear(pallet2)
end_variable
begin_variable
var5
-1
2
Atom available(hoist0)
NegatedAtom available(hoist0)
end_variable
begin_variable
var6
-1
2
Atom available(hoist1)
NegatedAtom available(hoist1)
end_variable
begin_variable
var7
-1
2
Atom available(hoist2)
NegatedAtom available(hoist2)
end_variable
begin_variable
var8
-1
4
Atom at(crate0, depot0)
Atom at(crate0, distributor0)
Atom at(crate0, distributor1)
<none of those>
end_variable
begin_variable
var9
-1
4
Atom at(crate1, depot0)
Atom at(crate1, distributor0)
Atom at(crate1, distributor1)
<none of those>
end_variable
begin_variable
var10
-1
2
Atom clear(crate0)
NegatedAtom clear(crate0)
end_variable
begin_variable
var11
-1
2
Atom clear(crate1)
NegatedAtom clear(crate1)
end_variable
begin_variable
var12
-1
9
Atom in(crate0, truck0)
Atom in(crate0, truck1)
Atom lifting(hoist0, crate0)
Atom lifting(hoist1, crate0)
Atom lifting(hoist2, crate0)
Atom on(crate0, crate1)
Atom on(crate0, pallet0)
Atom on(crate0, pallet1)
Atom on(crate0, pallet2)
end_variable
begin_variable
var13
-1
9
Atom in(crate1, truck0)
Atom in(crate1, truck1)
Atom lifting(hoist0, crate1)
Atom lifting(hoist1, crate1)
Atom lifting(hoist2, crate1)
Atom on(crate1, crate0)
Atom on(crate1, pallet0)
Atom on(crate1, pallet1)
Atom on(crate1, pallet2)
end_variable
10
begin_mutex_group
8
8 0
8 1
8 2
12 0
12 1
12 2
12 3
12 4
end_mutex_group
begin_mutex_group
8
9 0
9 1
9 2
13 0
13 1
13 2
13 3
13 4
end_mutex_group
begin_mutex_group
3
5 0
12 2
13 2
end_mutex_group
begin_mutex_group
3
6 0
12 3
13 3
end_mutex_group
begin_mutex_group
3
7 0
12 4
13 4
end_mutex_group
begin_mutex_group
7
10 0
12 0
12 1
12 2
12 3
12 4
13 5
end_mutex_group
begin_mutex_group
7
11 0
12 5
13 0
13 1
13 2
13 3
13 4
end_mutex_group
begin_mutex_group
3
2 0
12 6
13 6
end_mutex_group
begin_mutex_group
3
3 0
12 7
13 7
end_mutex_group
begin_mutex_group
3
4 0
12 8
13 8
end_mutex_group
begin_state
0
2
1
1
0
0
0
0
1
0
0
0
7
6
end_state
begin_goal
2
12 8
13 7
end_goal
72
begin_operator
drive truck0 depot0 distributor0
0
1
0 1 0 1
1
end_operator
begin_operator
drive truck0 depot0 distributor1
0
1
0 1 0 2
1
end_operator
begin_operator
drive truck0 distributor0 depot0
0
1
0 1 1 0
1
end_operator
begin_operator
drive truck0 distributor0 distributor1
0
1
0 1 1 2
1
end_operator
begin_operator
drive truck0 distributor1 depot0
0
1
0 1 2 0
1
end_operator
begin_operator
drive truck0 distributor1 distributor0
0
1
0 1 2 1
1
end_operator
begin_operator
drive truck1 depot0 distributor0
0
1
0 0 0 1
1
end_operator
begin_operator
drive truck1 depot0 distributor1
0
1
0 0 0 2
1
end_operator
begin_operator
drive truck1 distributor0 depot0
0
1
0 0 1 0
1
end_operator
begin_operator
drive truck1 distributor0 distributor1
0
1
0 0 1 2
1
end_operator
begin_operator
drive truck1 distributor1 depot0
0
1
0 0 2 0
1
end_operator
begin_operator
drive truck1 distributor1 distributor0
0
1
0 0 2 1
1
end_operator
begin_operator
drop hoist0 crate0 crate1 depot0
1
9 0
5
0 8 -1 0
0 5 -1 0
0 10 -1 0
0 11 0 1
0 12 2 5
1
end_operator
begin_operator
drop hoist0 crate0 pallet0 depot0
0
5
0 8 -1 0
0 5 -1 0
0 10 -1 0
0 2 0 1
0 12 2 6
1
end_operator
begin_operator
drop hoist0 crate1 crate0 depot0
1
8 0
5
0 9 -1 0
0 5 -1 0
0 10 0 1
0 11 -1 0
0 13 2 5
1
end_operator
begin_operator
drop hoist0 crate1 pallet0 depot0
0
5
0 9 -1 0
0 5 -1 0
0 11 -1 0
0 2 0 1
0 13 2 6
1
end_operator
begin_operator
drop hoist1 crate0 crate1 distributor0
1
9 1
5
0 8 -1 1
0 6 -1 0
0 10 -1 0
0 11 0 1
0 12 3 5
1
end_operator
begin_operator
drop hoist1 crate0 pallet1 distributor0
0
5
0 8 -1 1
0 6 -1 0
0 10 -1 0
0 3 0 1
0 12 3 7
1
end_operator
begin_operator
drop hoist1 crate1 crate0 distributor0
1
8 1
5
0 9 -1 1
0 6 -1 0
0 10 0 1
0 11 -1 0
0 13 3 5
1
end_operator
begin_operator
drop hoist1 crate1 pallet1 distributor0
0
5
0 9 -1 1
0 6 -1 0
0 11 -1 0
0 3 0 1
0 13 3 7
1
end_operator
begin_operator
drop hoist2 crate0 crate1 distributor1
1
9 2
5
0 8 -1 2
0 7 -1 0
0 10 -1 0
0 11 0 1
0 12 4 5
1
end_operator
begin_operator
drop hoist2 crate0 pallet2 distributor1
0
5
0 8 -1 2
0 7 -1 0
0 10 -1 0
0 4 0 1
0 12 4 8
1
end_operator
begin_operator
drop hoist2 crate1 crate0 distributor1
1
8 2
5
0 9 -1 2
0 7 -1 0
0 10 0 1
0 11 -1 0
0 13 4 5
1
end_operator
begin_operator
drop hoist2 crate1 pallet2 distributor1
0
5
0 9 -1 2
0 7 -1 0
0 11 -1 0
0 4 0 1
0 13 4 8
1
end_operator
begin_operator
lift hoist0 crate0 crate1 depot0
0
5
0 8 0 3
0 5 0 1
0 10 0 1
0 11 -1 0
0 12 5 2
1
end_operator
begin_operator
lift hoist0 crate0 pallet0 depot0
0
5
0 8 0 3
0 5 0 1
0 10 0 1
0 2 -1 0
0 12 6 2
1
end_operator
begin_operator
lift hoist0 crate0 pallet1 depot0
0
5
0 8 0 3
0 5 0 1
0 10 0 1
0 3 -1 0
0 12 7 2
1
end_operator
begin_operator
lift hoist0 crate0 pallet2 depot0
0
5
0 8 0 3
0 5 0 1
0 10 0 1
0 4 -1 0
0 12 8 2
1
end_operator
begin_operator
lift hoist0 crate1 crate0 depot0
0
5
0 9 0 3
0 5 0 1
0 10 -1 0
0 11 0 1
0 13 5 2
1
end_operator
begin_operator
lift hoist0 crate1 pallet0 depot0
0
5
0 9 0 3
0 5 0 1
0 11 0 1
0 2 -1 0
0 13 6 2
1
end_operator
begin_operator
lift hoist0 crate1 pallet1 depot0
0
5
0 9 0 3
0 5 0 1
0 11 0 1
0 3 -1 0
0 13 7 2
1
end_operator
begin_operator
lift hoist0 crate1 pallet2 depot0
0
5
0 9 0 3
0 5 0 1
0 11 0 1
0 4 -1 0
0 13 8 2
1
end_operator
begin_operator
lift hoist1 crate0 crate1 distributor0
0
5
0 8 1 3
0 6 0 1
0 10 0 1
0 11 -1 0
0 12 5 3
1
end_operator
begin_operator
lift hoist1 crate0 pallet0 distributor0
0
5
0 8 1 3
0 6 0 1
0 10 0 1
0 2 -1 0
0 12 6 3
1
end_operator
begin_operator
lift hoist1 crate0 pallet1 distributor0
0
5
0 8 1 3
0 6 0 1
0 10 0 1
0 3 -1 0
0 12 7 3
1
end_operator
begin_operator
lift hoist1 crate0 pallet2 distributor0
0
5
0 8 1 3
0 6 0 1
0 10 0 1
0 4 -1 0
0 12 8 3
1
end_operator
begin_operator
lift hoist1 crate1 crate0 distributor0
0
5
0 9 1 3
0 6 0 1
0 10 -1 0
0 11 0 1
0 13 5 3
1
end_operator
begin_operator
lift hoist1 crate1 pallet0 distributor0
0
5
0 9 1 3
0 6 0 1
0 11 0 1
0 2 -1 0
0 13 6 3
1
end_operator
begin_operator
lift hoist1 crate1 pallet1 distributor0
0
5
0 9 1 3
0 6 0 1
0 11 0 1
0 3 -1 0
0 13 7 3
1
end_operator
begin_operator
lift hoist1 crate1 pallet2 distributor0
0
5
0 9 1 3
0 6 0 1
0 11 0 1
0 4 -1 0
0 13 8 3
1
end_operator
begin_operator
lift hoist2 crate0 crate1 distributor1
0
5
0 8 2 3
0 7 0 1
0 10 0 1
0 11 -1 0
0 12 5 4
1
end_operator
begin_operator
lift hoist2 crate0 pallet0 distributor1
0
5
0 8 2 3
0 7 0 1
0 10 0 1
0 2 -1 0
0 12 6 4
1
end_operator
begin_operator
lift hoist2 crate0 pallet1 distributor1
0
5
0 8 2 3
0 7 0 1
0 10 0 1
0 3 -1 0
0 12 7 4
1
end_operator
begin_operator
lift hoist2 crate0 pallet2 distributor1
0
5
0 8 2 3
0 7 0 1
0 10 0 1
0 4 -1 0
0 12 8 4
1
end_operator
begin_operator
lift hoist2 crate1 crate0 distributor1
0
5
0 9 2 3
0 7 0 1
0 10 -1 0
0 11 0 1
0 13 5 4
1
end_operator
begin_operator
lift hoist2 crate1 pallet0 distributor1
0
5
0 9 2 3
0 7 0 1
0 11 0 1
0 2 -1 0
0 13 6 4
1
end_operator
begin_operator
lift hoist2 crate1 pallet1 distributor1
0
5
0 9 2 3
0 7 0 1
0 11 0 1
0 3 -1 0
0 13 7 4
1
end_operator
begin_operator
lift hoist2 crate1 pallet2 distributor1
0
5
0 9 2 3
0 7 0 1
0 11 0 1
0 4 -1 0
0 13 8 4
1
end_operator
begin_operator
load hoist0 crate0 truck0 depot0
1
1 0
2
0 5 -1 0
0 12 2 0
1
end_operator
begin_operator
load hoist0 crate0 truck1 depot0
1
0 0
2
0 5 -1 0
0 12 2 1
1
end_operator
begin_operator
load hoist0 crate1 truck0 depot0
1
1 0
2
0 5 -1 0
0 13 2 0
1
end_operator
begin_operator
load hoist0 crate1 truck1 depot0
1
0 0
2
0 5 -1 0
0 13 2 1
1
end_operator
begin_operator
load hoist1 crate0 truck0 distributor0
1
1 1
2
0 6 -1 0
0 12 3 0
1
end_operator
begin_operator
load hoist1 crate0 truck1 distributor0
1
0 1
2
0 6 -1 0
0 12 3 1
1
end_operator
begin_operator
load hoist1 crate1 truck0 distributor0
1
1 1
2
0 6 -1 0
0 13 3 0
1
end_operator
begin_operator
load hoist1 crate1 truck1 distributor0
1
0 1
2
0 6 -1 0
0 13 3 1
1
end_operator
begin_operator
load hoist2 crate0 truck0 distributor1
1
1 2
2
0 7 -1 0
0 12 4 0
1
end_operator
begin_operator
load hoist2 crate0 truck1 distributor1
1
0 2
2
0 7 -1 0
0 12 4 1
1
end_operator
begin_operator
load hoist2 crate1 truck0 distributor1
1
1 2
2
0 7 -1 0
0 13 4 0
1
end_operator
begin_operator
load hoist2 crate1 truck1 distributor1
1
0 2
2
0 7 -1 0
0 13 4 1
1
end_operator
begin_operator
unload hoist0 crate0 truck0 depot0
1
1 0
2
0 5 0 1
0 12 0 2
1
end_operator
begin_operator
unload hoist0 crate0 truck1 depot0
1
0 0
2
0 5 0 1
0 12 1 2
1
end_operator
begin_operator
unload hoist0 crate1 truck0 depot0
1
1 0
2
0 5 0 1
0 13 0 2
1
end_operator
begin_operator
unload hoist0 crate1 truck1 depot0
1
0 0
2
0 5 0 1
0 13 1 2
1
end_operator
begin_operator
unload hoist1 crate0 truck0 distributor0
1
1 1
2
0 6 0 1
0 12 0 3
1
end_operator
begin_operator
unload hoist1 crate0 truck1 distributor0
1
0 1
2
0 6 0 1
0 12 1 3
1
end_operator
begin_operator
unload hoist1 crate1 truck0 distributor0
1
1 1
2
0 6 0 1
0 13 0 3
1
end_operator
begin_operator
unload hoist1 crate1 truck1 distributor0
1
0 1
2
0 6 0 1
0 13 1 3
1
end_operator
begin_operator
unload hoist2 crate0 truck0 distributor1
1
1 2
2
0 7 0 1
0 12 0 4
1
end_operator
begin_operator
unload hoist2 crate0 truck1 distributor1
1
0 2
2
0 7 0 1
0 12 1 4
1
end_operator
begin_operator
unload hoist2 crate1 truck0 distributor1
1
1 2
2
0 7 0 1
0 13 0 4
1
end_operator
begin_operator
unload hoist2 crate1 truck1 distributor1
1
0 2
2
0 7 0 1
0 13 1 4
1
end_operator
0
