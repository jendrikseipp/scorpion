#ifndef ATOM_H
#define ATOM_H

#include <functional>

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

// Hash function for Atom to enable use in unordered containers.
namespace std {
template<>
struct hash<Atom> {
    size_t operator()(const Atom &atom) const noexcept {
        return hash<int>{}(atom.var) * 31 + hash<int>{}(atom.value);
    }
};
}

#endif
