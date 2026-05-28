#ifndef TRANSLATE_GROUNDING_SYMBOLS_H
#define TRANSLATE_GROUNDING_SYMBOLS_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace translate::grounding {
/*
  Interning table for Datalog argument names (object/constant names,
  variable names, and the string forms of integer constants). The
  grounded model holds ~10M atoms on hard-to-ground instances; storing
  each argument as a 4-byte symbol id instead of a 40-byte
  std::variant<std::string,int> (plus a heap buffer per atom) is the
  dominant memory win there.

  Ids are assigned first-come. To keep the model's atom ordering --
  and therefore the byte-for-byte output -- identical to the Python
  translator, callers must compare arguments by *name* (via name())
  where ordering matters; equality and hashing may use ids directly,
  since equal names always intern to the same id.

  One process performs one translation, so a single static table
  suffices. It lives for the whole run because instantiate reads the
  model back through it.
*/
class SymbolTable {
public:
    int intern(const std::string &s) {
        auto it = ids_.find(s);
        if (it != ids_.end()) return it->second;
        int id = static_cast<int>(names_.size());
        names_.push_back(s);
        ids_.emplace(s, id);
        return id;
    }
    const std::string &name(int id) const { return names_[id]; }
    std::size_t size() const { return names_.size(); }

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, int> ids_;
};

// Process-wide symbol table for grounding arguments.
SymbolTable &symbols();
}

#endif
