#include "condition.h"

#include "../grounding/symbols.h"

#include <functional>
#include <stdexcept>

using namespace std;
namespace translate::pddl {
namespace {
using utils::hash_combine;

template<class T>
size_t hash_value(const T &v) {
    return hash<T>{}(v);
}
}

unordered_set<string> Condition::free_variables() const {
    unordered_set<string> result;
    for (const auto &p : parts()) {
        auto sub = p->free_variables();
        result.insert(sub.begin(), sub.end());
    }
    return result;
}

bool Condition::has_disjunction() const {
    for (const auto &p : parts())
        if (p->has_disjunction())
            return true;
    return false;
}

bool Condition::has_existential_part() const {
    for (const auto &p : parts())
        if (p->has_existential_part())
            return true;
    return false;
}

bool Condition::has_universal_part() const {
    for (const auto &p : parts())
        if (p->has_universal_part())
            return true;
    return false;
}

// -- Truth / Falsity ---------------------------------------------------------

size_t Truth::hash() const {
    return hash_value<int>(static_cast<int>(Kind::TRUTH));
}

size_t Falsity::hash() const {
    return hash_value<int>(static_cast<int>(Kind::FALSITY));
}

void Truth::dump(ostream &os, int indent) const {
    os << string(indent * 2, ' ') << "Truth\n";
}

void Falsity::dump(ostream &os, int indent) const {
    os << string(indent * 2, ' ') << "Falsity\n";
}

ConditionPtr Truth::negate() const {
    return make_falsity();
}
ConditionPtr Falsity::negate() const {
    return make_truth();
}

// -- Literal -----------------------------------------------------------------

Literal::Literal(string predicate, vector<string> args)
    : predicate(move(predicate)),
      args(move(args)),
      predicate_id(grounding::symbols().intern(this->predicate)),
      cached_hash(detail::literal_hash(this->predicate, this->args)) {
}

string Literal::str() const {
    string s = negated() ? "NegatedAtom " : "Atom ";
    s += predicate;
    s += '(';
    for (size_t i = 0; i < args.size(); ++i) {
        if (i)
            s += ", ";
        s += args[i];
    }
    s += ')';
    return s;
}

void Literal::dump(ostream &os, int indent) const {
    os << string(indent * 2, ' ') << str() << "\n";
}

unordered_set<string> Literal::free_variables() const {
    unordered_set<string> result;
    for (const auto &a : args)
        if (!a.empty() && a[0] == '?')
            result.insert(a);
    return result;
}

bool Atom::equals(const Condition &other) const {
    if (other.kind() != Kind::ATOM)
        return false;
    const auto &o = static_cast<const Atom &>(other);
    return cached_hash == o.cached_hash && predicate == o.predicate &&
           args == o.args;
}

bool NegatedAtom::equals(const Condition &other) const {
    if (other.kind() != Kind::NEGATED_ATOM)
        return false;
    const auto &o = static_cast<const NegatedAtom &>(other);
    return cached_hash == o.cached_hash && predicate == o.predicate &&
           args == o.args;
}

ConditionPtr Atom::negate() const {
    return make_shared<NegatedAtom>(predicate, args);
}

ConditionPtr NegatedAtom::negate() const {
    return make_shared<Atom>(predicate, args);
}

// -- JunctorCondition --------------------------------------------------------

JunctorCondition::JunctorCondition(vector<ConditionPtr> children, Kind k)
    : children(move(children)), cached_hash(0) {
    size_t h = hash_value<int>(static_cast<int>(k));
    for (const auto &c : this->children)
        hash_combine(h, c ? c->hash() : 0);
    cached_hash = h;
}

bool JunctorCondition::equals(const Condition &other) const {
    if (other.kind() != kind())
        return false;
    const auto &o = static_cast<const JunctorCondition &>(other);
    if (cached_hash != o.cached_hash)
        return false;
    if (children.size() != o.children.size())
        return false;
    ConditionPtrEqual eq;
    for (size_t i = 0; i < children.size(); ++i)
        if (!eq(children[i], o.children[i]))
            return false;
    return true;
}

void JunctorCondition::dump(ostream &os, int indent) const {
    const char *label =
        (kind() == Kind::CONJUNCTION) ? "Conjunction" : "Disjunction";
    os << string(indent * 2, ' ') << label << "\n";
    for (const auto &c : children)
        c->dump(os, indent + 1);
}

vector<ConditionPtr> JunctorCondition::negated_children() const {
    vector<ConditionPtr> negated;
    negated.reserve(children.size());
    for (const auto &c : children)
        negated.push_back(c->negate());
    return negated;
}

ConditionPtr Conjunction::negate() const {
    return make_shared<Disjunction>(negated_children());
}

ConditionPtr Disjunction::negate() const {
    return make_shared<Conjunction>(negated_children());
}

// -- QuantifiedCondition -----------------------------------------------------

QuantifiedCondition::QuantifiedCondition(
    vector<TypedObject> parameters, vector<ConditionPtr> body, Kind k)
    : parameters(move(parameters)), body(move(body)), cached_hash(0) {
    size_t h = hash_value<int>(static_cast<int>(k));
    for (const auto &p : this->parameters) {
        hash_combine(h, hash_value<string>(p.name));
        hash_combine(h, hash_value<string>(p.type_name));
    }
    for (const auto &b : this->body)
        hash_combine(h, b ? b->hash() : 0);
    cached_hash = h;
}

bool QuantifiedCondition::equals(const Condition &other) const {
    if (other.kind() != kind())
        return false;
    const auto &o = static_cast<const QuantifiedCondition &>(other);
    if (cached_hash != o.cached_hash)
        return false;
    if (parameters.size() != o.parameters.size())
        return false;
    if (body.size() != o.body.size())
        return false;
    for (size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i] != o.parameters[i])
            return false;
    ConditionPtrEqual eq;
    for (size_t i = 0; i < body.size(); ++i)
        if (!eq(body[i], o.body[i]))
            return false;
    return true;
}

void QuantifiedCondition::dump(ostream &os, int indent) const {
    const char *label = (kind() == Kind::UNIVERSAL) ? "UniversalCondition"
                                                    : "ExistentialCondition";
    os << string(indent * 2, ' ') << label;
    for (size_t i = 0; i < parameters.size(); ++i)
        os << (i == 0 ? " " : ", ") << parameters[i];
    os << "\n";
    for (const auto &b : body)
        b->dump(os, indent + 1);
}

unordered_set<string> QuantifiedCondition::free_variables() const {
    auto result = Condition::free_variables();
    for (const auto &p : parameters)
        result.erase(p.name); // PDDL parameter names include the "?" prefix.
    return result;
}

ConditionPtr UniversalCondition::negate() const {
    vector<ConditionPtr> negated_body;
    negated_body.reserve(body.size());
    for (const auto &b : body)
        negated_body.push_back(b->negate());
    return make_shared<ExistentialCondition>(parameters, move(negated_body));
}

// -- instantiate() -----------------------------------------------------------

bool Condition::instantiate(
    const VarMapping &, const FactMap &, vector<GroundLiteral> &) const {
    throw runtime_error("Cannot instantiate condition: not normalized");
}

bool Falsity::instantiate(
    const VarMapping &, const FactMap &, vector<GroundLiteral> &) const {
    return false;
}

namespace {
// Resolve this literal's args under `m` into the integer ground-fact `key`
// (interned predicate id + object-id args). A parameter argument resolves to
// its bound object id via `m`; a constant argument is interned on the spot.
// `key` is a caller-owned reused buffer so probing does not allocate per
// literal (SmallVector keeps the small arg list inline).
void resolve_key(
    GroundKey &key, int predicate_id, const vector<string> &args,
    const VarMapping &m) {
    key.predicate = predicate_id;
    key.args.clear();
    for (const auto &a : args) {
        auto it = m.find(a);
        key.args.push_back(
            it == m.end() ? grounding::symbols().intern(a) : it->second);
    }
}
}

bool Atom::instantiate(
    const VarMapping &var_mapping, const FactMap &facts,
    vector<GroundLiteral> &result) const {
    static thread_local GroundKey key;
    resolve_key(key, predicate_id, args, var_mapping);
    // Single probe: fluent -> a real precondition on that fact; static-true ->
    // drop the (satisfied) literal; absent -> the literal is false, so the
    // caller drops the action.
    const FactId *id = facts.find(key);
    if (!id)
        return false;
    if (*id != STATIC_FACT)
        result.push_back({*id, false});
    return true;
}

bool NegatedAtom::instantiate(
    const VarMapping &var_mapping, const FactMap &facts,
    vector<GroundLiteral> &result) const {
    static thread_local GroundKey key;
    resolve_key(key, predicate_id, args, var_mapping);
    // Mirror image of Atom: absent (static-false) -> negation holds, drop the
    // literal; static-true -> negation is false, drop the action; fluent ->
    // a real negative precondition.
    const FactId *id = facts.find(key);
    if (!id)
        return true;
    if (*id != STATIC_FACT) {
        result.push_back({*id, true});
        return true;
    }
    return false;
}

bool Conjunction::instantiate(
    const VarMapping &var_mapping, const FactMap &facts,
    vector<GroundLiteral> &result) const {
    for (const auto &p : children) {
        if (p && !p->instantiate(var_mapping, facts, result))
            return false;
    }
    return true;
}

bool ExistentialCondition::instantiate(
    const VarMapping &var_mapping, const FactMap &facts,
    vector<GroundLiteral> &result) const {
    if (!body.empty() && body[0])
        return body[0]->instantiate(var_mapping, facts, result);
    return true;
}

ConditionPtr ExistentialCondition::negate() const {
    vector<ConditionPtr> negated_body;
    negated_body.reserve(body.size());
    for (const auto &b : body)
        negated_body.push_back(b->negate());
    return make_shared<UniversalCondition>(parameters, move(negated_body));
}

// -- simplified() ------------------------------------------------------------

ConditionPtr Truth::simplified() const {
    return make_truth();
}
ConditionPtr Falsity::simplified() const {
    return make_falsity();
}
ConditionPtr Literal::simplified() const {
    if (negated())
        return make_shared<NegatedAtom>(predicate, args);
    return make_shared<Atom>(predicate, args);
}

ConditionPtr JunctorCondition::simplify_junctor(Kind absorbing_kind) const {
    const Kind self = kind();
    const Kind identity_kind =
        absorbing_kind == Kind::FALSITY ? Kind::TRUTH : Kind::FALSITY;
    auto constant = [](Kind k) {
        return k == Kind::FALSITY ? make_falsity() : make_truth();
    };
    vector<ConditionPtr> result;
    result.reserve(children.size());
    for (const auto &child : children) {
        ConditionPtr s = child->simplified();
        Kind sk = s->kind();
        if (sk == self) {
            // Flatten a nested junctor of the same kind.
            const auto &j = static_cast<const JunctorCondition &>(*s);
            for (const auto &p : j.children)
                result.push_back(p);
        } else if (sk == absorbing_kind) {
            return constant(absorbing_kind);
        } else if (sk != identity_kind) {
            result.push_back(move(s));
        }
        // identity children are dropped.
    }
    if (result.empty())
        return constant(identity_kind);
    if (result.size() == 1)
        return result.front();
    if (self == Kind::CONJUNCTION)
        return make_shared<Conjunction>(move(result));
    return make_shared<Disjunction>(move(result));
}

ConditionPtr Conjunction::simplified() const {
    return simplify_junctor(Kind::FALSITY);
}

ConditionPtr Disjunction::simplified() const {
    return simplify_junctor(Kind::TRUTH);
}

ConditionPtr QuantifiedCondition::simplified() const {
    // Python: if the simplified body is a constant condition, return it.
    ConditionPtr simplified_body = body[0]->simplified();
    if (simplified_body->kind() == Kind::TRUTH ||
        simplified_body->kind() == Kind::FALSITY) {
        return simplified_body;
    }
    vector<ConditionPtr> new_body = {simplified_body};
    if (kind() == Kind::UNIVERSAL)
        return make_shared<UniversalCondition>(parameters, move(new_body));
    return make_shared<ExistentialCondition>(parameters, move(new_body));
}

// -- uniquify_variables() ----------------------------------------------------

ConditionPtr Condition::uniquify_variables(
    unordered_map<string, string> &type_map,
    const unordered_map<string, string> &renamings) const {
    // Default: recurse over children and rebuild via change_parts.
    // Literals and quantifiers override this method.
    vector<ConditionPtr> new_parts;
    const auto &kids = parts();
    new_parts.reserve(kids.size());
    for (const auto &p : kids)
        new_parts.push_back(p->uniquify_variables(type_map, renamings));
    return change_parts(move(new_parts));
}

ConditionPtr Literal::uniquify_variables(
    unordered_map<string, string> & /*type_map*/,
    const unordered_map<string, string> &renamings) const {
    return rename_variables(renamings);
}

ConditionPtr Literal::change_parts(vector<ConditionPtr>) const {
    if (negated())
        return make_shared<NegatedAtom>(predicate, args);
    return make_shared<Atom>(predicate, args);
}

ConditionPtr Literal::rename_variables(
    const unordered_map<string, string> &renamings) const {
    vector<string> new_args;
    new_args.reserve(args.size());
    for (const auto &a : args) {
        auto it = renamings.find(a);
        new_args.push_back(it == renamings.end() ? a : it->second);
    }
    if (negated())
        return make_shared<NegatedAtom>(predicate, move(new_args));
    return make_shared<Atom>(predicate, move(new_args));
}

ConditionPtr QuantifiedCondition::uniquify_variables(
    unordered_map<string, string> &type_map,
    const unordered_map<string, string> &renamings) const {
    unordered_map<string, string> local_renamings = renamings;
    vector<TypedObject> new_params;
    new_params.reserve(parameters.size());
    for (const auto &par : parameters)
        new_params.push_back(
            pddl::uniquify_name(par, type_map, local_renamings));
    vector<ConditionPtr> new_body;
    new_body.reserve(body.size());
    for (const auto &b : body)
        new_body.push_back(b->uniquify_variables(type_map, local_renamings));
    if (kind() == Kind::UNIVERSAL)
        return make_shared<UniversalCondition>(
            move(new_params), move(new_body));
    return make_shared<ExistentialCondition>(move(new_params), move(new_body));
}
}
