#include "propositional_task.h"

#include "../utils/tokenizer.h"
#include "../novelty/novelty_table.h"

#include <fstream>


namespace extra_tasks {

static void parse_predicates_file(const std::string& filename, dlplan::core::VocabularyInfo& vocabulary_info) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        throw std::runtime_error("parse_predicates_file - predicates.txt does not exist.");
    }
    std::string name;
    int arity;
    while (infile >> name >> arity) {
        vocabulary_info.add_predicate(name, arity);
        vocabulary_info.add_predicate(name + "_g", arity);
    }
}


static void parse_constants_file(const std::string& filename, dlplan::core::VocabularyInfo& vocabulary_info) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        throw std::runtime_error("parse_constants_file - constants.txt does not exist.");
    }
    std::string name;
    while (infile >> name) {
        vocabulary_info.add_constant(name);
    }
}


enum class AtomTokenType {
    COMMA,
    OPENING_PARENTHESIS,
    CLOSING_PARENTHESIS,
    NAME
};


static const std::vector<std::pair<AtomTokenType, std::regex>> atom_token_regexes = {
    { AtomTokenType::COMMA, utils::Tokenizer<AtomTokenType>::build_regex(",") },
    { AtomTokenType::OPENING_PARENTHESIS, utils::Tokenizer<AtomTokenType>::build_regex("\\(") },
    { AtomTokenType::CLOSING_PARENTHESIS, utils::Tokenizer<AtomTokenType>::build_regex("\\)") },
    { AtomTokenType::NAME, utils::Tokenizer<AtomTokenType>::build_regex("[a-zA-Z0-9_@\\-]+") },
};


static int parse_atom(const std::string& atom_name, dlplan::core::InstanceInfo& instance_info, bool is_static, bool is_goal) {
    auto tokens = utils::Tokenizer<AtomTokenType>().tokenize(atom_name, atom_token_regexes);
    if (tokens.size() < 3) throw std::runtime_error("parse_atom - insufficient number of tokens: " + std::to_string(tokens.size()));
    if (tokens[0].first != AtomTokenType::NAME) throw std::runtime_error("parse_atom_line - expected predicate name at position 0.");
    if (tokens[1].first != AtomTokenType::OPENING_PARENTHESIS) throw std::runtime_error("parse_atom_line - expected opening parenthesis at position 1.");
    std::string predicate_name = tokens[0].second;
    if (is_goal) {
        predicate_name += "_g";
    }
    if (predicate_name == "dummy") {
        return UNDEFINED;
    } else if (predicate_name.substr(0, 10) == "new-axiom@") {
        return UNDEFINED;
    }
    std::vector<std::string> object_names;
    int i = 2; // position of first object_name
    while (i < static_cast<int>(tokens.size())) {
        if (tokens[i].first == AtomTokenType::CLOSING_PARENTHESIS) {
            break;
        } else if (tokens[i].first == AtomTokenType::COMMA) {
            ++i;
        } else if (tokens[i].first == AtomTokenType::NAME) {
            object_names.push_back(tokens[i].second);
            ++i;
        } else {
            throw std::runtime_error("parse_atom_line - expected comma or name: " + tokens[i].second);
        }
    }
    if (tokens.back().first != AtomTokenType::CLOSING_PARENTHESIS) throw std::runtime_error("parse_atom_line - expected closing parenthesis.");
    const auto& atom = (is_static)
        ? instance_info.add_static_atom(predicate_name, object_names)
        : instance_info.add_atom(predicate_name, object_names);
    return atom.get_index();
}


static void parse_static_atoms_file(const std::string& filename, dlplan::core::InstanceInfo& instance_info) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        throw std::runtime_error("parse_static_atoms_file - static_atoms.txt does not exist.");
    }
    std::string name;
    while (infile >> name) {
        parse_atom(name, instance_info, true, false);
    }
}


static void parse_goal_atoms_file(const std::string& filename, dlplan::core::InstanceInfo& instance_info) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        throw std::runtime_error("parse_goal_atoms_file - goal_atoms.txt does not exist.");
    }
    std::string name;
    while (infile >> name) {
        parse_atom(name, instance_info, true, true);
    }
}


static const std::vector<std::pair<AtomTokenType, std::regex>> fd_atom_token_regexes = {
    { AtomTokenType::COMMA, utils::Tokenizer<AtomTokenType>::build_regex(",") },
    { AtomTokenType::OPENING_PARENTHESIS, utils::Tokenizer<AtomTokenType>::build_regex("\\(") },
    { AtomTokenType::CLOSING_PARENTHESIS, utils::Tokenizer<AtomTokenType>::build_regex("\\)") },
    { AtomTokenType::NAME, utils::Tokenizer<AtomTokenType>::build_regex("[a-zA-Z0-9_@\\-]+") },
};


PropositionalTask::PropositionalTask(
    const std::shared_ptr<AbstractTask> &parent)
    : DelegatingTask(parent),
      m_vocabulary_info(std::make_shared<dlplan::core::VocabularyInfo>()),
      m_syntactic_element_factory(std::make_shared<dlplan::core::VocabularyInfo>()),
      m_fact_indexer(std::make_shared<novelty::FactIndexer>(TaskProxy(*parent))) {
    m_syntactic_element_factory = dlplan::core::SyntacticElementFactory(m_vocabulary_info);
    parse_predicates_file("predicates.txt", *m_vocabulary_info);
    parse_constants_file("constants.txt", *m_vocabulary_info);
    m_instance_info = std::make_shared<dlplan::core::InstanceInfo>(m_vocabulary_info);
    parse_static_atoms_file("static-atoms.txt", *m_instance_info);
    parse_goal_atoms_file("goal-atoms.txt", *m_instance_info);
    std::string atom_prefix = "Atom ";
    fact_offsets.reserve(TaskProxy(*parent).get_variables().size());
    num_facts = 0;
    for (const auto& variable : TaskProxy(*parent).get_variables()) {
        fact_offsets.push_back(num_facts);
        int domain_size = variable.get_domain_size();
        num_facts += domain_size;
        for (int i = 0; i < domain_size; ++i) {
            std::string name = variable.get_fact(i).get_name();
            if (name.substr(0, 5) == atom_prefix) {
                std::string normalized_atom = name.substr(atom_prefix.size());
                fact_index_to_dlplan_atom_index.push_back(
                    parse_atom(normalized_atom, *m_instance_info, false, false));
            } else {
                fact_index_to_dlplan_atom_index.push_back(UNDEFINED);
            }
        }
    }
    for (size_t index = 0; index < parent->get_num_goals(); ++index) {
        m_goal_facts.insert(get_fact_id(parent->get_goal_fact(index)));
    }
}

dlplan::core::State PropositionalTask::compute_dlplan_state(const State& state) const {
    std::vector<int> atom_indices;
    atom_indices.reserve(get_num_facts());
    for (int fact_index : get_fact_ids(state)) {
        int dlplan_atom_index = fact_index_to_dlplan_atom_index[fact_index];
        if (dlplan_atom_index != UNDEFINED) {
            atom_indices.push_back(dlplan_atom_index);
        }
    }
    atom_indices.shrink_to_fit();
    return dlplan::core::State(m_instance_info, atom_indices, state.get_id().value);
}

std::vector<int> PropositionalTask::get_initial_state_values() const {
    return m_initial_state_values;
}

std::vector<int> PropositionalTask::get_fact_ids(const State& state) const {
    std::vector<int> fact_ids;
    int num_vars = state.size();
    fact_ids.reserve(num_vars);
    for (FactProxy fact_proxy : state) {
        FactPair fact = fact_proxy.get_pair();
        int fact_id = get_fact_id(fact);
        fact_ids.push_back(fact_id);
    }
    return fact_ids;
}

std::vector<int> PropositionalTask::get_fact_ids(const OperatorProxy &op, const State& state) const {
    std::vector<int> fact_ids;
    int num_vars = state.size();
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        for (int var2 = 0; var2 < num_vars; ++var2) {
            if (fact.var == var2) {
                continue;
            }
            int fact_id = get_fact_id(fact);
            fact_ids.push_back(fact_id);
        }
    }
    return fact_ids;
}

int PropositionalTask::get_fact_id(FactPair fact) const {
    return fact_offsets[fact.var] + fact.value;
}

int PropositionalTask::get_num_facts() const {
    return num_facts;
}

const std::unordered_set<int>& PropositionalTask::get_goal_facts() const {
    return m_goal_facts;
}

dlplan::core::SyntacticElementFactory& PropositionalTask::get_syntactic_element_factory_ref() {
    return m_syntactic_element_factory;
}

dlplan::core::DenotationsCaches& PropositionalTask::get_denotations_caches() {
    return m_denotations_caches;
}

std::shared_ptr<novelty::FactIndexer> PropositionalTask::get_fact_indexer() {
    return m_fact_indexer;
}

}