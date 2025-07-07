#pragma once

#include "state_registry.h"
#include "algorithms/subscriber.h"
#include "utils/collections.h"
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <list>
#include <tuple>

/*
  PerStateInformation: Key-value variant, value references are stable for object lifetime.

  We keep per-registry hash tables StateID → iterator-into-list-of-Entries,
  and all entries in a std::list for reference/pointer stability.
*/
template<class Entry>
class PerStateInformation : public subscriber::Subscriber<StateRegistry> {
    using StateKey = std::pair<const StateRegistry*, int>; // registry*, state id value
    using EntryList = std::list<Entry>;
    using EntryIter = typename EntryList::iterator;
    using StateIdToIter = std::unordered_map<int, EntryIter>; // state id value → Entry
    using RegistryMap = std::unordered_map<const StateRegistry*, StateIdToIter>;

    Entry default_value;
    RegistryMap reg_to_map; ///< Per-registry state id → entry iterator
    EntryList all_entries;  ///< Storage: references never invalidated.

    mutable const StateRegistry *cached_registry = nullptr;
    mutable StateIdToIter* cached_map = nullptr;

    // Utility: get state id (as int) from "State"
    static int state_id_value(const State& state) { return state.get_id().get_value(); }

    // Get map for registry; create if missing. Set cache.
    StateIdToIter& get_map(const StateRegistry* reg) {
        if (cached_registry != reg) {
            cached_registry = reg;
            auto [it,emplaced] = reg_to_map.try_emplace(reg);
            if (emplaced) reg->subscribe(this);
            cached_map = &it->second;
        }
        return *cached_map;
    }

    // Get map for registry; const view. (null if not present)
    const StateIdToIter* get_map(const StateRegistry* reg) const {
        if (cached_registry != reg) {
            auto it = reg_to_map.find(reg);
            if (it == reg_to_map.end()) return nullptr;
            cached_registry = reg;
            cached_map = const_cast<StateIdToIter*>(&it->second);
        }
        return cached_map;
    }

public:
    PerStateInformation()
      : default_value() {}

    explicit PerStateInformation(const Entry& def)
      : default_value(def) {}

    PerStateInformation(const PerStateInformation<Entry>&) = delete;
    PerStateInformation& operator=(const PerStateInformation<Entry>&) = delete;

    virtual ~PerStateInformation() override {
        // (Lists/maps auto-release. No heap pointer management.)
    }

    Entry &operator[](const State &state) {
        const StateRegistry* reg = state.get_registry();
        if (!reg) {
            std::cerr << "Tried to access per-state information with an "
                      << "unregistered state." << std::endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
        int sid = state_id_value(state);

        StateIdToIter& idmap = get_map(reg);
        auto it = idmap.find(sid);
        if (it == idmap.end()) {
            // Insert new entry in list, stash its iterator
            all_entries.emplace_back(default_value);
            auto entry_it = std::prev(all_entries.end());
            idmap.emplace(sid, entry_it);
            return *entry_it;
        }
        return *it->second;
    }

    const Entry& operator[](const State& state) const {
        const StateRegistry* reg = state.get_registry();
        if (!reg) {
            std::cerr << "Tried to access per-state information with an "
                      << "unregistered state." << std::endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
        int sid = state_id_value(state);

        const StateIdToIter* idmap = get_map(reg);
        if (!idmap) return default_value;
        auto it = idmap->find(sid);
        if (it == idmap->end()) return default_value;
        return *(it->second);
    }

    virtual void notify_service_destroyed(const StateRegistry* registry) override {
        // Remove all entries belonging to this registry (walk idmap, remove from all_entries)
        auto it = reg_to_map.find(registry);
        if (it != reg_to_map.end()) {
            for (auto& [sid, liter] : it->second)
                all_entries.erase(liter);
            reg_to_map.erase(it);
        }
        // Clear cache if the destroyed registry was cached
        if (registry == cached_registry) {
            cached_registry = nullptr;
            cached_map = nullptr;
        }
    }
};

