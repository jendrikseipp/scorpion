#include "condition.h"

#include <functional>

namespace translate::pddl {
namespace {
// Combine hashes (boost::hash_combine style).
inline void hash_combine(std::size_t &seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

template<class T>
std::size_t hash_value(const T &v) {
    return std::hash<T>{}(v);
}
}

std::unordered_set<std::string> Condition::free_variables() const {
    std::unordered_set<std::string> result;
    for (const auto &p : parts()) {
        auto sub = p->free_variables();
        result.insert(sub.begin(), sub.end());
    }
    return result;
}

bool Condition::has_disjunction() const {
    for (const auto &p : parts())
        if (p->has_disjunction()) return true;
    return false;
}

bool Condition::has_existential_part() const {
    for (const auto &p : parts())
        if (p->has_existential_part()) return true;
    return false;
}

bool Condition::has_universal_part() const {
    for (const auto &p : parts())
        if (p->has_universal_part()) return true;
    return false;
}

// -- Truth / Falsity ---------------------------------------------------------

std::size_t Truth::hash() const {
    return hash_value<int>(static_cast<int>(Kind::TRUTH));
}

std::size_t Falsity::hash() const {
    return hash_value<int>(static_cast<int>(Kind::FALSITY));
}

void Truth::dump(std::ostream &os, int indent) const {
    os << std::string(indent * 2, ' ') << "Truth\n";
}

void Falsity::dump(std::ostream &os, int indent) const {
    os << std::string(indent * 2, ' ') << "Falsity\n";
}

ConditionPtr Truth::negate() const { return std::make_shared<Falsity>(); }
ConditionPtr Falsity::negate() const { return std::make_shared<Truth>(); }

// -- Literal -----------------------------------------------------------------

Literal::Literal(std::string predicate, std::vector<std::string> args)
    : predicate(std::move(predicate)), args(std::move(args)), cached_hash(0) {
    std::size_t h = hash_value<std::string>(this->predicate);
    for (const auto &a : this->args)
        hash_combine(h, hash_value<std::string>(a));
    cached_hash = h;
}

void Literal::dump(std::ostream &os, int indent) const {
    os << std::string(indent * 2, ' ');
    os << (negated() ? "NegatedAtom " : "Atom ") << predicate << "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) os << ", ";
        os << args[i];
    }
    os << ")\n";
}

std::unordered_set<std::string> Literal::free_variables() const {
    std::unordered_set<std::string> result;
    for (const auto &a : args)
        if (!a.empty() && a[0] == '?')
            result.insert(a);
    return result;
}

bool Atom::equals(const Condition &other) const {
    if (other.kind() != Kind::ATOM) return false;
    const auto &o = static_cast<const Atom &>(other);
    return cached_hash == o.cached_hash &&
           predicate == o.predicate && args == o.args;
}

bool NegatedAtom::equals(const Condition &other) const {
    if (other.kind() != Kind::NEGATED_ATOM) return false;
    const auto &o = static_cast<const NegatedAtom &>(other);
    return cached_hash == o.cached_hash &&
           predicate == o.predicate && args == o.args;
}

ConditionPtr Atom::negate() const {
    return std::make_shared<NegatedAtom>(predicate, args);
}

ConditionPtr NegatedAtom::negate() const {
    return std::make_shared<Atom>(predicate, args);
}

// -- JunctorCondition --------------------------------------------------------

JunctorCondition::JunctorCondition(std::vector<ConditionPtr> children, Kind k)
    : children(std::move(children)), cached_hash(0) {
    std::size_t h = hash_value<int>(static_cast<int>(k));
    for (const auto &c : this->children)
        hash_combine(h, c ? c->hash() : 0);
    cached_hash = h;
}

bool JunctorCondition::equals(const Condition &other) const {
    if (other.kind() != kind()) return false;
    const auto &o = static_cast<const JunctorCondition &>(other);
    if (cached_hash != o.cached_hash) return false;
    if (children.size() != o.children.size()) return false;
    ConditionPtrEqual eq;
    for (std::size_t i = 0; i < children.size(); ++i)
        if (!eq(children[i], o.children[i])) return false;
    return true;
}

void JunctorCondition::dump(std::ostream &os, int indent) const {
    const char *label = (kind() == Kind::CONJUNCTION) ? "Conjunction"
                                                      : "Disjunction";
    os << std::string(indent * 2, ' ') << label << "\n";
    for (const auto &c : children)
        c->dump(os, indent + 1);
}

ConditionPtr Conjunction::negate() const {
    std::vector<ConditionPtr> negated;
    negated.reserve(children.size());
    for (const auto &c : children)
        negated.push_back(c->negate());
    return std::make_shared<Disjunction>(std::move(negated));
}

ConditionPtr Disjunction::negate() const {
    std::vector<ConditionPtr> negated;
    negated.reserve(children.size());
    for (const auto &c : children)
        negated.push_back(c->negate());
    return std::make_shared<Conjunction>(std::move(negated));
}

// -- QuantifiedCondition -----------------------------------------------------

QuantifiedCondition::QuantifiedCondition(std::vector<TypedObject> parameters,
                                         std::vector<ConditionPtr> body,
                                         Kind k)
    : parameters(std::move(parameters)), body(std::move(body)),
      cached_hash(0) {
    std::size_t h = hash_value<int>(static_cast<int>(k));
    for (const auto &p : this->parameters) {
        hash_combine(h, hash_value<std::string>(p.name));
        hash_combine(h, hash_value<std::string>(p.type_name));
    }
    for (const auto &b : this->body)
        hash_combine(h, b ? b->hash() : 0);
    cached_hash = h;
}

bool QuantifiedCondition::equals(const Condition &other) const {
    if (other.kind() != kind()) return false;
    const auto &o = static_cast<const QuantifiedCondition &>(other);
    if (cached_hash != o.cached_hash) return false;
    if (parameters.size() != o.parameters.size()) return false;
    if (body.size() != o.body.size()) return false;
    for (std::size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i] != o.parameters[i]) return false;
    ConditionPtrEqual eq;
    for (std::size_t i = 0; i < body.size(); ++i)
        if (!eq(body[i], o.body[i])) return false;
    return true;
}

void QuantifiedCondition::dump(std::ostream &os, int indent) const {
    const char *label =
        (kind() == Kind::UNIVERSAL) ? "UniversalCondition"
                                    : "ExistentialCondition";
    os << std::string(indent * 2, ' ') << label;
    for (std::size_t i = 0; i < parameters.size(); ++i)
        os << (i == 0 ? " " : ", ") << parameters[i];
    os << "\n";
    for (const auto &b : body)
        b->dump(os, indent + 1);
}

std::unordered_set<std::string> QuantifiedCondition::free_variables() const {
    auto result = Condition::free_variables();
    for (const auto &p : parameters)
        result.erase(p.name); // PDDL parameter names include the "?" prefix.
    return result;
}

ConditionPtr UniversalCondition::negate() const {
    std::vector<ConditionPtr> negated_body;
    negated_body.reserve(body.size());
    for (const auto &b : body)
        negated_body.push_back(b->negate());
    return std::make_shared<ExistentialCondition>(parameters,
                                                  std::move(negated_body));
}

ConditionPtr ExistentialCondition::negate() const {
    std::vector<ConditionPtr> negated_body;
    negated_body.reserve(body.size());
    for (const auto &b : body)
        negated_body.push_back(b->negate());
    return std::make_shared<UniversalCondition>(parameters,
                                                std::move(negated_body));
}
}
