#pragma once

// std_map_tree.h
//
// Wraps std::map<Key, Value, Compare> so the order book can swap implementations
// (--impl=main|alt) and the benchmarks can compare the hand-built AVL tree against
// the STL container it replaces (M3), count memory per element (M5), etc.
//
//   template <class K, class V, class C> using MainTree = team::AVLTree<K, V, C>;
//   template <class K, class V, class C> using AltTree  = team::StdMapTree<K, V, C>;
//
// Counters: `comparisons` is counted exactly (a counting comparator is handed to
// std::map), and `allocations` is counted once per new node. std::map's internal
// node walks and rotations are not observable, so `visits` and `moves` stay 0 for
// this implementation; compare the two trees on `comparisons`, time and memory.
//
// Pointer stability matches AVLTree: std::map never relocates elements, so a
// Value* stays valid until that key is erased.

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "kit/counters.h"

namespace team {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class StdMapTree {
    // Forwards to the user's Compare and counts every call.
    struct CountingCompare {
        Compare comp;
        kit::Counters* stats;  // points at the heap-allocated counters owned by the tree

        bool operator()(const Key& a, const Key& b) const {
            if (stats) KIT_COUNT(*stats, comparisons);
            return comp(a, b);
        }
    };

    using Map = std::map<Key, Value, CountingCompare>;

    // Heap-allocated so the comparator's pointer stays valid when the tree is moved.
    std::unique_ptr<kit::Counters> stats_;
    Map map_;

public:
    // Creates an empty tree.
    explicit StdMapTree(Compare comp = Compare())
        : stats_(new kit::Counters()), map_(CountingCompare{std::move(comp), stats_.get()}) {}

    // Deep copy (counters start at zero).
    StdMapTree(const StdMapTree& other)
        : stats_(new kit::Counters()),
          map_(CountingCompare{other.map_.key_comp().comp, stats_.get()}) {
        map_.insert(other.map_.begin(), other.map_.end());
        stats_->reset();
    }

    // Copy assignment (copy-and-swap).
    StdMapTree& operator=(const StdMapTree& other) {
        if (this != &other) {
            StdMapTree tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // Move: the heap counters and the map's nodes transfer without copying.
    StdMapTree(StdMapTree&&) noexcept = default;
    StdMapTree& operator=(StdMapTree&&) noexcept = default;

    // Swaps contents (nodes, comparator, counters) with another tree.
    void swap(StdMapTree& other) noexcept {
        stats_.swap(other.stats_);
        map_.swap(other.map_);
    }

    // counters 

    // Read-only access to this tree's operation counters.
    const kit::Counters& stats() const { return *stats_; }

    // Zeroes the counters.
    void reset_stats() { stats_->reset(); }

    //  main interface 

    // Returns a pointer to the Value stored under `key`, inserting default_val first
    // if the key is absent.
    template <typename V>
    Value* get_or_insert(const Key& key, V&& default_val) {
        auto it = map_.lower_bound(key);
        if (it == map_.end() || map_.key_comp()(key, it->first)) {
            KIT_COUNT(*stats_, allocations);
            it = map_.emplace_hint(it, key, std::forward<V>(default_val));
        }
        return &it->second;
    }

    // Same, but a new entry is value-initialized (Value{}).
    Value* get_or_insert(const Key& key) { return get_or_insert(key, Value{}); }

    // Returns a pointer to the Value for `key`, or nullptr if absent.
    Value* find(const Key& key) {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    // Const overload: read-only pointer, or nullptr.
    const Value* find(const Key& key) const {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    // True if `key` is present.
    bool contains(const Key& key) const { return map_.find(key) != map_.end(); }

    // Returns the first key in Compare order and its Value, or {nullptr, nullptr}.
    std::pair<const Key*, Value*> best() {
        if (map_.empty()) return {nullptr, nullptr};
        auto it = map_.begin();
        return {&it->first, &it->second};
    }

    // Const overload of best().
    std::pair<const Key*, const Value*> best() const {
        if (map_.empty()) return {nullptr, nullptr};
        auto it = map_.begin();
        return {&it->first, &it->second};
    }

    // Removes `key`. Returns true if an entry was removed.
    bool erase(const Key& key) {
        const Key key_copy = key;  // `key` may point into the node being erased
        return map_.erase(key_copy) > 0;
    }

    // Calls fn(key, value) on the first `depth` entries in Compare order.
    template <typename Func>
    void traverse_top_n(std::size_t depth, Func&& fn) const {
        std::size_t count = 0;
        for (auto it = map_.begin(); it != map_.end() && count < depth; ++it, ++count) {
            fn(it->first, it->second);
        }
    }

    // Removes every entry.
    void clear() { map_.clear(); }

    // True if empty.
    bool empty() const { return map_.empty(); }

    // Number of entries.
    std::size_t size() const { return map_.size(); }

    // Checks that keys are strictly increasing in Compare order. Does not touch counters.
    bool validate() const {
        const Compare& comp = map_.key_comp().comp;
        const Key* prev = nullptr;
        for (const auto& kv : map_) {
            if (prev && !comp(*prev, kv.first)) return false;
            prev = &kv.first;
        }
        return true;
    }

    // Estimated bytes used by the tree structure: the object plus, per entry, the stored
    // pair and the red-black node header (color + parent/left/right pointers = 32 bytes
    // in libstdc++ and libc++). Heap memory inside each Value and allocator overhead
    // are not included, same as AVLTree::memory_bytes().
    std::size_t memory_bytes() const {
        constexpr std::size_t kNodeHeader = 32;
        return sizeof(*this) + sizeof(kit::Counters) +
               map_.size() * (kNodeHeader + sizeof(std::pair<const Key, Value>));
    }
};

} // namespace team
