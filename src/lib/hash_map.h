#pragma once

// lib::HashMap: a hash map that walks its entries in the order game.exe's STLport hash_map did, for the maps
// whose walk decides what the game does (which object is destroyed first, which unit a loop stops at, which
// prototype the plugin's fallback picks). Maps that are only looked up are std::unordered_map.
//
// The order: a prime number of buckets (HashPrime of the size hint, 100 by default: 193), a key in bucket
// hash % buckets, a new key at the head of its bucket's chain, buckets walked in order and each chain from its
// head. Before an insert the table grows to HashPrime(size + 1) buckets when that is more than it has; each old
// chain then moves head first to the head of its new bucket (so keys sharing a new bucket come out reversed).

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lib {

// The bucket count for a size hint: the first of STLport's primes not below it (the largest past the end).
std::size_t HashPrime(std::size_t hint);

// A dword key hashed as itself (ints, ids, pointers).
struct DwordHash {
    template <typename T>
    std::uint32_t operator()(T v) const {
        if constexpr (std::is_pointer_v<T>) return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(v));
        else return static_cast<std::uint32_t>(v);
    }
};
// A string key: h = 5 * h + c over its characters, signed (STLport's __stl_hash_string in this build).
struct TextHash {
    std::uint32_t operator()(const std::string& s) const {
        std::uint32_t h = 0;
        for (char c : s) h = h * 5 + static_cast<std::uint32_t>(static_cast<int>(c));
        return h;
    }
};

template <typename K, typename V, typename Hash = DwordHash>
class HashMap {
    struct Node {
        Node* next;
        std::pair<const K, V> value;
    };

    template <bool Const>
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const K, V>;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<Const, const value_type*, value_type*>;
        using reference = std::conditional_t<Const, const value_type&, value_type&>;

        Iterator() = default;
        // (a template, so the copy constructor stays the implicit one)
        template <bool C = Const, std::enable_if_t<C, int> = 0>
        Iterator(const Iterator<false>& other) : node_(other.node_), map_(other.map_) {}

        reference operator*() const { return node_->value; }
        pointer operator->() const { return &node_->value; }
        Iterator& operator++() {
            node_ = map_->After(node_);
            return *this;
        }
        Iterator operator++(int) {
            Iterator old = *this;
            ++*this;
            return old;
        }
        friend bool operator==(const Iterator& a, const Iterator& b) { return a.node_ == b.node_; }

    private:
        friend class HashMap;
        template <bool> friend class Iterator;
        Iterator(Node* node, const HashMap* map) : node_(node), map_(map) {}
        Node* node_ = nullptr;
        const HashMap* map_ = nullptr;
    };

    // One bucket's chain from its head.
    template <bool Const>
    class LocalIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const K, V>;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<Const, const value_type*, value_type*>;
        using reference = std::conditional_t<Const, const value_type&, value_type&>;

        LocalIterator() = default;
        reference operator*() const { return node_->value; }
        pointer operator->() const { return &node_->value; }
        LocalIterator& operator++() {
            node_ = node_->next;
            return *this;
        }
        friend bool operator==(const LocalIterator& a, const LocalIterator& b) { return a.node_ == b.node_; }

    private:
        friend class HashMap;
        explicit LocalIterator(Node* node) : node_(node) {}
        Node* node_ = nullptr;
    };

public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<const K, V>;
    using size_type = std::size_t;
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using local_iterator = LocalIterator<false>;
    using const_local_iterator = LocalIterator<true>;

    explicit HashMap(size_type hint = 100) : buckets_(HashPrime(hint), nullptr) {}
    // The same buckets, each chain copied in order.
    HashMap(const HashMap& other) : buckets_(other.buckets_.size(), nullptr), count_(other.count_) { CopyChains(other); }
    HashMap(HashMap&& other) noexcept : buckets_(std::move(other.buckets_)), count_(other.count_) {
        other.buckets_.clear();
        other.count_ = 0;
    }
    HashMap& operator=(const HashMap& other) {
        if (&other == this) return *this;
        clear();
        buckets_.assign(other.buckets_.size(), nullptr);
        count_ = other.count_;
        CopyChains(other);
        return *this;
    }
    HashMap& operator=(HashMap&& other) noexcept {
        if (&other == this) return *this;
        clear();
        buckets_.swap(other.buckets_);
        count_ = other.count_;
        other.count_ = 0;
        return *this;
    }
    ~HashMap() { clear(); }

    iterator begin() { return {First(), this}; }
    const_iterator begin() const { return {First(), this}; }
    iterator end() { return {}; }
    const_iterator end() const { return {}; }
    size_type size() const { return count_; }
    bool empty() const { return count_ == 0; }

    size_type bucket_count() const { return buckets_.size(); }
    size_type bucket(const K& key) const { return Hash()(key) % buckets_.size(); }
    local_iterator begin(size_type b) { return local_iterator(buckets_[b]); }
    const_local_iterator begin(size_type b) const { return const_local_iterator(buckets_[b]); }
    local_iterator end(size_type) { return {}; }
    const_local_iterator end(size_type) const { return {}; }

    iterator find(const K& key) { return {FindNode(key), this}; }
    const_iterator find(const K& key) const { return {FindNode(key), this}; }
    size_type count(const K& key) const { return FindNode(key) ? 1 : 0; }
    bool contains(const K& key) const { return FindNode(key) != nullptr; }

    // The table grows for one more entry (also when the key is there), then the key's value, a
    // value-initialised one inserted at the head of its bucket when there is none.
    V& operator[](const K& key) { return try_emplace(key).first->second; }
    template <typename... A>
    std::pair<iterator, bool> try_emplace(const K& key, A&&... arguments) {
        reserve(count_ + 1);
        size_type b = bucket(key);
        for (Node* n = buckets_[b]; n; n = n->next)
            if (n->value.first == key) return {iterator(n, this), false};
        Node* n = new Node{buckets_[b], std::pair<const K, V>(std::piecewise_construct, std::forward_as_tuple(key),
                                                              std::forward_as_tuple(std::forward<A>(arguments)...))};
        buckets_[b] = n;
        ++count_;
        return {iterator(n, this), true};
    }
    std::pair<iterator, bool> insert(const value_type& value) { return try_emplace(value.first, value.second); }

    // Erases the entry; the one after it in walk order.
    iterator erase(const_iterator position) {
        Node* node = position.node_;
        Node* next = After(node);
        Node** slot = &buckets_[bucket(node->value.first)];
        while (*slot != node) slot = &(*slot)->next;
        *slot = node->next;
        delete node;
        --count_;
        return {next, this};
    }
    iterator erase(iterator position) { return erase(const_iterator(position)); }
    size_type erase(const K& key) {
        Node* node = FindNode(key);
        if (!node) return 0;
        erase(const_iterator(node, this));
        return 1;
    }
    // Every chain freed from its head; the buckets stay.
    void clear() {
        for (Node*& head : buckets_) {
            for (Node* n = head; n;) {
                Node* next = n->next;
                delete n;
                n = next;
            }
            head = nullptr;
        }
        count_ = 0;
    }

    // STLport's resize(hint): HashPrime(hint) buckets when that is more than there are, each old chain moved
    // head first to the head of its new bucket.
    void reserve(size_type hint) {
        size_type old = buckets_.size();
        if (hint <= old) return;
        size_type n = HashPrime(hint);
        if (n <= old) return;
        std::vector<Node*> fresh(n, nullptr);
        for (Node*& head : buckets_) {
            while (Node* first = head) {
                size_type to = Hash()(first->value.first) % n;
                head = first->next;
                first->next = fresh[to];
                fresh[to] = first;
            }
        }
        buckets_.swap(fresh);
    }

private:
    Node* FindNode(const K& key) const {
        if (buckets_.empty()) return nullptr;
        for (Node* n = buckets_[bucket(key)]; n; n = n->next)
            if (n->value.first == key) return n;
        return nullptr;
    }
    Node* First() const {
        for (Node* head : buckets_)
            if (head) return head;
        return nullptr;
    }
    Node* After(const Node* node) const {
        if (node->next) return node->next;
        for (size_type b = bucket(node->value.first) + 1; b < buckets_.size(); ++b)
            if (buckets_[b]) return buckets_[b];
        return nullptr;
    }
    void CopyChains(const HashMap& other) {
        for (size_type b = 0; b < other.buckets_.size(); ++b) {
            Node** tail = &buckets_[b];
            for (const Node* n = other.buckets_[b]; n; n = n->next) {
                *tail = new Node{nullptr, n->value};
                tail = &(*tail)->next;
            }
        }
    }

    std::vector<Node*> buckets_;
    size_type count_ = 0;
};

} // namespace lib
