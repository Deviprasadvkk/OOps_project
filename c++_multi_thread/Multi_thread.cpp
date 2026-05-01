// ============================================================================
// INCLUDES: What libraries we use and why
// ============================================================================

#include <atomic>        // Provides std::atomic_flag for safe lockless synchronization
#include <iostream>      // Provides std::cout, std::endl for printing output
#include <list>          // Provides std::list: a doubly-linked list data structure
#include <stdexcept>     // Provides std::invalid_argument for error handling
#include <string>        // Provides std::string for string keys
#include <unordered_map> // Provides std::unordered_map: a hash table for O(1) lookups
#include <utility>       // Provides std::pair and std::make_pair for tuples

// ============================================================================
// SPINLOCK: A simple, lightweight mutual exclusion mechanism
// ============================================================================
//
// A spinlock is a synchronization primitive that prevents multiple threads
// from accessing shared data at the same time. Unlike std::mutex, a spinlock
// busily waits ("spins") rather than sleeping. This is fast for short waits.
//
// How it works:
// - We use an std::atomic_flag as the underlying state (true = locked).
// - test_and_set() atomically reads the flag and sets it to true.
// - If another thread is holding the lock, we keep looping (spinning).
// - When the lock is released, we stop spinning and proceed.
//
// Memory ordering:
// - memory_order_acquire: after lock(), everything we read/write happens after the lock point.
// - memory_order_release: before unlock(), all reads/writes have completed.
// This prevents the CPU from reordering operations across the lock boundary.

class SpinLock
{
public:
    // lock(): Acquire the lock. Block (spin) until we own it.
    void lock()
    {
        // Keep testing and setting the flag until test_and_set() returns false.
        // test_and_set(memory_order_acquire) atomically:
        //   1. Reads the current value of flag_
        //   2. Sets flag_ to true
        //   3. Returns the old value
        // If the old value was false (nobody had it), we proceed with the lock.
        // If the old value was true (somebody has it), we loop again (spin).
        while (flag_.test_and_set(std::memory_order_acquire))
        {
            // Empty loop body. We just keep spinning until lock is acquired.
            // In production code, you might add a hint like __builtin_pause() here.
        }
    }

    // unlock(): Release the lock so other threads can acquire it.
    void unlock()
    {
        // clear(memory_order_release) atomically sets flag_ to false.
        // The memory_order_release ensures all our prior reads/writes
        // are fully visible to other threads before the lock is released.
        flag_.clear(std::memory_order_release);
    }

private:
    // The flag_ is an atomic boolean that represents lock state.
    // atomic_flag is the simplest atomic type with lock-free guarantees.
    std::atomic_flag flag_;

public:
    // Constructor: Initialize the flag as "unlocked" (false).
    SpinLock() : flag_(ATOMIC_FLAG_INIT) {}
};

// ============================================================================
// LOCK GUARD: RAII for automatic lock management
// ============================================================================
//
// RAII = Resource Acquisition Is Initialization.
// The idea: a C++ object's lifetime matches the resource's lifetime.
//
// LockGuard ensures that:
// - When constructed, it acquires the lock.
// - When destructed (goes out of scope), it releases the lock.
// This is safer than manual lock() / unlock() because even if an exception
// is thrown, the destructor still runs and releases the lock.
//
// Example usage:
//   {
//       LockGuard guard(my_lock);    // Constructor: lock is acquired here
//       // use shared data safely
//   }  // End of scope: destructor runs, lock is released automatically

class LockGuard
{
public:
    // Constructor: Take a reference to the lock and immediately acquire it.
    // 'explicit' prevents accidental implicit conversions.
    explicit LockGuard(SpinLock &lock)
        : lock_(lock) // Store reference to the lock
    {
        // Acquire the lock right now in the constructor.
        lock_.lock();
    }

    // Destructor: Called when LockGuard goes out of scope.
    // This automatically releases the lock.
    ~LockGuard()
    {
        lock_.unlock();
    }

    // Delete copy constructor and assignment operator.
    // We don't want two LockGuard objects controlling the same lock because
    // that would cause double-unlock bugs. By deleting these, we prevent copies.
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    // Reference to the spinlock we're guarding.
    SpinLock &lock_;
};

// ============================================================================
// LRU CACHE: A bounded, thread-safe cache with O(1) get and put operations
// ============================================================================
//
// What is an LRU cache?
// An LRU (Least Recently Used) cache stores a fixed number of key-value pairs.
// When it is full and a new item is inserted, the least recently used item
// is automatically removed to make room.
//
// Why two data structures?
// - std::unordered_map (hash table) for O(1) key lookup.
// - std::list (doubly-linked list) to track usage order.
//
// The list is ordered: front = most recently used, back = least recently used.
// The map stores: key -> iterator to the node in the list.
// This combination gives us O(1) for all operations: get, put, evict.
//
// Thread safety:
// A single SpinLock protects both the map and the list together. They must be
// kept in sync: every get/put operation must lock, update both structures,
// then unlock. This is why we use RAII (LockGuard) to ensure unlock happens.

template <typename Key, typename Value>
class LRUCache
{
public:
    // Constructor: Initialize the cache with a fixed capacity.
    // Capacity must be > 0, otherwise we'd have a useless cache.
    explicit LRUCache(std::size_t capacity)
        : capacity_(capacity) // Store the max number of items we can hold
    {
        // Validate the input. If capacity is 0, throw an exception.
        // This is called "defensive programming": fail fast with a clear error.
        if (capacity_ == 0)
        {
            throw std::invalid_argument("LRUCache capacity must be greater than zero");
        }
    }

    // ========================================================================
    // get(): Retrieve a value by key and update recency order
    // ========================================================================
    //
    // Behavior:
    // - If the key exists: return true, set outValue to the stored value,
    //   and mark the key as most recently used.
    // - If the key does not exist: return false, don't modify outValue.
    //
    // Time complexity: O(1) on average (hash table lookup + list splice).
    //
    // Why do we update recency on a get?
    // Because accessing an item means it is being used now, so it should
    // be less likely to be evicted. If we don't, frequently-accessed items
    // might get removed if they are accessed but not written to.

    bool get(const Key &key, Value &outValue)
    {
        // CRITICAL: Acquire the lock so no other thread can modify the cache
        // while we are reading and updating. The lock is held for the entire
        // scope of this function. When this function returns or throws, the
        // destructor of LockGuard runs and releases the lock automatically.
        LockGuard lock(lock_);

        // Verbose type name required in older C++ (C++11).
        // In C++17, we could write: auto mapIt = index_.find(key);
        // This looks up the key in the hash map.
        // find() returns an iterator (pointer) to the entry if found,
        // or index_.end() if not found.
        typename std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator>::iterator mapIt =
            index_.find(key);

        // Check if the key was found. end() means "not found".
        if (mapIt == index_.end())
        {
            // Key does not exist. Increment the miss counter and return false.
            // This is useful for cache statistics: hit rate = hits / (hits + misses).
            ++misses_;
            return false;
        }

        // Key found! Now update the recency order in the linked list.
        // mapIt->second is an iterator to the list node containing this key-value pair.
        //
        // splice() is a list operation that moves nodes around without copying data.
        // It takes: (target position, source list, node to move).
        // Here we move the accessed node to items_.begin() (the front),
        // making it the most recently used.
        items_.splice(items_.begin(), items_, mapIt->second);

        // Increment the hit counter for statistics.
        ++hits_;

        // Extract the value from the list node and store it in the output parameter.
        // mapIt->second is the iterator to the node.
        // mapIt->second->second is the value part of the (key, value) pair.
        outValue = mapIt->second->second;

        // Success!
        return true;
    }

    // ========================================================================
    // put(): Insert or update a key-value pair, handle eviction if needed
    // ========================================================================
    //
    // Behavior:
    // - If the key already exists: update its value and move it to the front.
    // - If the key is new:
    //   - If cache is not full: just insert it at the front.
    //   - If cache is full: remove the least recently used item (at the back),
    //     then insert the new item at the front.
    //
    // Time complexity: O(1) on average.
    //
    // Invariant maintained:
    // The front of the list is always the most recently used item.
    // The back is always the least recently used (next to be evicted).

    void put(const Key &key, const Value &value)
    {
        // Acquire the lock. Held for the entire function.
        LockGuard lock(lock_);

        // Look up the key in the map to see if it already exists.
        typename std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator>::iterator mapIt =
            index_.find(key);

        // Case 1: Key already exists. Update it.
        if (mapIt != index_.end())
        {
            // mapIt->second is the list iterator for this key-value node.
            // mapIt->second->second is the value we need to update.
            mapIt->second->second = value;

            // Move the updated node to the front (most recently used).
            items_.splice(items_.begin(), items_, mapIt->second);

            // Done. No need to check capacity; we're just updating.
            return;
        }

        // Case 2: Key is new. We need to insert it.
        // First, check if the cache is full.
        if (items_.size() == capacity_)
        {
            // Cache is full. Evict the least recently used item (at the back).
            // items_.back() returns a reference to the last node in the list.
            std::pair<Key, Value> &lruEntry = items_.back();

            // Remove the key from the map so lookups won't find it.
            index_.erase(lruEntry.first);

            // Remove the node from the list. pop_back() removes the last element.
            items_.pop_back();

            // After this, we have room for one more item.
        }

        // Now insert the new entry at the front of the list (most recently used).
        // push_front() adds an element at the beginning.
        // std::make_pair creates a (key, value) pair.
        items_.push_front(std::make_pair(key, value));

        // Add the key to the map, pointing to the front of the list.
        // items_.front().first is the key part of the first node.
        // items_.begin() is an iterator to the first node.
        // After this, map lookups will find the key and direct us to the list node.
        index_[items_.front().first] = items_.begin();
    }

    // ========================================================================
    // contains(): Check if a key is in the cache
    // ========================================================================
    // Locks and does a simple map lookup. O(1) on average.
    bool contains(const Key &key) const
    {
        LockGuard lock(lock_);
        // find() returns end() if not found.
        return index_.find(key) != index_.end();
    }

    // ========================================================================
    // size(): Return the current number of items in the cache
    // ========================================================================
    // Locks and returns the size of the list (or map, they are always equal).
    std::size_t size() const
    {
        LockGuard lock(lock_);
        return items_.size();
    }

    // ========================================================================
    // capacity(): Return the maximum number of items the cache can hold
    // ========================================================================
    // No lock needed: capacity_ never changes after construction.
    std::size_t capacity() const
    {
        return capacity_;
    }

    // ========================================================================
    // hits(): Return the number of successful cache get() operations
    // ========================================================================
    // Locks because hits_ can be modified by get() calls in other threads.
    std::size_t hits() const
    {
        LockGuard lock(lock_);
        return hits_;
    }

    // ========================================================================
    // misses(): Return the number of failed cache get() operations
    // ========================================================================
    // Locks because misses_ can be modified by get() calls in other threads.
    std::size_t misses() const
    {
        LockGuard lock(lock_);
        return misses_;
    }

    // ========================================================================
    // clear(): Empty the cache completely and reset statistics
    // ========================================================================
    void clear()
    {
        LockGuard lock(lock_);
        // Remove all items from the list.
        items_.clear();
        // Remove all entries from the map.
        index_.clear();
        // Reset statistics.
        hits_ = 0;
        misses_ = 0;
    }

    // ========================================================================
    // print_state(): Debug helper to visualize cache order
    // ========================================================================
    // This is NOT a production method; it is for learning and debugging.
    // It prints the cache from most recently used (front) to least recently used (back).
    void print_state() const
    {
        // Lock before reading the data structures.
        LockGuard lock(lock_);

        std::cout << "Cache state [MRU -> LRU]: ";

        // Iterate through the list from front to back.
        // We use an explicit iterator loop because C++11 doesn't support
        // structured bindings (which would be easier in C++17).
        for (typename std::list<std::pair<Key, Value>>::const_iterator it = items_.begin();
             it != items_.end();
             ++it)
        {
            // Print each key-value pair.
            std::cout << "(" << it->first << ": " << it->second << ") ";
        }

        std::cout << '\n';
    }

private:
    // ========================================================================
    // Private type definitions (convenience aliases)
    // ========================================================================
    // These make the code shorter by avoiding repetition of long template names.
    typedef std::list<std::pair<Key, Value>> ListType;
    typedef typename ListType::iterator ListIterator;

    // ========================================================================
    // Data members: The core data structures
    // ========================================================================

    // items_: A doubly-linked list storing (key, value) pairs.
    // Order: front = most recently used, back = least recently used.
    // Why a list? Because moving a node to the front is O(1) with splice().
    ListType items_;

    // index_: A hash map from key to iterator in the list.
    // Why a map? Because looking up a key is O(1) on average.
    // The iterator lets us jump directly to the list node for that key.
    std::unordered_map<Key, ListIterator> index_;

    // lock_: A spinlock protecting the above two data structures.
    // INVARIANT: Whenever we modify items_ or index_, the lock must be held.
    // This ensures threads don't see inconsistent state.
    mutable SpinLock lock_;

    // capacity_: The maximum number of items the cache can hold.
    // Set in constructor, never changes.
    std::size_t capacity_;

    // hits_: Counter for successful get() operations.
    // Incremented by get() when key is found.
    // Protected by lock_.
    std::size_t hits_ = 0;

    // misses_: Counter for failed get() operations.
    // Incremented by get() when key is not found.
    // Protected by lock_.
    std::size_t misses_ = 0;

public:
    // ========================================================================
    // Disable copy operations
    // ========================================================================
    // We delete copy constructor and assignment because:
    // 1. Sharing the same cache across objects would be confusing.
    // 2. Copying the lock is dangerous (two objects would share the lock).
    // 3. Deep copying the list and map would be expensive.
    // By deleting these, we force users to use references or pointers
    // instead of copying.
    LRUCache(const LRUCache &) = delete;
    LRUCache &operator=(const LRUCache &) = delete;
};

// ============================================================================
// DEMO: Walk through the cache behavior step-by-step
// ============================================================================
//
// This main() function demonstrates:
// 1. Creating a cache with capacity 3.
// 2. Inserting items.
// 3. Accessing an item (which updates recency).
// 4. Inserting when full (which triggers eviction).
// 5. Checking membership.
// 6. Viewing cache statistics.

int main()
{
    // ========================================================================
    // Step 1: Create a cache
    // ========================================================================
    // LRUCache<std::string, int> means:
    // - Keys are strings (like "A", "B", "C").
    // - Values are integers (like 1, 2, 3).
    // - Capacity is 3 (can hold at most 3 key-value pairs).
    LRUCache<std::string, int> cache(3);

    // ========================================================================
    // Step 2: Insert three items. At this point, cache is full.
    // ========================================================================
    // Each put() call:
    // - Acquires the lock
    // - Adds the item to the front of the list (most recently used)
    // - Adds the key to the map pointing to that list node
    // - Releases the lock

    cache.put("A", 1); // Cache now has: [A]
    cache.put("B", 2); // Cache now has: [B, A]
    cache.put("C", 3); // Cache now has: [C, B, A]

    // Print the cache order to verify. Front to back: C, B, A
    // C is MRU (just inserted), A is LRU (oldest).
    std::cout << "After inserting A, B, C:\n";
    cache.print_state();

    // ========================================================================
    // Step 3: Access an item (get). This does NOT change the value but DOES
    // change recency.
    // ========================================================================
    // When we access "A", the cache:
    // - Looks it up in the map
    // - Finds it in the list (currently at the back, least recently used)
    // - Moves it to the front (now it is most recently used)
    // - Returns its value (1)

    int value = 0;
    if (cache.get("A", value))
    {
        // get() returns true because "A" was found.
        std::cout << "\nGot A = " << value << '\n';
        std::cout << "A has been moved to MRU position.\n";
    }

    // ========================================================================
    // Step 4: Insert a new item when the cache is full.
    // ========================================================================
    // The cache has capacity 3 and currently holds [A, C, B] (A is MRU now).
    // When we insert "D":
    // 1. The cache is full.
    // 2. We identify B as the least recently used (at the back).
    // 3. We remove B from both the list and the map.
    // 4. We insert D at the front.
    // Result: [D, A, C] (B was evicted).

    cache.put("D", 4);

    std::cout << "\nAfter inserting D (cache is full, so B is evicted):\n";
    cache.print_state();

    // ========================================================================
    // Step 5: Check membership
    // ========================================================================
    // B was evicted, so contains("B") returns false.
    // A is still in the cache, so contains("A") returns true.

    std::cout << "\nChecking membership:\n";
    std::cout << "Contains B? " << (cache.contains("B") ? "yes" : "no") << '\n';
    std::cout << "  (B was evicted, so it's no longer in the cache)\n";

    std::cout << "Contains A? " << (cache.contains("A") ? "yes" : "no") << '\n';
    std::cout << "  (A is still in the cache)\n";

    // ========================================================================
    // Step 6: View cache statistics
    // ========================================================================
    // During this demo:
    // - We called get() once (for "A"), and it succeeded -> 1 hit
    // - We did not call get() with a missing key -> 0 misses
    // Hit rate = 1 / (1 + 0) = 100%

    std::cout << "\nCache statistics:\n";
    std::cout << "Hits: " << cache.hits() << " (successful get operations)\n";
    std::cout << "Misses: " << cache.misses() << " (failed get operations)\n";
    std::cout << "Hit rate: " << (cache.hits() * 100.0 / (cache.hits() + cache.misses() + 1)) << "%\n";
    std::cout << "Size: " << cache.size() << " / " << cache.capacity() << '\n';

    // ========================================================================
    // Summary of LRU behavior
    // ========================================================================
    std::cout << "\n=== LRU Cache Summary ===\n";
    std::cout << "The cache maintains a ranking by recency:\n";
    std::cout << "- When you GET an item, it moves to the front (most recent).\n";
    std::cout << "- When you PUT an item, it goes to the front (most recent).\n";
    std::cout << "- When the cache is full, the item at the back (least recent) is evicted.\n";
    std::cout << "- This ensures frequently-used items stay in the cache.\n";

    return 0;
}
