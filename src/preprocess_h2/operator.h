#ifndef OPERATOR_H
#define OPERATOR_H

#include "atom.h"
#include "variable.h"

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class H2Mutexes;

class Operator {
public:
    class Prevail {
    public:
        Variable *var;
        int prev;
        Prevail(Variable *v, int p) : var(v), prev(p) {
        }
        void remove_unreachable_atoms() {
            prev = var->get_new_id(prev);
        }
    };
    class EffCond {
    public:
        Variable *var;
        int cond;
        EffCond(Variable *v, int c) : var(v), cond(c) {
        }
        // return true if the condition is reachable
        bool remove_unreachable_atoms() {
            if (var->is_reachable(cond)) {
                cond = var->get_new_id(cond);
                return true;
            } else {
                return false;
            }
        }
    };
    class PrePost {
    public:
        Variable *var;
        int pre, post;
        bool is_conditional_effect;
        std::vector<EffCond> effect_conds;
        PrePost(Variable *v, int pr, int po)
            : var(v), pre(pr), post(po), is_conditional_effect(false) {
        }
        PrePost(Variable *v, std::vector<EffCond> ecs, int pr, int po)
            : var(v),
              pre(pr),
              post(po),
              is_conditional_effect(true),
              effect_conds(std::move(ecs)) {
        }
        bool is_conditional() const {
            return is_conditional_effect;
        }

        void remove_unreachable_atoms() {
            if (pre != -1)
                pre = var->get_new_id(pre);
            post = var->get_new_id(post);
            if (is_conditional_effect) {
                std::vector<EffCond> new_conds;
                new_conds.reserve(effect_conds.size());
                for (EffCond &effect_condition : effect_conds) {
                    if (effect_condition.remove_unreachable_atoms()) {
                        new_conds.push_back(effect_condition);
                    }
                }
                effect_conds = std::move(new_conds);
                if (effect_conds.empty()) {
                    is_conditional_effect = false;
                }
            }
        }
    };

private:
    std::string name;
    std::vector<Prevail> prevail; // var, val
    std::vector<PrePost> pre_post; // var, old-val, new-val
    int cost;
    bool spurious;

    std::vector<Atom> augmented_preconditions;
    std::vector<Atom> potential_preconditions;

    std::vector<std::pair<Variable *, int>> augmented_preconditions_var;
    std::vector<std::pair<Variable *, int>> potential_preconditions_var;
public:
    Operator(std::istream &in, const std::vector<Variable *> &variables);

    void strip_unimportant_effects();
    bool is_redundant() const;

    void dump() const;
    int get_encoding_size() const;
    void generate_cpp_input(std::ofstream &outfile) const;
    int get_cost() const {
        return cost;
    }
    const std::string &get_name() const {
        return name;
    }
    bool has_conditional_effects() const {
        for (const PrePost &effect : pre_post) {
            if (effect.is_conditional())
                return true;
        }
        return false;
    }
    void set_spurious() {
        spurious = true;
    }
    const std::vector<Prevail> &get_prevail() const {
        return prevail;
    }
    const std::vector<PrePost> &get_pre_post() const {
        return pre_post;
    }
    const std::vector<Atom> &get_augmented_preconditions() const {
        return augmented_preconditions;
    }

    const std::vector<Atom> &get_potential_preconditions() const {
        return potential_preconditions;
    }

    int count_potential_preconditions() const;
    int count_augmented_preconditions() const {
        return static_cast<int>(augmented_preconditions.size());
    }

    int count_potential_noeff_preconditions() const;
    void include_augmented_preconditions();

    void remove_ambiguity(const H2Mutexes &h2);

    void remove_unreachable_atoms(const std::vector<Variable *> &variables);

    std::vector<int> get_signature() const;
};

extern void strip_operators(std::vector<Operator> &operators);
extern void remove_duplicate_operators(std::vector<Operator> &operators);

#endif
