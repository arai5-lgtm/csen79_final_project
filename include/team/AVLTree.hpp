#pragma once

// Stores unique keys, each with one Value (for the order book: a price level
// holding its FIFO queue of resting orders). Keys are ordered by Compare, so
// the "best" key is always the first one in Compare order:
//   asks: AVLTree<Price, Level>                       -> lowest ask first
//   bids: AVLTree<Price, Level, std::greater<Price>>  -> highest bid first
//
// Pointer stability: nodes are never copied or moved between tree positions
// except by relinking, so a Value* returned by find()/get_or_insert() stays
// valid until THAT key is erased (or the tree is cleared/destroyed). Erasing
// some other key never invalidates it.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include "kit/counters.h"

namespace team {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class AVLTree {
public:
    struct Node {
        Key key;
        Value value;
        Node* left;
        Node* right;
        int height;

        // Constructs a leaf node (height 1), forwarding the value payload.
        template <typename V>
        Node(const Key& k, V&& v)
            : key(k), value(std::forward<V>(v)), left(nullptr), right(nullptr), height(1) {}
    };

private:
    Node* root_ = nullptr;
    std::size_t size_ = 0;
    Compare comp_;
    mutable kit::Counters stats_;  // mutable so const lookups can be counted

    // Returns the cached height of n, or 0 if n is null.
    static int height(const Node* n) { return n ? n->height : 0; }

    // Returns height(left) - height(right); > 0 means left-heavy.
    static int balance_factor(const Node* n) {
        return n ? height(n->left) - height(n->right) : 0;
    }

    // Recomputes n->height from its children.
    static void update_height(Node* n) {
        n->height = 1 + std::max(height(n->left), height(n->right));
    }

    // Single right rotation around y (fixes a left-left imbalance).
    Node* rotate_right(Node* y) {
        KIT_COUNT(stats_, moves);
        Node* x = y->left;
        y->left = x->right;
        x->right = y;
        update_height(y);
        update_height(x);
        return x;
    }

    // Single left rotation around x (fixes a right-right imbalance).
    Node* rotate_left(Node* x) {
        KIT_COUNT(stats_, moves);
        Node* y = x->right;
        x->right = y->left;
        y->left = x;
        update_height(x);
        update_height(y);
        return y;
    }

    // Updates node's height and applies a single or double rotation if |balance| > 1.
    // Returns the new root of this subtree.
    Node* rebalance(Node* node) {
        update_height(node);
        const int bf = balance_factor(node);

        if (bf > 1) {
            if (balance_factor(node->left) < 0) node->left = rotate_left(node->left);  // left-right
            return rotate_right(node);                                                  // left-left
        }
        if (bf < -1) {
            if (balance_factor(node->right) > 0) node->right = rotate_right(node->right);  // right-left
            return rotate_left(node);                                                       // right-right
        }
        return node;
    }

 
    // Inserts (key, val) below node unless key exists; out_ptr receives the Value*
    // for that key either way. Rebalances on the way back up.
    template <typename V>
    Node* insert_node(Node* node, const Key& key, V&& val, Value*& out_ptr) {
        if (!node) {
            KIT_COUNT(stats_, allocations);
            ++size_;
            Node* created = new Node(key, std::forward<V>(val));
            out_ptr = &created->value;
            return created;
        }

        KIT_COUNT(stats_, visits);
        KIT_COUNT(stats_, comparisons);
        if (comp_(key, node->key)) {
            node->left = insert_node(node->left, key, std::forward<V>(val), out_ptr);
        } else {
            KIT_COUNT(stats_, comparisons);
            if (comp_(node->key, key)) {
                node->right = insert_node(node->right, key, std::forward<V>(val), out_ptr);
            } else {
                out_ptr = &node->value;  // key already present: nothing changes
                return node;
            }
        }
        return rebalance(node);
    }


    // Detaches the minimum node of the non-null subtree n and stores it in min_out
    Node* detach_min(Node* n, Node*& min_out) {
        KIT_COUNT(stats_, visits);
        if (!n->left) {
            min_out = n;
            return n->right;
        }
        n->left = detach_min(n->left, min_out);
        return rebalance(n);
    }

    // Removes the node whose key equals `key` (if any) and rebalances on the way up.
    // `key` must not alias a key stored inside the tree (erase() passes a copy).
    Node* remove_node(Node* node, const Key& key) {
        if (!node) return nullptr;

        KIT_COUNT(stats_, visits);
        KIT_COUNT(stats_, comparisons);
        if (comp_(key, node->key)) {
            node->left = remove_node(node->left, key);
        } else {
            KIT_COUNT(stats_, comparisons);
            if (comp_(node->key, key)) {
                node->right = remove_node(node->right, key);
            } else {
                // Found it.
                if (!node->left || !node->right) {
                    Node* child = node->left ? node->left : node->right;
                    delete node;
                    --size_;
                    return child;
                }
                // Two children: relink the in-order successor into this position
                // instead of copying its data, so surviving Value* stay valid.
                Node* successor = nullptr;
                Node* new_right = detach_min(node->right, successor);
                successor->left = node->left;
                successor->right = new_right;
                KIT_COUNT(stats_, moves);
                delete node;
                --size_;
                return rebalance(successor);
            }
        }
        return rebalance(node);
    }

    // Iterative search; returns the node holding `key`, or nullptr.
    Node* find_node(const Key& key) const {
        Node* curr = root_;
        while (curr) {
            KIT_COUNT(stats_, visits);
            KIT_COUNT(stats_, comparisons);
            if (comp_(key, curr->key)) {
                curr = curr->left;
            } else {
                KIT_COUNT(stats_, comparisons);
                if (comp_(curr->key, key)) curr = curr->right;
                else return curr;
            }
        }
        return nullptr;
    }

    // Returns the first node in Compare order (leftmost), or nullptr if empty.
    Node* leftmost() const {
        Node* curr = root_;
        while (curr) {
            KIT_COUNT(stats_, visits);
            if (!curr->left) break;
            curr = curr->left;
        }
        return curr;
    }

    // In-order traversal that stops as soon as `limit` elements were reported.
    // Visits O(limit + log n) nodes, never the whole tree.
    template <typename Func>
    void in_order_limit(const Node* node, std::size_t limit, std::size_t& count, Func& fn) const {
        if (!node || count >= limit) return;
        KIT_COUNT(stats_, visits);
        in_order_limit(node->left, limit, count, fn);
        if (count < limit) {
            fn(node->key, node->value);
            ++count;
            in_order_limit(node->right, limit, count, fn);
        }
    }

    // Post-order delete of every node below (and including) node.
    static void destroy_tree(Node* node) {
        if (!node) return;
        destroy_tree(node->left);
        destroy_tree(node->right);
        delete node;
    }

    // Deep-copies a subtree, preserving heights.
    Node* clone_tree(const Node* other) {
        if (!other) return nullptr;
        KIT_COUNT(stats_, allocations);
        Node* copy = new Node(other->key, other->value);
        copy->height = other->height;
        copy->left = clone_tree(other->left);
        copy->right = clone_tree(other->right);
        return copy;
    }

   // Checks ordering (lo < key < hi), cached heights, and |balance| <= 1 below n.
    bool validate_node(const Node* n, const Key* lo, const Key* hi,
                       int& h, std::size_t& count) const {
        if (!n) { h = 0; return true; }
        if (lo && !comp_(*lo, n->key)) return false;
        if (hi && !comp_(n->key, *hi)) return false;
        int hl = 0, hr = 0;
        if (!validate_node(n->left, lo, &n->key, hl, count)) return false;
        if (!validate_node(n->right, &n->key, hi, hr, count)) return false;
        ++count;
        h = 1 + std::max(hl, hr);
        return n->height == h && (hl - hr <= 1) && (hr - hl <= 1);
    }

public:
    // Creates an empty tree.
    explicit AVLTree(Compare comp = Compare()) : comp_(std::move(comp)) {}

    // Frees every node.
    ~AVLTree() { destroy_tree(root_); }

    // Deep copy (counters start fresh, then count the copy's allocations).
    AVLTree(const AVLTree& other) : size_(other.size_), comp_(other.comp_) {
        root_ = clone_tree(other.root_);
    }

    // Deep copy assignment.
    AVLTree& operator=(const AVLTree& other) {
        if (this != &other) {
            AVLTree tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // O(1) move: steals the nodes and the counters; `other` is left empty.
    AVLTree(AVLTree&& other) noexcept
        : root_(other.root_), size_(other.size_), comp_(std::move(other.comp_)),
          stats_(std::move(other.stats_)) {
        other.root_ = nullptr;
        other.size_ = 0;
        other.stats_.reset();
    }

    // Move assignment: frees current nodes, then steals from `other`.
    AVLTree& operator=(AVLTree&& other) noexcept {
        if (this != &other) {
            destroy_tree(root_);
            root_ = other.root_;
            size_ = other.size_;
            comp_ = std::move(other.comp_);
            stats_ = std::move(other.stats_);
            other.root_ = nullptr;
            other.size_ = 0;
            other.stats_.reset();
        }
        return *this;
    }

    // Swaps contents (nodes, comparator, counters) with another tree in O(1).
    void swap(AVLTree& other) noexcept {
        std::swap(root_, other.root_);
        std::swap(size_, other.size_);
        std::swap(comp_, other.comp_);
        std::swap(stats_, other.stats_);
    }

 // Read-only access to this tree's operation counters.
    const kit::Counters& stats() const { return stats_; }

    // Zeroes the counters.
    void reset_stats() { stats_.reset(); }



    // Returns a pointer to the Value stored under `key`, inserting default_val first
    // if the key is absent. The pointer stays valid until this key is erased.
    template <typename V>
    Value* get_or_insert(const Key& key, V&& default_val) {
        Value* result = nullptr;
        root_ = insert_node(root_, key, std::forward<V>(default_val), result);
        return result;
    }

    // Same, but a new entry is value-initialized (Value{}).
    Value* get_or_insert(const Key& key) { return get_or_insert(key, Value{}); }

    // Returns a pointer to the Value for `key`, or nullptr if absent. O(log n).
    Value* find(const Key& key) {
        Node* n = find_node(key);
        return n ? &n->value : nullptr;
    }

    // Const overload: read-only pointer, or nullptr.
    const Value* find(const Key& key) const {
        const Node* n = find_node(key);
        return n ? &n->value : nullptr;
    }

    // True if `key` is present.
    bool contains(const Key& key) const { return find_node(key) != nullptr; }

    // Returns the best (first in Compare order) key and its Value, or {nullptr, nullptr} when empty. 
    // O(log n) time;  The pointers stay valid until that key is erased.
    std::pair<const Key*, Value*> best() {
        Node* n = leftmost();
        if (!n) return {nullptr, nullptr};
        return {&n->key, &n->value};
    }

    // Const overload of best().
    std::pair<const Key*, const Value*> best() const {
        const Node* n = leftmost();
        if (!n) return {nullptr, nullptr};
        return {&n->key, &n->value};
    }

    // Removes `key`. Returns true if a node was removed. O(log n).
    bool erase(const Key& key) {
        const Key key_copy = key;  // `key` may point into the node being deleted (e.g. *best().first)
        const std::size_t before = size_;
        root_ = remove_node(root_, key_copy);
        return size_ < before;
    }

    // Calls fn(key, value) on the first `depth` entries in Compare order (best first)
    // without sorting or visiting the whole tree. Used for BOOK [depth].
    template <typename Func>
    void traverse_top_n(std::size_t depth, Func&& fn) const {
        std::size_t count = 0;
        in_order_limit(root_, depth, count, fn);
    }

    // Removes every entry.
    void clear() {
        destroy_tree(root_);
        root_ = nullptr;
        size_ = 0;
    }

    // True if the tree holds no entries.
    bool empty() const { return size_ == 0; }

    // Number of entries (price levels).
    std::size_t size() const { return size_; }

    // Tree height (0 for an empty tree); at most about 1.44 * log2(n + 2).
    int height() const { return height(root_); }

    // Checks BST order, cached heights, AVL balance and size_.
    bool validate() const {
        int h = 0;
        std::size_t count = 0;
        return validate_node(root_, nullptr, nullptr, h, count) && count == size_;
    }

    // Bytes used by the tree structure itself: the object plus one Node per entry.
    // Heap memory owned INSIDE each Value and allocator overhead are not included.
    std::size_t memory_bytes() const { return sizeof(*this) + size_ * sizeof(Node); }
};

} // namespace team
