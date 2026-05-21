#include "condition.h"

#include <functional>
#include <stdexcept>

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

// -- instantiate() -----------------------------------------------------------

void Condition::instantiate(
    const std::unordered_map<std::string, std::string> &,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &,
    std::vector<ConditionPtr> &) const {
    throw std::runtime_error("Cannot instantiate condition: not normalized");
}

void Falsity::instantiate(
    const std::unordered_map<std::string, std::string> &,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &,
    std::vector<ConditionPtr> &) const {
    throw Impossible();
}

namespace {
std::vector<std::string> resolve_args(
    const std::vector<std::string> &args,
    const std::unordered_map<std::string, std::string> &m) {
    std::vector<std::string> out;
    out.reserve(args.size());
    for (const auto &a : args) {
        auto it = m.find(a);
        out.push_back(it == m.end() ? a : it->second);
    }
    return out;
}
}

void Atom::instantiate(
    const std::unordered_map<std::string, std::string> &var_mapping,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &init_facts,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &fluent_facts,
    std::vector<ConditionPtr> &result) const {
    auto args_resolved = resolve_args(args, var_mapping);
    auto ground = std::make_shared<Atom>(predicate, args_resolved);
    if (fluent_facts.find(ground) != fluent_facts.end()) {
        result.push_back(ground);
    } else if (init_facts.find(ground) == init_facts.end()) {
        throw Impossible();
    }
}

void NegatedAtom::instantiate(
    const std::unordered_map<std::string, std::string> &var_mapping,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &init_facts,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &fluent_facts,
    std::vector<ConditionPtr> &result) const {
    auto args_resolved = resolve_args(args, var_mapping);
    auto ground = std::make_shared<Atom>(predicate, args_resolved);
    if (fluent_facts.find(ground) != fluent_facts.end()) {
        result.push_back(
            std::make_shared<NegatedAtom>(predicate, args_resolved));
    } else if (init_facts.find(ground) != init_facts.end()) {
        throw Impossible();
    }
}

void Conjunction::instantiate(
    const std::unordered_map<std::string, std::string> &var_mapping,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &init_facts,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &fluent_facts,
    std::vector<ConditionPtr> &result) const {
    for (const auto &p : children) {
        if (p) p->instantiate(var_mapping, init_facts, fluent_facts, result);
    }
}

void ExistentialCondition::instantiate(
    const std::unordered_map<std::string, std::string> &var_mapping,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &init_facts,
    const std::unordered_set<ConditionPtr, ConditionPtrHash,
                             ConditionPtrEqual> &fluent_facts,
    std::vector<ConditionPtr> &result) const {
    if (!body.empty() && body[0])
        body[0]->instantiate(var_mapping, init_facts, fluent_facts, result);
}

ConditionPtr ExistentialCondition::negate() const {
    std::vector<ConditionPtr> negated_body;
    negated_body.reserve(body.size());
    for (const auto &b : body)
        negated_body.push_back(b->negate());
    return std::make_shared<UniversalCondition>(parameters,
                                                std::move(negated_body));
}

// -- simplified() ------------------------------------------------------------

ConditionPtr Truth::simplified() const { return std::make_shared<Truth>(); }
ConditionPtr Falsity::simplified() const { return std::make_shared<Falsity>(); }
ConditionPtr Literal::simplified() const {
    if (negated())
        return std::make_shared<NegatedAtom>(predicate, args);
    return std::make_shared<Atom>(predicate, args);
}

ConditionPtr Conjunction::simplified() const {
    std::vector<ConditionPtr> result;
    result.reserve(children.size());
    for (const auto &child : children) {
        ConditionPtr s = child->simplified();
        switch (s->kind()) {
            case Kind::CONJUNCTION: {
                const auto &c = static_cast<const Conjunction &>(*s);
                for (const auto &p : c.children)
                    result.push_back(p);
                break;
            }
            case Kind::FALSITY:
                return std::make_shared<Falsity>();
            case Kind::TRUTH:
                break;
            default:
                result.push_back(std::move(s));
        }
    }
    if (result.empty()) return std::make_shared<Truth>();
    if (result.size() == 1) return result.front();
    return std::make_shared<Conjunction>(std::move(result));
}

ConditionPtr Disjunction::simplified() const {
    std::vector<ConditionPtr> result;
    result.reserve(children.size());
    for (const auto &child : children) {
        ConditionPtr s = child->simplified();
        switch (s->kind()) {
            case Kind::DISJUNCTION: {
                const auto &d = static_cast<const Disjunction &>(*s);
                for (const auto &p : d.children)
                    result.push_back(p);
                break;
            }
            case Kind::TRUTH:
                return std::make_shared<Truth>();
            case Kind::FALSITY:
                break;
            default:
                result.push_back(std::move(s));
        }
    }
    if (result.empty()) return std::make_shared<Falsity>();
    if (result.size() == 1) return result.front();
    return std::make_shared<Disjunction>(std::move(result));
}

ConditionPtr QuantifiedCondition::simplified() const {
    // Python: if the simplified body is a constant condition, return it.
    ConditionPtr simplified_body = body[0]->simplified();
    if (simplified_body->kind() == Kind::TRUTH ||
        simplified_body->kind() == Kind::FALSITY) {
        return simplified_body;
    }
    std::vector<ConditionPtr> new_body = {simplified_body};
    if (kind() == Kind::UNIVERSAL)
        return std::make_shared<UniversalCondition>(parameters,
                                                    std::move(new_body));
    return std::make_shared<ExistentialCondition>(parameters,
                                                  std::move(new_body));
}

// -- uniquify_variables() ----------------------------------------------------

ConditionPtr Condition::uniquify_variables(
    std::unordered_map<std::string, std::string> &type_map,
    const std::unordered_map<std::string, std::string> &renamings) const {
    // Default: recurse over children and rebuild via change_parts.
    // Literals and quantifiers override this method.
    std::vector<ConditionPtr> new_parts;
    const auto &kids = parts();
    new_parts.reserve(kids.size());
    for (const auto &p : kids)
        new_parts.push_back(p->uniquify_variables(type_map, renamings));
    return change_parts(std::move(new_parts));
}

ConditionPtr Literal::uniquify_variables(
    std::unordered_map<std::string, std::string> &/*type_map*/,
    const std::unordered_map<std::string, std::string> &renamings) const {
    return rename_variables(renamings);
}

ConditionPtr Literal::change_parts(std::vector<ConditionPtr>) const {
    if (negated())
        return std::make_shared<NegatedAtom>(predicate, args);
    return std::make_shared<Atom>(predicate, args);
}

ConditionPtr Literal::rename_variables(
    const std::unordered_map<std::string, std::string> &renamings) const {
    std::vector<std::string> new_args;
    new_args.reserve(args.size());
    for (const auto &a : args) {
        auto it = renamings.find(a);
        new_args.push_back(it == renamings.end() ? a : it->second);
    }
    if (negated())
        return std::make_shared<NegatedAtom>(predicate, std::move(new_args));
    return std::make_shared<Atom>(predicate, std::move(new_args));
}

ConditionPtr QuantifiedCondition::uniquify_variables(
    std::unordered_map<std::string, std::string> &type_map,
    const std::unordered_map<std::string, std::string> &renamings) const {
    std::unordered_map<std::string, std::string> local_renamings = renamings;
    std::vector<TypedObject> new_params;
    new_params.reserve(parameters.size());
    for (const auto &par : parameters)
        new_params.push_back(pddl::uniquify_name(par, type_map, local_renamings));
    std::vector<ConditionPtr> new_body;
    new_body.reserve(body.size());
    for (const auto &b : body)
        new_body.push_back(b->uniquify_variables(type_map, local_renamings));
    if (kind() == Kind::UNIVERSAL)
        return std::make_shared<UniversalCondition>(std::move(new_params),
                                                    std::move(new_body));
    return std::make_shared<ExistentialCondition>(std::move(new_params),
                                                  std::move(new_body));
}
}
