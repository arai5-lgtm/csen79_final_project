#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include "kit/counters.h" // Uses the starter kit's counter definitions[cite: 9]

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

        // Constructs a new leaf node with height 1 and forwards the value payload.
        template <typename V>
        Node(const Key& k, V&& v)
            : key(k), value(std::forward<V>(v)), left(nullptr), right(nullptr), height(1) {}
    };

private:
    Node* root_ = nullptr;
    std::size_t size_ = 0;
    Compare comp_;
    mutable kit::Counters* counters_ = nullptr;

    // Increments the comparison counter if counter struct is bound.
    void inc_comparisons(uint64_t n = 1) const {
        if (counters_) counters_->comparisons += n;
    }

    // Increments the elements/nodes visited counter if counter struct is bound.
    void inc_visited(uint64_t n = 1) const {
        if (counters_) counters_->elements_visited += n;
    }

    // Increments the heap allocation counter when a new tree node is created.
    void inc_allocations(uint64_t n = 1) const {
        if (counters_) counters_->allocations += n;
    }

    // Increments the elements moved counter to record structural tree rotations.
    void inc_moved(uint64_t n = 1) const {
        if (counters_) counters_->elements_moved += n;
    }

    // Returns the cached height of node n, or 0 if n is null.
    int height(Node* n) const {
        return n ? n->height : 0;
    }

    // Computes the balance factor of node n.
    int balance_factor(Node* n) const {
        return n ? height(n->left) - height(n->right) : 0;
    }

    // Recalculates and updates the height of node n based on its children's heights.
    void update_height(Node* n) {
        if (n) {
            n->height = 1 + std::max(height(n->left), height(n->right));
        }
    }

    // Performs a single right rotation around node y to fix left-heavy imbalance.
    Node* rotate_right(Node* y) {
        inc_moved();
        Node* x = y->left;
        Node* T2 = x->right;

        x->right = y;
        y->left = T2;

        update_height(y);
        update_height(x);
        return x;
    }

    // Performs a single left rotation around node x to fix right-heavy imbalance.
    Node* rotate_left(Node* x) {
        inc_moved();
        Node* y = x->right;
        Node* T2 = y->left;

        y->left = x;
        x->right = T2;

        update_height(x);
        update_height(y);
        return y;
    }

    // Updates height and applies single or double rotations if |balance_factor| > 1.
    Node* rebalance(Node* node) {
        update_height(node);
        int bf = balance_factor(node);

        if (bf > 1) {
            if (balance_factor(node->left) < 0) {
                node->left = rotate_left(node->left); // Left-Right double rotation
            }
            return rotate_right(node); // Left-Left single rotation
        }

        if (bf < -1) {
            if (balance_factor(node->right) > 0) {
                node->right = rotate_right(node->right); // Right-Left double rotation
            }
            return rotate_left(node); // Right-Right single rotation
        }

        return node;
    }

    // Recursively inserts a key-value pair (or finds existing key) and rebalances on unwind.
    template <typename V>
    Node* insert_node(Node* node, const Key& key, V&& val, Value*& out_ptr) {
        if (!node) {
            inc_allocations();
            size_++;
            Node* created = new Node(key, std::forward<V>(val));
            out_ptr = &created->value;
            return created;
        }

        inc_visited();
        inc_comparisons();
        if (comp_(key, node->key)) {
            node->left = insert_node(node->left, key, std::forward<V>(val), out_ptr);
        } else {
            inc_comparisons();
            if (comp_(node->key, key)) {
                node->right = insert_node(node->right, key, std::forward<V>(val), out_ptr);
            } else {
                out_ptr = &node->value;
                return node;
            }
        }

        return rebalance(node);
    }

    // Walks down the left spine to find the minimum node in a subtree.
    Node* min_value_node(Node* node) const {
        Node* current = node;
        while (current && current->left) {
            inc_visited();
            current = current->left;
        }
        return current;
    }

    // Recursively locates and deletes a node by key, then rebalances along the path back up.
    Node* remove_node(Node* node, const Key& key, bool& removed) {
        if (!node) return nullptr;

        inc_visited();
        inc_comparisons();
        if (comp_(key, node->key)) {
            node->left = remove_node(node->left, key, removed);
        } else {
            inc_comparisons();
            if (comp_(node->key, key)) {
                node->right = remove_node(node->right, key, removed);
            } else {
                removed = true;
                if (!node->left || !node->right) {
                    Node* temp = node->left ? node->left : node->right;
                    delete node;
                    size_--;
                    return temp;
                } else {
                    Node* successor = min_value_node(node->right);
                    node->key = successor->key;
                    node->value = std::move(successor->value);
                    bool dummy = false;
                    node->right = remove_node(node->right, successor->key, dummy);
                }
            }
        }

        return rebalance(node);
    }

    // Performs a bounded in-order traversal, stopping once `limit` elements have been visited.
    template <typename Func>
    void in_order_limit(Node* node, std::size_t limit, std::size_t& count, Func&& fn) const {
        if (!node || count >= limit) return;

        inc_visited();
        in_order_limit(node->left, limit, count, fn);

        if (count < limit) {
            fn(node->key, node->value);
            count++;
            in_order_limit(node->right, limit, count, fn);
        }
    }

    // Post-order recursive helper that deallocates every node in the tree.
    void destroy_tree(Node* node) {
        if (!node) return;
        destroy_tree(node->left);
        destroy_tree(node->right);
        delete node;
    }

    // Deep-copies a subtree recursively while preserving node heights.
    Node* clone_tree(Node* other) {
        if (!other) return nullptr;
        inc_allocations();
        Node* copy = new Node(other->key, other->value);
        copy->height = other->height;
        copy->left = clone_tree(other->left);
        copy->right = clone_tree(other->right);
        return copy;
    }

public:
    // Initializes an empty AVL tree with an optional pointer to starter-kit counters.
    explicit AVLTree(kit::Counters* counters = nullptr) : counters_(counters) {}

    // Destructor that frees all dynamically allocated nodes to prevent memory leaks.
    ~AVLTree() {
        destroy_tree(root_);
    }

    // Copy constructor that performs a deep copy of another AVL tree.
    AVLTree(const AVLTree& other)
        : size_(other.size_), comp_(other.comp_), counters_(other.counters_) {
        root_ = clone_tree(other.root_);
    }

    // Copy assignment operator that cleans up existing nodes before deep-copying.
    AVLTree& operator=(const AVLTree& other) {
        if (this != &other) {
            destroy_tree(root_);
            size_ = other.size_;
            comp_ = other.comp_;
            counters_ = other.counters_;
            root_ = clone_tree(other.root_);
        }
        return *this;
    }

    // Move constructor that transfers ownership of the root pointer; O(1) time.
    AVLTree(AVLTree&& other) noexcept
        : root_(other.root_), size_(other.size_), comp_(std::move(other.comp_)), counters_(other.counters_) {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    // Move assignment operator that frees current nodes and takes ownership from `other`.
    AVLTree& operator=(AVLTree&& other) noexcept {
        if (this != &other) {
            destroy_tree(root_);
            root_ = other.root_;
            size_ = other.size_;
            comp_ = std::move(other.comp_);
            counters_ = other.counters_;
            other.root_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    // Updates the starter-kit counter struct.
    void bind_counters(kit::Counters* counters) {
        counters_ = counters;
    }

    // Inserts key with default_val if absent, or returns a pointer to the existing Value.
    template <typename V>
    Value* get_or_insert(const Key& key, V&& default_val) {
        Value* result = nullptr;
        root_ = insert_node(root_, key, std::forward<V>(default_val), result);
        return result;
    }

    // Searches iteratively for `key` and returns a mutable pointer to its Value, or nullptr.
    Value* find(const Key& key) {
        Node* curr = root_;
        while (curr) {
            inc_visited();
            inc_comparisons();
            if (comp_(key, curr->key)) {
                curr = curr->left;
            } else {
                inc_comparisons();
                if (comp_(curr->key, key)) {
                    curr = curr->right;
                } else {
                    return &curr->value;
                }
            }
        }
        return nullptr;
    }

    // Const overload of find() returning a read-only pointer to the Value, or nullptr.
    const Value* find(const Key& key) const {
        return const_cast<AVLTree*>(this)->find(key);
    }

    // Returns pointers to the best (lowest ask or highest bid) key and value.
    std::pair<const Key*, Value*> best() {
        if (!root_) return {nullptr, nullptr};
        inc_visited();
        Node* best_node = min_value_node(root_);
        return {&best_node->key, &best_node->value};
    }

    // Removes the node matching `key` from the tree and returns true if a node was deleted.
    bool erase(const Key& key) {
        bool removed = false;
        root_ = remove_node(root_, key, removed);
        return removed;
    }

    // Retrieves and processes top depth price levels in exact sorted order to implement BOOK [depth] command without sorting entire order book.
    template <typename Func>
    void traverse_top_n(std::size_t depth, Func&& fn) const {
        std::size_t count = 0;
        in_order_limit(root_, depth, count, std::forward<Func>(fn));
    }

    // Returns true if the tree contains zero price levels.
    bool empty() const { return size_ == 0; }

    // Returns the total number of active nodes (price levels) currently in the tree.
    std::size_t size() const { return size_; }

    // Calculates the total heap and object memory footprint in bytes for M5 benchmarks.
    std::size_t memory_bytes() const {
        return sizeof(*this) + size_ * sizeof(Node);
    }
};

} // namespace team
