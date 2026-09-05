begin_version
3
end_version
begin_metric
0
end_metric
9
begin_variable
var0
0
2
Atom new-axiom@0()
NegatedAtom new-axiom@0()
end_variable
begin_variable
var1
-1
3
Atom at(o1, l1)
Atom at(o1, l2)
Atom at(o1, l3)
end_variable
begin_variable
var2
-1
2
Atom at(o2, l2)
Atom at(o2, l3)
end_variable
begin_variable
var3
-1
2
Atom marked(l2)
NegatedAtom marked(l2)
end_variable
begin_variable
var4
-1
2
Atom marked(l3)
NegatedAtom marked(l3)
end_variable
begin_variable
var5
-1
2
Atom done(o2)
NegatedAtom done(o2)
end_variable
begin_variable
var6
-1
2
Atom done(o1)
NegatedAtom done(o1)
end_variable
begin_variable
var7
0
2
Atom new-axiom@1()
NegatedAtom new-axiom@1()
end_variable
begin_variable
var8
1
2
Atom new-axiom@2()
NegatedAtom new-axiom@2()
end_variable
0
begin_state
1
0
0
1
1
1
1
1
1
end_state
begin_goal
1
8 0
end_goal
14
begin_operator
finish o1
1
1 0
1
0 6 -1 0
1
end_operator
begin_operator
finish o1
2
1 1
3 0
1
0 6 -1 0
1
end_operator
begin_operator
finish o1
2
1 2
4 0
1
0 6 -1 0
1
end_operator
begin_operator
finish o1
1
0 1
1
0 6 -1 0
1
end_operator
begin_operator
finish o2
2
2 0
3 0
1
0 5 -1 0
1
end_operator
begin_operator
finish o2
2
2 1
4 0
1
0 5 -1 0
1
end_operator
begin_operator
finish o2
1
0 1
1
0 5 -1 0
1
end_operator
begin_operator
mark l2
0
2
1 1 1 3 1 0
1 2 0 3 1 0
1
end_operator
begin_operator
mark l3
0
2
1 1 2 4 1 0
1 2 1 4 1 0
1
end_operator
begin_operator
move o1 l1 l2
0
1
0 1 0 1
1
end_operator
begin_operator
move o1 l2 l3
1
3 0
1
0 1 1 2
1
end_operator
begin_operator
move o1 l3 l2
0
1
0 1 2 1
1
end_operator
begin_operator
move o2 l2 l3
1
3 0
1
0 2 0 1
1
end_operator
begin_operator
move o2 l3 l2
0
1
0 2 1 0
1
end_operator
6
begin_rule
0
0 1 0
end_rule
begin_rule
3
1 0
6 0
7 1
8 1 0
end_rule
begin_rule
4
1 1
3 0
6 0
7 1
8 1 0
end_rule
begin_rule
3
1 2
4 0
6 0
8 1 0
end_rule
begin_rule
1
5 1
7 1 0
end_rule
begin_rule
1
6 1
7 1 0
end_rule
