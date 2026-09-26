#pragma once

// Process-lifetime, immutable entries. Readers take no mutex, allocate nothing,
// and never enter metadata resolution. Only the owning metadata slow path may
// build values. The table publishes completed values; it does not run builders
// or wait for another thread while holding a cache lock.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

struct AdmissionKey
{
    uint64_t generation;
    const void* physical;
    const void* context;
    uint32_t domain;

    bool operator==(const AdmissionKey& other) const
    {
        return generation == other.generation && physical == other.physical &&
            context == other.context && domain == other.domain;
    }

    size_t Hash() const
    {
        uint64_t h = generation ^ (static_cast<uint64_t>(domain) << 32);
        h ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(physical));
        h ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(context)) * UINT64_C(0x9e3779b97f4a7c15);
        h ^= h >> 30; h *= UINT64_C(0xbf58476d1ce4e5b9);
        h ^= h >> 27; h *= UINT64_C(0x94d049bb133111eb);
        h ^= h >> 31;
        return static_cast<size_t>(h);
    }
};

template<class Value, size_t BucketCount = 1024>
class AdmissionCache
{
    static_assert(BucketCount > 0, "cache requires buckets");
    struct Node
    {
        const AdmissionKey key;
        const Value value;
        Node* next;
        Node(const AdmissionKey& k, Value&& v) : key(k), value(std::move(v)), next(nullptr) {}
    };
    std::atomic<Node*> buckets_[BucketCount];

public:
    AdmissionCache()
    {
        for (size_t i = 0; i < BucketCount; ++i)
            buckets_[i].store(nullptr, std::memory_order_relaxed);
    }
    AdmissionCache(const AdmissionCache&) = delete;
    AdmissionCache& operator=(const AdmissionCache&) = delete;

    // For standalone tests / quiescent ownership only. The VM adapter retains
    // its cache for process lifetime and never destroys it under readers.
    ~AdmissionCache()
    {
        for (size_t i = 0; i < BucketCount; ++i)
        {
            Node* node = buckets_[i].load(std::memory_order_relaxed);
            while (node)
            {
                Node* next = node->next;
                delete node;
                node = next;
            }
        }
    }

    const Value* Find(const AdmissionKey& key) const noexcept
    {
        Node* node = buckets_[key.Hash() % BucketCount].load(std::memory_order_acquire);
        for (; node; node = node->next)
            if (node->key == key) return &node->value;
        return nullptr;
    }

    // Duplicate publication is harmless, but single construction is enforced
    // by the VM adapter's metadata lock + second Find, not by this table.
    // An allocation/copy exception occurs before the release publication.
    const Value* Publish(const AdmissionKey& key, Value value, bool& inserted)
    {
        inserted = false;
        std::unique_ptr<Node> fresh(new Node(key, std::move(value)));
        auto& bucket = buckets_[key.Hash() % BucketCount];
        Node* head = bucket.load(std::memory_order_acquire);
        for (;;)
        {
            for (Node* node = head; node; node = node->next)
                if (node->key == key) return &node->value;
            fresh->next = head;
            if (bucket.compare_exchange_weak(head, fresh.get(),
                std::memory_order_release, std::memory_order_acquire))
            {
                inserted = true;
                return &fresh.release()->value;
            }
        }
    }

    static size_t EntryBytes() noexcept { return sizeof(Node); }
    static size_t BucketBytes() noexcept { return sizeof(buckets_); }
};

}}}
