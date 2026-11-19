#pragma once
// R03 B2 D4: nonallocating transfer of a fully prepared private snapshot to
// process-lifetime ownership BEFORE publishing the pointer to native readers.
//
// The containing owner MUST have process lifetime; AssemblyShadow's Transaction
// is heap-allocated once and intentionally never deleted by Current(). No VM,
// GC, metadata, logging, lock acquisition or user callbacks are performed here.
// This helper does not make an already published snapshot unloadable/retryable.
#include <atomic>
#include <memory>
#include <utility>

namespace il2cpp { namespace vm { namespace assembly_shadow_publication {

template<class SnapshotT>
class ProcessSnapshotOwner final
{
public:
    ProcessSnapshotOwner() noexcept = default;
    ProcessSnapshotOwner(const ProcessSnapshotOwner&) = delete;
    ProcessSnapshotOwner& operator=(const ProcessSnapshotOwner&) = delete;

    // A single transaction writer calls this while holding the existing
    // transaction, metadata, assembly and usage locks. The public pointer is
    // untouched on rejection. There is NO throwing operation between taking
    // stable ownership and the release-store.
    bool TryPublish(std::unique_ptr<SnapshotT>& pending,
        std::atomic<const SnapshotT*>& published) noexcept
    {
        if (!pending || retained_ || published.load(std::memory_order_acquire))
            return false;
        retained_ = std::move(pending); // std::unique_ptr default deleter, noexcept
        published.store(retained_.get(), std::memory_order_release);
        return true;
    }

    const SnapshotT* get() const noexcept { return retained_.get(); }

private:
    std::unique_ptr<SnapshotT> retained_;
};

}}} // il2cpp::vm::assembly_shadow_publication
