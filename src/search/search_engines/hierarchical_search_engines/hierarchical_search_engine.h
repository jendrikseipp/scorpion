#ifndef SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H
#define SEARCH_ENGINES_HIERARCHICAL_SEARCH_ENGINE_H

#include "goal_test.h"

#include "../utils/tokenizer.h"
#include "../../tasks/modified_initial_state_task.h"
#include "../../search_engine.h"

#include <deque>
#include <memory>
#include <vector>

namespace options {
class Options;
}

namespace hierarchical_search_engine {
void add_goal_test_option_to_parser(options::OptionParser &parser);

// Strategies for selecting a split in case there are multiple possibilities.
enum class GoalTestEnum {
    TOP_GOAL,
    SKETCH_SUBGOAL,
    INCREMENT_GOAL_COUNT,
};


struct FilenameTree {
    std::string filename;
    std::vector<std::unique_ptr<FilenameTree>> children;

    FilenameTree(
        std::string&& filename,
        std::vector<std::unique_ptr<FilenameTree>>&& children);
};

enum class FilenameTreeTokenType {
    COMMA,
    OPENING_PARENTHESIS,
    CLOSING_PARENTHESIS,
    NAME
};

static const std::vector<std::pair<FilenameTreeTokenType, std::regex>> atom_token_regexes = {
    { FilenameTreeTokenType::COMMA, utils::Tokenizer<FilenameTreeTokenType>::build_regex(",") },
    { FilenameTreeTokenType::OPENING_PARENTHESIS, utils::Tokenizer<FilenameTreeTokenType>::build_regex("\\(") },
    { FilenameTreeTokenType::CLOSING_PARENTHESIS, utils::Tokenizer<FilenameTreeTokenType>::build_regex("\\)") },
    { FilenameTreeTokenType::NAME, utils::Tokenizer<FilenameTreeTokenType>::build_regex("[a-z0-9_\\-]+") },
};

class FilenameTreeParser {
public:
    std::unique_ptr<FilenameTree> parse(utils::Tokenizer<FilenameTreeTokenType>::Tokens& tokens) {
        if (tokens.empty()) {
            throw std::runtime_error("FilenameTreeParser::parse - unexpected end of tokens.");
        }
        auto token = tokens.front();
        tokens.pop_front();
        if (!tokens.empty() && tokens.front().first == FilenameTreeTokenType::OPENING_PARENTHESIS) {
            // Consume "(".
            tokens.pop_front();
            std::vector<std::unique_ptr<FilenameTree>> children;
            while (!tokens.empty() && tokens.front().first != FilenameTreeTokenType::CLOSING_PARENTHESIS) {
                if (tokens.front().first == FilenameTreeTokenType::COMMA) {
                    tokens.pop_front();
                }
                children.push_back(parse(tokens));
            }
            // Consume ")".
            if (tokens.empty()) throw std::runtime_error("FilenameTreeParser::parse - Expected ')' is missing.");
            tokens.pop_front();
            return utils::make_unique_ptr<FilenameTree>(std::move(token.second), std::move(children));
        } else if (token.first == FilenameTreeTokenType::CLOSING_PARENTHESIS) {
            throw std::runtime_error("FilenameTreeParser::parse - Unexpected ')'");
        } else {
            return utils::make_unique_ptr<FilenameTree>(FilenameTree(std::move(token.second), {}));
        }
    }
};

/**
 */
class HierarchicalSearchEngine : public SearchEngine {
protected:
    std::shared_ptr<extra_tasks::PropositionalTask> propositional_task;
    std::shared_ptr<extra_tasks::ModifiedInitialStateTask> search_task;

    std::shared_ptr<goal_test::GoalTest> goal_test;
    HierarchicalSearchEngine* parent_search_engine;

    Plan plan;

protected:
    /**
     * Performs task transformation to ModifiedInitialStateTask.
     */
    explicit HierarchicalSearchEngine(const options::Options &opts);

    /**
     * React upon reaching goal state.
     */
    virtual void on_goal(const State &state, Plan&& partial_plan);

    /**
     * Setters
     */
    virtual void set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> propositional_task);
    virtual void set_parent_search_engine(HierarchicalSearchEngine& parent);
    virtual void set_initial_state(const State& state);
};
}

#endif
