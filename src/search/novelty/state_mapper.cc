#include "state_mapper.h"

#include "tokenizer.h"

#include <fstream>


namespace novelty {

static void parse_predicates_file(const std::string& filename, dlplan::core::VocabularyInfo& vocabulary_info) {
    std::ifstream infile(filename);
    std::string name;
    int arity;
    while (infile >> name >> arity) {
        vocabulary_info.add_predicate(name, arity);
        vocabulary_info.add_predicate(name + "_g", arity);
    }
}


static void parse_constants_file(const std::string& filename, dlplan::core::VocabularyInfo& vocabulary_info) {
    std::ifstream infile(filename);
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
    { AtomTokenType::COMMA, Tokenizer<AtomTokenType>::build_regex(",") },
    { AtomTokenType::OPENING_PARENTHESIS, Tokenizer<AtomTokenType>::build_regex("\\(") },
    { AtomTokenType::CLOSING_PARENTHESIS, Tokenizer<AtomTokenType>::build_regex("\\)") },
    { AtomTokenType::NAME, Tokenizer<AtomTokenType>::build_regex("[a-zA-Z0-9_@\\-]+") },
};


static int parse_atom(const std::string& atom_name, dlplan::core::InstanceInfo& instance_info, bool is_static, bool is_goal) {
    auto tokens = Tokenizer<AtomTokenType>().tokenize(atom_name, atom_token_regexes);
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
    std::string name;
    while (infile >> name) {
        parse_atom(name, instance_info, true, false);
    }
}


static void parse_goal_atoms_file(const std::string& filename, dlplan::core::InstanceInfo& instance_info) {
    std::ifstream infile(filename);
    std::string name;
    while (infile >> name) {
        parse_atom(name, instance_info, true, true);
    }
}


static std::vector<int> compute_fact_index_to_dlplan_atom_index(
    const TaskProxy& task_proxy,
    dlplan::core::InstanceInfo& instance_info) {
    std::vector<int> fact_index_to_dlplan_atom_index;
    std::string atom_prefix = "Atom ";
    for (const auto& variable : task_proxy.get_variables()) {
        for (size_t i = 0; i < variable.get_domain_size(); ++i) {
            std::string name = variable.get_fact(i).get_name();
            if (name.substr(0, 5) == atom_prefix) {
                std::string normalized_atom = name.substr(atom_prefix.size());
                fact_index_to_dlplan_atom_index.push_back(
                    parse_atom(normalized_atom, instance_info, false, false));
            } else {
                fact_index_to_dlplan_atom_index.push_back(UNDEFINED);
            }
        }
    }
    return fact_index_to_dlplan_atom_index;
}


static const std::vector<std::pair<AtomTokenType, std::regex>> fd_atom_token_regexes = {
    { AtomTokenType::COMMA, Tokenizer<AtomTokenType>::build_regex(",") },
    { AtomTokenType::OPENING_PARENTHESIS, Tokenizer<AtomTokenType>::build_regex("\\(") },
    { AtomTokenType::CLOSING_PARENTHESIS, Tokenizer<AtomTokenType>::build_regex("\\)") },
    { AtomTokenType::NAME, Tokenizer<AtomTokenType>::build_regex("[a-zA-Z0-9_@\\-]+") },
};

StateMapper::StateMapper(const TaskProxy &task_proxy)
    : m_vocabulary_info(std::make_shared<dlplan::core::VocabularyInfo>()) {
    parse_predicates_file("predicates.txt", *m_vocabulary_info);
    parse_constants_file("constants.txt", *m_vocabulary_info);
    m_instance_info = std::make_shared<dlplan::core::InstanceInfo>(m_vocabulary_info);
    parse_static_atoms_file("static-atoms.txt", *m_instance_info);
    parse_goal_atoms_file("goal-atoms.txt", *m_instance_info);
    m_fact_index_to_dlplan_atom_index = compute_fact_index_to_dlplan_atom_index(task_proxy, *m_instance_info);
}

dlplan::core::State StateMapper::compute_dlplan_state(int state_index, const std::vector<int>& fact_indices) const {
    std::vector<int> atom_indices;
    atom_indices.reserve(fact_indices.size());
    for (int fact_index : fact_indices) {
        int dlplan_atom_index = m_fact_index_to_dlplan_atom_index[fact_index];
        if (dlplan_atom_index != UNDEFINED) {
            atom_indices.push_back(dlplan_atom_index);
        }
    }
    atom_indices.shrink_to_fit();
    return dlplan::core::State(m_instance_info, std::move(atom_indices), state_index);
}

}
