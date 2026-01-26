begin_version
3
end_version
begin_metric
1
end_metric
25
begin_variable
var0
-1
2
Atom available(b0)
NegatedAtom available(b0)
end_variable
begin_variable
var1
-1
3
Atom empty(highspeed-saw0)
Atom in-highspeed-saw(b0, highspeed-saw0)
Atom in-highspeed-saw(b1, highspeed-saw0)
end_variable
begin_variable
var2
-1
2
Atom available(b1)
NegatedAtom available(b1)
end_variable
begin_variable
var3
-1
2
Atom boardsize(b1, s5)
NegatedAtom boardsize(b1, s5)
end_variable
begin_variable
var4
-1
2
Atom boardsize(b1, s4)
NegatedAtom boardsize(b1, s4)
end_variable
begin_variable
var5
-1
2
Atom boardsize(b1, s3)
NegatedAtom boardsize(b1, s3)
end_variable
begin_variable
var6
-1
2
Atom boardsize(b1, s2)
NegatedAtom boardsize(b1, s2)
end_variable
begin_variable
var7
-1
2
Atom boardsize(b1, s1)
NegatedAtom boardsize(b1, s1)
end_variable
begin_variable
var8
-1
2
Atom colour(p0, blue)
NegatedAtom colour(p0, blue)
end_variable
begin_variable
var9
-1
2
Atom colour(p2, blue)
NegatedAtom colour(p2, blue)
end_variable
begin_variable
var10
-1
4
Atom surface-condition(p1, rough)
Atom surface-condition(p1, smooth)
Atom surface-condition(p1, verysmooth)
<none of those>
end_variable
begin_variable
var11
-1
2
Atom colour(p1, natural)
NegatedAtom colour(p1, natural)
end_variable
begin_variable
var12
-1
5
Atom treatment(p2, colourfragments)
Atom treatment(p2, glazed)
Atom treatment(p2, untreated)
Atom treatment(p2, varnished)
Atom unused(p2)
end_variable
begin_variable
var13
-1
2
Atom colour(p2, natural)
NegatedAtom colour(p2, natural)
end_variable
begin_variable
var14
-1
2
Atom colour(p1, blue)
NegatedAtom colour(p1, blue)
end_variable
begin_variable
var15
-1
2
Atom available(p2)
NegatedAtom available(p2)
end_variable
begin_variable
var16
-1
2
Atom available(p1)
NegatedAtom available(p1)
end_variable
begin_variable
var17
-1
2
Atom available(p0)
NegatedAtom available(p0)
end_variable
begin_variable
var18
-1
4
Atom surface-condition(p2, rough)
Atom surface-condition(p2, smooth)
Atom surface-condition(p2, verysmooth)
<none of those>
end_variable
begin_variable
var19
-1
5
Atom treatment(p1, colourfragments)
Atom treatment(p1, glazed)
Atom treatment(p1, untreated)
Atom treatment(p1, varnished)
Atom unused(p1)
end_variable
begin_variable
var20
-1
4
Atom surface-condition(p0, rough)
Atom surface-condition(p0, smooth)
Atom surface-condition(p0, verysmooth)
<none of those>
end_variable
begin_variable
var21
-1
5
Atom treatment(p0, colourfragments)
Atom treatment(p0, glazed)
Atom treatment(p0, untreated)
Atom treatment(p0, varnished)
Atom unused(p0)
end_variable
begin_variable
var22
-1
2
Atom colour(p0, natural)
NegatedAtom colour(p0, natural)
end_variable
begin_variable
var23
-1
2
Atom wood(p2, pine)
NegatedAtom wood(p2, pine)
end_variable
begin_variable
var24
-1
3
Atom wood(p0, oak)
Atom wood(p0, pine)
<none of those>
end_variable
10
begin_mutex_group
2
0 0
1 1
end_mutex_group
begin_mutex_group
2
2 0
1 2
end_mutex_group
begin_mutex_group
2
17 0
21 4
end_mutex_group
begin_mutex_group
2
16 0
19 4
end_mutex_group
begin_mutex_group
2
15 0
12 4
end_mutex_group
begin_mutex_group
4
20 0
20 1
20 2
21 4
end_mutex_group
begin_mutex_group
4
10 0
10 1
10 2
19 4
end_mutex_group
begin_mutex_group
4
18 0
18 1
18 2
12 4
end_mutex_group
begin_mutex_group
3
21 4
24 0
24 1
end_mutex_group
begin_mutex_group
2
12 4
23 0
end_mutex_group
begin_state
0
0
0
1
1
1
1
1
1
1
3
1
4
1
1
1
1
1
3
4
3
4
1
1
2
end_state
begin_goal
11
14 0
15 0
16 0
17 0
18 1
19 3
20 1
21 3
22 0
23 0
24 0
end_goal
175
begin_operator
cut-board-large b1 p1 highspeed-saw0 pine rough s3 s1 s2 s0
2
5 0
1 2
4
0 16 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
10
end_operator
begin_operator
cut-board-large b1 p1 highspeed-saw0 pine rough s4 s2 s3 s1
2
4 0
1 2
5
0 16 -1 0
0 7 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
10
end_operator
begin_operator
cut-board-large b1 p1 highspeed-saw0 pine rough s5 s3 s4 s2
2
3 0
1 2
5
0 16 -1 0
0 6 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
10
end_operator
begin_operator
cut-board-large b1 p1 highspeed-saw0 pine rough s6 s4 s5 s3
1
1 2
5
0 16 -1 0
0 5 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
10
end_operator
begin_operator
cut-board-large b1 p2 highspeed-saw0 pine rough s3 s1 s2 s0
2
5 0
1 2
5
0 15 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
10
end_operator
begin_operator
cut-board-large b1 p2 highspeed-saw0 pine rough s4 s2 s3 s1
2
4 0
1 2
6
0 15 -1 0
0 7 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
10
end_operator
begin_operator
cut-board-large b1 p2 highspeed-saw0 pine rough s5 s3 s4 s2
2
3 0
1 2
6
0 15 -1 0
0 6 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
10
end_operator
begin_operator
cut-board-large b1 p2 highspeed-saw0 pine rough s6 s4 s5 s3
1
1 2
6
0 15 -1 0
0 5 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
10
end_operator
begin_operator
cut-board-small b0 p0 highspeed-saw0 oak rough s1 s0
1
1 1
5
0 17 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 0
10
end_operator
begin_operator
cut-board-small b1 p0 highspeed-saw0 pine rough s1 s0
2
7 0
1 2
5
0 17 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
10
end_operator
begin_operator
cut-board-small b1 p0 highspeed-saw0 pine rough s2 s1
2
6 0
1 2
6
0 17 -1 0
0 7 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
10
end_operator
begin_operator
cut-board-small b1 p0 highspeed-saw0 pine rough s3 s2
2
5 0
1 2
6
0 17 -1 0
0 6 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
10
end_operator
begin_operator
cut-board-small b1 p0 highspeed-saw0 pine rough s4 s3
2
4 0
1 2
6
0 17 -1 0
0 5 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
10
end_operator
begin_operator
cut-board-small b1 p0 highspeed-saw0 pine rough s5 s4
2
3 0
1 2
6
0 17 -1 0
0 4 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
10
end_operator
begin_operator
cut-board-small b1 p0 highspeed-saw0 pine rough s6 s5
1
1 2
6
0 17 -1 0
0 3 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
10
end_operator
begin_operator
do-glaze p0 glazer0 blue
1
17 0
3
0 8 -1 0
0 22 -1 1
0 21 2 1
10
end_operator
begin_operator
do-glaze p1 glazer0 blue
1
16 0
3
0 14 -1 0
0 11 -1 1
0 19 2 1
20
end_operator
begin_operator
do-glaze p2 glazer0 blue
1
15 0
3
0 9 -1 0
0 13 -1 1
0 12 2 1
20
end_operator
begin_operator
do-grind p0 grinder0 smooth blue colourfragments untreated
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 1 2
0 21 0 2
15
end_operator
begin_operator
do-grind p0 grinder0 smooth blue glazed untreated
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 1 2
0 21 1 2
15
end_operator
begin_operator
do-grind p0 grinder0 smooth blue untreated untreated
2
17 0
21 2
3
0 8 0 1
0 22 -1 0
0 20 1 2
15
end_operator
begin_operator
do-grind p0 grinder0 smooth blue varnished colourfragments
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 1 2
0 21 3 0
15
end_operator
begin_operator
do-grind p0 grinder0 smooth natural colourfragments untreated
2
17 0
22 0
2
0 20 1 2
0 21 0 2
15
end_operator
begin_operator
do-grind p0 grinder0 smooth natural glazed untreated
2
17 0
22 0
2
0 20 1 2
0 21 1 2
15
end_operator
begin_operator
do-grind p0 grinder0 smooth natural untreated untreated
3
17 0
22 0
21 2
1
0 20 1 2
15
end_operator
begin_operator
do-grind p0 grinder0 smooth natural varnished colourfragments
2
17 0
22 0
2
0 20 1 2
0 21 3 0
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth blue colourfragments untreated
2
17 0
20 2
3
0 8 0 1
0 22 -1 0
0 21 0 2
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth blue glazed untreated
2
17 0
20 2
3
0 8 0 1
0 22 -1 0
0 21 1 2
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth blue untreated untreated
3
17 0
20 2
21 2
2
0 8 0 1
0 22 -1 0
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth blue varnished colourfragments
2
17 0
20 2
3
0 8 0 1
0 22 -1 0
0 21 3 0
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth natural colourfragments untreated
3
17 0
22 0
20 2
1
0 21 0 2
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth natural glazed untreated
3
17 0
22 0
20 2
1
0 21 1 2
15
end_operator
begin_operator
do-grind p0 grinder0 verysmooth natural varnished colourfragments
3
17 0
22 0
20 2
1
0 21 3 0
15
end_operator
begin_operator
do-grind p1 grinder0 smooth blue colourfragments untreated
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 1 2
0 19 0 2
45
end_operator
begin_operator
do-grind p1 grinder0 smooth blue glazed untreated
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 1 2
0 19 1 2
45
end_operator
begin_operator
do-grind p1 grinder0 smooth blue untreated untreated
2
16 0
19 2
3
0 14 0 1
0 11 -1 0
0 10 1 2
45
end_operator
begin_operator
do-grind p1 grinder0 smooth blue varnished colourfragments
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 1 2
0 19 3 0
45
end_operator
begin_operator
do-grind p1 grinder0 smooth natural colourfragments untreated
2
16 0
11 0
2
0 10 1 2
0 19 0 2
45
end_operator
begin_operator
do-grind p1 grinder0 smooth natural glazed untreated
2
16 0
11 0
2
0 10 1 2
0 19 1 2
45
end_operator
begin_operator
do-grind p1 grinder0 smooth natural untreated untreated
3
16 0
11 0
19 2
1
0 10 1 2
45
end_operator
begin_operator
do-grind p1 grinder0 smooth natural varnished colourfragments
2
16 0
11 0
2
0 10 1 2
0 19 3 0
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth blue colourfragments untreated
2
16 0
10 2
3
0 14 0 1
0 11 -1 0
0 19 0 2
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth blue glazed untreated
2
16 0
10 2
3
0 14 0 1
0 11 -1 0
0 19 1 2
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth blue untreated untreated
3
16 0
10 2
19 2
2
0 14 0 1
0 11 -1 0
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth blue varnished colourfragments
2
16 0
10 2
3
0 14 0 1
0 11 -1 0
0 19 3 0
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth natural colourfragments untreated
3
16 0
11 0
10 2
1
0 19 0 2
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth natural glazed untreated
3
16 0
11 0
10 2
1
0 19 1 2
45
end_operator
begin_operator
do-grind p1 grinder0 verysmooth natural varnished colourfragments
3
16 0
11 0
10 2
1
0 19 3 0
45
end_operator
begin_operator
do-grind p2 grinder0 smooth blue colourfragments untreated
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 1 2
0 12 0 2
45
end_operator
begin_operator
do-grind p2 grinder0 smooth blue glazed untreated
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 1 2
0 12 1 2
45
end_operator
begin_operator
do-grind p2 grinder0 smooth blue untreated untreated
2
15 0
12 2
3
0 9 0 1
0 13 -1 0
0 18 1 2
45
end_operator
begin_operator
do-grind p2 grinder0 smooth blue varnished colourfragments
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 1 2
0 12 3 0
45
end_operator
begin_operator
do-grind p2 grinder0 smooth natural colourfragments untreated
2
15 0
13 0
2
0 18 1 2
0 12 0 2
45
end_operator
begin_operator
do-grind p2 grinder0 smooth natural glazed untreated
2
15 0
13 0
2
0 18 1 2
0 12 1 2
45
end_operator
begin_operator
do-grind p2 grinder0 smooth natural untreated untreated
3
15 0
13 0
12 2
1
0 18 1 2
45
end_operator
begin_operator
do-grind p2 grinder0 smooth natural varnished colourfragments
2
15 0
13 0
2
0 18 1 2
0 12 3 0
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth blue colourfragments untreated
2
15 0
18 2
3
0 9 0 1
0 13 -1 0
0 12 0 2
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth blue glazed untreated
2
15 0
18 2
3
0 9 0 1
0 13 -1 0
0 12 1 2
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth blue untreated untreated
3
15 0
18 2
12 2
2
0 9 0 1
0 13 -1 0
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth blue varnished colourfragments
2
15 0
18 2
3
0 9 0 1
0 13 -1 0
0 12 3 0
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth natural colourfragments untreated
3
15 0
13 0
18 2
1
0 12 0 2
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth natural glazed untreated
3
15 0
13 0
18 2
1
0 12 1 2
45
end_operator
begin_operator
do-grind p2 grinder0 verysmooth natural varnished colourfragments
3
15 0
13 0
18 2
1
0 12 3 0
45
end_operator
begin_operator
do-immersion-varnish p0 immersion-varnisher0 blue smooth
2
17 0
20 1
3
0 8 -1 0
0 22 -1 1
0 21 2 3
10
end_operator
begin_operator
do-immersion-varnish p0 immersion-varnisher0 blue verysmooth
2
17 0
20 2
3
0 8 -1 0
0 22 -1 1
0 21 2 3
10
end_operator
begin_operator
do-immersion-varnish p0 immersion-varnisher0 natural smooth
2
17 0
20 1
2
0 22 -1 0
0 21 2 3
10
end_operator
begin_operator
do-immersion-varnish p0 immersion-varnisher0 natural verysmooth
2
17 0
20 2
2
0 22 -1 0
0 21 2 3
10
end_operator
begin_operator
do-immersion-varnish p1 immersion-varnisher0 blue smooth
2
16 0
10 1
3
0 14 -1 0
0 11 -1 1
0 19 2 3
10
end_operator
begin_operator
do-immersion-varnish p1 immersion-varnisher0 blue verysmooth
2
16 0
10 2
3
0 14 -1 0
0 11 -1 1
0 19 2 3
10
end_operator
begin_operator
do-immersion-varnish p1 immersion-varnisher0 natural smooth
2
16 0
10 1
2
0 11 -1 0
0 19 2 3
10
end_operator
begin_operator
do-immersion-varnish p1 immersion-varnisher0 natural verysmooth
2
16 0
10 2
2
0 11 -1 0
0 19 2 3
10
end_operator
begin_operator
do-immersion-varnish p2 immersion-varnisher0 blue smooth
2
15 0
18 1
3
0 9 -1 0
0 13 -1 1
0 12 2 3
10
end_operator
begin_operator
do-immersion-varnish p2 immersion-varnisher0 blue verysmooth
2
15 0
18 2
3
0 9 -1 0
0 13 -1 1
0 12 2 3
10
end_operator
begin_operator
do-immersion-varnish p2 immersion-varnisher0 natural smooth
2
15 0
18 1
2
0 13 -1 0
0 12 2 3
10
end_operator
begin_operator
do-immersion-varnish p2 immersion-varnisher0 natural verysmooth
2
15 0
18 2
2
0 13 -1 0
0 12 2 3
10
end_operator
begin_operator
do-plane p0 planer0 rough blue colourfragments
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 0 1
0 21 0 2
10
end_operator
begin_operator
do-plane p0 planer0 rough blue glazed
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 0 1
0 21 1 2
10
end_operator
begin_operator
do-plane p0 planer0 rough blue untreated
2
17 0
21 2
3
0 8 0 1
0 22 -1 0
0 20 0 1
10
end_operator
begin_operator
do-plane p0 planer0 rough blue varnished
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 0 1
0 21 3 2
10
end_operator
begin_operator
do-plane p0 planer0 rough natural colourfragments
2
17 0
22 0
2
0 20 0 1
0 21 0 2
10
end_operator
begin_operator
do-plane p0 planer0 rough natural glazed
2
17 0
22 0
2
0 20 0 1
0 21 1 2
10
end_operator
begin_operator
do-plane p0 planer0 rough natural untreated
3
17 0
22 0
21 2
1
0 20 0 1
10
end_operator
begin_operator
do-plane p0 planer0 rough natural varnished
2
17 0
22 0
2
0 20 0 1
0 21 3 2
10
end_operator
begin_operator
do-plane p0 planer0 smooth blue colourfragments
2
17 0
20 1
3
0 8 0 1
0 22 -1 0
0 21 0 2
10
end_operator
begin_operator
do-plane p0 planer0 smooth blue glazed
2
17 0
20 1
3
0 8 0 1
0 22 -1 0
0 21 1 2
10
end_operator
begin_operator
do-plane p0 planer0 smooth blue untreated
3
17 0
20 1
21 2
2
0 8 0 1
0 22 -1 0
10
end_operator
begin_operator
do-plane p0 planer0 smooth blue varnished
2
17 0
20 1
3
0 8 0 1
0 22 -1 0
0 21 3 2
10
end_operator
begin_operator
do-plane p0 planer0 smooth natural colourfragments
3
17 0
22 0
20 1
1
0 21 0 2
10
end_operator
begin_operator
do-plane p0 planer0 smooth natural glazed
3
17 0
22 0
20 1
1
0 21 1 2
10
end_operator
begin_operator
do-plane p0 planer0 smooth natural varnished
3
17 0
22 0
20 1
1
0 21 3 2
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth blue colourfragments
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 2 1
0 21 0 2
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth blue glazed
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 2 1
0 21 1 2
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth blue untreated
2
17 0
21 2
3
0 8 0 1
0 22 -1 0
0 20 2 1
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth blue varnished
1
17 0
4
0 8 0 1
0 22 -1 0
0 20 2 1
0 21 3 2
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth natural colourfragments
2
17 0
22 0
2
0 20 2 1
0 21 0 2
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth natural glazed
2
17 0
22 0
2
0 20 2 1
0 21 1 2
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth natural untreated
3
17 0
22 0
21 2
1
0 20 2 1
10
end_operator
begin_operator
do-plane p0 planer0 verysmooth natural varnished
2
17 0
22 0
2
0 20 2 1
0 21 3 2
10
end_operator
begin_operator
do-plane p1 planer0 rough blue colourfragments
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 0 1
0 19 0 2
30
end_operator
begin_operator
do-plane p1 planer0 rough blue glazed
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 0 1
0 19 1 2
30
end_operator
begin_operator
do-plane p1 planer0 rough blue untreated
2
16 0
19 2
3
0 14 0 1
0 11 -1 0
0 10 0 1
30
end_operator
begin_operator
do-plane p1 planer0 rough blue varnished
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 0 1
0 19 3 2
30
end_operator
begin_operator
do-plane p1 planer0 rough natural colourfragments
2
16 0
11 0
2
0 10 0 1
0 19 0 2
30
end_operator
begin_operator
do-plane p1 planer0 rough natural glazed
2
16 0
11 0
2
0 10 0 1
0 19 1 2
30
end_operator
begin_operator
do-plane p1 planer0 rough natural untreated
3
16 0
11 0
19 2
1
0 10 0 1
30
end_operator
begin_operator
do-plane p1 planer0 rough natural varnished
2
16 0
11 0
2
0 10 0 1
0 19 3 2
30
end_operator
begin_operator
do-plane p1 planer0 smooth blue colourfragments
2
16 0
10 1
3
0 14 0 1
0 11 -1 0
0 19 0 2
30
end_operator
begin_operator
do-plane p1 planer0 smooth blue glazed
2
16 0
10 1
3
0 14 0 1
0 11 -1 0
0 19 1 2
30
end_operator
begin_operator
do-plane p1 planer0 smooth blue untreated
3
16 0
10 1
19 2
2
0 14 0 1
0 11 -1 0
30
end_operator
begin_operator
do-plane p1 planer0 smooth blue varnished
2
16 0
10 1
3
0 14 0 1
0 11 -1 0
0 19 3 2
30
end_operator
begin_operator
do-plane p1 planer0 smooth natural colourfragments
3
16 0
11 0
10 1
1
0 19 0 2
30
end_operator
begin_operator
do-plane p1 planer0 smooth natural glazed
3
16 0
11 0
10 1
1
0 19 1 2
30
end_operator
begin_operator
do-plane p1 planer0 smooth natural varnished
3
16 0
11 0
10 1
1
0 19 3 2
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth blue colourfragments
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 2 1
0 19 0 2
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth blue glazed
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 2 1
0 19 1 2
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth blue untreated
2
16 0
19 2
3
0 14 0 1
0 11 -1 0
0 10 2 1
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth blue varnished
1
16 0
4
0 14 0 1
0 11 -1 0
0 10 2 1
0 19 3 2
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth natural colourfragments
2
16 0
11 0
2
0 10 2 1
0 19 0 2
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth natural glazed
2
16 0
11 0
2
0 10 2 1
0 19 1 2
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth natural untreated
3
16 0
11 0
19 2
1
0 10 2 1
30
end_operator
begin_operator
do-plane p1 planer0 verysmooth natural varnished
2
16 0
11 0
2
0 10 2 1
0 19 3 2
30
end_operator
begin_operator
do-plane p2 planer0 rough blue colourfragments
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 0 1
0 12 0 2
30
end_operator
begin_operator
do-plane p2 planer0 rough blue glazed
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 0 1
0 12 1 2
30
end_operator
begin_operator
do-plane p2 planer0 rough blue untreated
2
15 0
12 2
3
0 9 0 1
0 13 -1 0
0 18 0 1
30
end_operator
begin_operator
do-plane p2 planer0 rough blue varnished
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 0 1
0 12 3 2
30
end_operator
begin_operator
do-plane p2 planer0 rough natural colourfragments
2
15 0
13 0
2
0 18 0 1
0 12 0 2
30
end_operator
begin_operator
do-plane p2 planer0 rough natural glazed
2
15 0
13 0
2
0 18 0 1
0 12 1 2
30
end_operator
begin_operator
do-plane p2 planer0 rough natural untreated
3
15 0
13 0
12 2
1
0 18 0 1
30
end_operator
begin_operator
do-plane p2 planer0 rough natural varnished
2
15 0
13 0
2
0 18 0 1
0 12 3 2
30
end_operator
begin_operator
do-plane p2 planer0 smooth blue colourfragments
2
15 0
18 1
3
0 9 0 1
0 13 -1 0
0 12 0 2
30
end_operator
begin_operator
do-plane p2 planer0 smooth blue glazed
2
15 0
18 1
3
0 9 0 1
0 13 -1 0
0 12 1 2
30
end_operator
begin_operator
do-plane p2 planer0 smooth blue untreated
3
15 0
18 1
12 2
2
0 9 0 1
0 13 -1 0
30
end_operator
begin_operator
do-plane p2 planer0 smooth blue varnished
2
15 0
18 1
3
0 9 0 1
0 13 -1 0
0 12 3 2
30
end_operator
begin_operator
do-plane p2 planer0 smooth natural colourfragments
3
15 0
13 0
18 1
1
0 12 0 2
30
end_operator
begin_operator
do-plane p2 planer0 smooth natural glazed
3
15 0
13 0
18 1
1
0 12 1 2
30
end_operator
begin_operator
do-plane p2 planer0 smooth natural varnished
3
15 0
13 0
18 1
1
0 12 3 2
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth blue colourfragments
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 2 1
0 12 0 2
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth blue glazed
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 2 1
0 12 1 2
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth blue untreated
2
15 0
12 2
3
0 9 0 1
0 13 -1 0
0 18 2 1
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth blue varnished
1
15 0
4
0 9 0 1
0 13 -1 0
0 18 2 1
0 12 3 2
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth natural colourfragments
2
15 0
13 0
2
0 18 2 1
0 12 0 2
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth natural glazed
2
15 0
13 0
2
0 18 2 1
0 12 1 2
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth natural untreated
3
15 0
13 0
12 2
1
0 18 2 1
30
end_operator
begin_operator
do-plane p2 planer0 verysmooth natural varnished
2
15 0
13 0
2
0 18 2 1
0 12 3 2
30
end_operator
begin_operator
do-saw-large b1 p1 saw0 pine rough s3 s1 s2 s0
2
2 0
5 0
4
0 16 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
30
end_operator
begin_operator
do-saw-large b1 p1 saw0 pine rough s4 s2 s3 s1
2
2 0
4 0
5
0 16 -1 0
0 7 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
30
end_operator
begin_operator
do-saw-large b1 p1 saw0 pine rough s5 s3 s4 s2
2
2 0
3 0
5
0 16 -1 0
0 6 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
30
end_operator
begin_operator
do-saw-large b1 p1 saw0 pine rough s6 s4 s5 s3
1
2 0
5
0 16 -1 0
0 5 -1 0
0 11 -1 0
0 10 -1 0
0 19 4 2
30
end_operator
begin_operator
do-saw-large b1 p2 saw0 pine rough s3 s1 s2 s0
2
2 0
5 0
5
0 15 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
30
end_operator
begin_operator
do-saw-large b1 p2 saw0 pine rough s4 s2 s3 s1
2
2 0
4 0
6
0 15 -1 0
0 7 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
30
end_operator
begin_operator
do-saw-large b1 p2 saw0 pine rough s5 s3 s4 s2
2
2 0
3 0
6
0 15 -1 0
0 6 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
30
end_operator
begin_operator
do-saw-large b1 p2 saw0 pine rough s6 s4 s5 s3
1
2 0
6
0 15 -1 0
0 5 -1 0
0 13 -1 0
0 18 -1 0
0 12 4 2
0 23 -1 0
30
end_operator
begin_operator
do-saw-small b0 p0 saw0 oak rough s1 s0
1
0 0
5
0 17 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 0
30
end_operator
begin_operator
do-saw-small b1 p0 saw0 pine rough s1 s0
2
2 0
7 0
5
0 17 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
30
end_operator
begin_operator
do-saw-small b1 p0 saw0 pine rough s2 s1
2
2 0
6 0
6
0 17 -1 0
0 7 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
30
end_operator
begin_operator
do-saw-small b1 p0 saw0 pine rough s3 s2
2
2 0
5 0
6
0 17 -1 0
0 6 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
30
end_operator
begin_operator
do-saw-small b1 p0 saw0 pine rough s4 s3
2
2 0
4 0
6
0 17 -1 0
0 5 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
30
end_operator
begin_operator
do-saw-small b1 p0 saw0 pine rough s5 s4
2
2 0
3 0
6
0 17 -1 0
0 4 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
30
end_operator
begin_operator
do-saw-small b1 p0 saw0 pine rough s6 s5
1
2 0
6
0 17 -1 0
0 3 -1 0
0 22 -1 0
0 20 -1 0
0 21 4 2
0 24 -1 1
30
end_operator
begin_operator
do-spray-varnish p0 spray-varnisher0 blue smooth
2
17 0
20 1
3
0 8 -1 0
0 22 -1 1
0 21 2 3
5
end_operator
begin_operator
do-spray-varnish p0 spray-varnisher0 blue verysmooth
2
17 0
20 2
3
0 8 -1 0
0 22 -1 1
0 21 2 3
5
end_operator
begin_operator
do-spray-varnish p0 spray-varnisher0 natural smooth
2
17 0
20 1
2
0 22 -1 0
0 21 2 3
5
end_operator
begin_operator
do-spray-varnish p0 spray-varnisher0 natural verysmooth
2
17 0
20 2
2
0 22 -1 0
0 21 2 3
5
end_operator
begin_operator
do-spray-varnish p1 spray-varnisher0 blue smooth
2
16 0
10 1
3
0 14 -1 0
0 11 -1 1
0 19 2 3
15
end_operator
begin_operator
do-spray-varnish p1 spray-varnisher0 blue verysmooth
2
16 0
10 2
3
0 14 -1 0
0 11 -1 1
0 19 2 3
15
end_operator
begin_operator
do-spray-varnish p1 spray-varnisher0 natural smooth
2
16 0
10 1
2
0 11 -1 0
0 19 2 3
15
end_operator
begin_operator
do-spray-varnish p1 spray-varnisher0 natural verysmooth
2
16 0
10 2
2
0 11 -1 0
0 19 2 3
15
end_operator
begin_operator
do-spray-varnish p2 spray-varnisher0 blue smooth
2
15 0
18 1
3
0 9 -1 0
0 13 -1 1
0 12 2 3
15
end_operator
begin_operator
do-spray-varnish p2 spray-varnisher0 blue verysmooth
2
15 0
18 2
3
0 9 -1 0
0 13 -1 1
0 12 2 3
15
end_operator
begin_operator
do-spray-varnish p2 spray-varnisher0 natural smooth
2
15 0
18 1
2
0 13 -1 0
0 12 2 3
15
end_operator
begin_operator
do-spray-varnish p2 spray-varnisher0 natural verysmooth
2
15 0
18 2
2
0 13 -1 0
0 12 2 3
15
end_operator
begin_operator
load-highspeed-saw b0 highspeed-saw0
0
2
0 0 0 1
0 1 0 1
30
end_operator
begin_operator
load-highspeed-saw b1 highspeed-saw0
0
2
0 2 0 1
0 1 0 2
30
end_operator
begin_operator
unload-highspeed-saw b0 highspeed-saw0
0
2
0 0 -1 0
0 1 1 0
10
end_operator
begin_operator
unload-highspeed-saw b1 highspeed-saw0
0
2
0 2 -1 0
0 1 2 0
10
end_operator
0
