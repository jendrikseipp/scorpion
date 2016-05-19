# -*- coding: utf-8 -*-

OPTIMAL = True

CONFIGS = [
    (175, ["--search",
           "astar(merge_and_shrink(merge_strategy=merge_linear(variable_order=reverse_level),shrink_strategy=shrink_bisimulation(greedy=true),label_reduction=exact(before_shrinking=true,before_merging=false),max_states=infinity,threshold_before_merge=1))"]),
    (432, ["--search",
           "astar(merge_and_shrink(merge_strategy=merge_linear(variable_order=reverse_level),shrink_strategy=shrink_bisimulation(greedy=false),label_reduction=exact(before_shrinking=true,before_merging=false),max_states=200000))"]),
    (455, ["--search",
           "astar(lmcount(lm_merged([lm_rhw(),lm_hm(m=1)]),admissible=true),mpd=true)"]),
    (569, ["--search",
           "astar(lmcut())"]),
     ]
