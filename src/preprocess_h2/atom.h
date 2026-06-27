#ifndef ATOM_H
#define ATOM_H

#include <compare>

struct Atom {
    int var;
    int value;

    Atom(int var, int value) : var(var), value(value) {
    }

    auto operator<=>(const Atom &) const = default;

    /*
      This special object represents "no such atom". E.g., functions
      that search for an atom can return "no_atom" when no matching atom is
      found.
    */
    static const Atom no_atom;
};

inline const Atom Atom::no_atom = Atom(-1, -1);

#endif
