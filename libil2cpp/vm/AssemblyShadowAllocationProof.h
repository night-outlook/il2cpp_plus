#pragma once

// Engine-independent single-construction policy used by the VM adapter and
// standalone tests. The supplied guard is the VM metadata lock, never a cache
// mutex. Completed entries and their dependency arrays live for the process.
#include "AssemblyShadowAdmissionCache.h"
#include "AssemblyShadowObservationCounters.h"
#include <stdexcept>
#include <vector>

namespace il2cpp { namespace vm { namespace assembly_shadow_r02 {

template<class Class>
struct AllocationCertificate
{
    struct Dependency
    {
        Class* baseline;
        Class* active;
        bool structural;
    };
    Class* target = nullptr;
    bool complete = true;
    bool involvesShadow = false;
    std::vector<Dependency> dependencies;

    void Observe(Class* baseline, Class* active, bool structural)
    {
        involvesShadow = true;
        for (const auto& entry : dependencies)
            if (entry.baseline == baseline && entry.active == active &&
                entry.structural == structural) return;
        dependencies.push_back({baseline, active, structural});
    }
    size_t DynamicBytes() const noexcept
    {
        return dependencies.capacity() * sizeof(Dependency);
    }
};

template<class Class>
class AllocationProofTrace
{
    using Certificate = AllocationCertificate<Class>;
    Certificate* previous_;
public:
    static Certificate*& Current()
    {
        static thread_local Certificate* current = nullptr;
        return current;
    }
    explicit AllocationProofTrace(Certificate& value) : previous_(Current())
    {
        Current() = &value;
    }
    ~AllocationProofTrace() { Current() = previous_; }
    AllocationProofTrace(const AllocationProofTrace&) = delete;
    AllocationProofTrace& operator=(const AllocationProofTrace&) = delete;
};

template<class Class>
class AllocationProofCache
{
    using Certificate = AllocationCertificate<Class>;
    AdmissionCache<Certificate> cache_;
    struct BuildFrame
    {
        const AllocationProofCache* owner;
        AdmissionKey key;
        BuildFrame* previous;
    };
    static BuildFrame*& CurrentBuild()
    {
        static thread_local BuildFrame* current = nullptr;
        return current;
    }
    class BuildScope
    {
        BuildFrame frame_;
    public:
        BuildScope(const AllocationProofCache* owner, const AdmissionKey& key)
            : frame_{owner, key, CurrentBuild()}
        {
            for (BuildFrame* p = CurrentBuild(); p; p = p->previous)
                if (p->owner == owner && p->key == key)
                    throw std::logic_error("Reentrant allocation proof construction");
            CurrentBuild() = &frame_;
        }
        ~BuildScope() { CurrentBuild() = frame_.previous; }
    };

public:
    // validate(nullptr) checks entry-independent execution/context conditions.
    // validate(certificate) also rechecks mutable baseline-use/poison state.
    // Neither callback may be replaced by a cached boolean verdict.
    template<class Guard, class Mutex, class Builder, class Validator>
    Class* Resolve(const AdmissionKey& key, Mutex* metadataMutex,
        Builder build, Validator validate)
    {
        validate(nullptr);
        if (const Certificate* hit = cache_.Find(key))
        {
            validate(hit);
            ObservationCounters::Add(Metric::AdmissionHits, 1);
            return hit->target;
        }
        ObservationCounters::Add(Metric::AdmissionMisses, 1);
        Guard lock(metadataMutex);
        // A different thread may have completed this physical type while we
        // waited for metadata. Never build twice merely because both missed.
        validate(nullptr);
        if (const Certificate* hit = cache_.Find(key))
        {
            validate(hit);
            ObservationCounters::Add(Metric::AdmissionHits, 1);
            return hit->target;
        }
        BuildScope scope(this, key);
        ObservationCounters::Add(Metric::AdmissionBuilds, 1);
        Certificate value;
        try
        {
            AllocationProofTrace<Class> trace(value);
            value.target = build(value);
            if (!value.target) throw std::logic_error("Null allocation proof target");
            validate(&value);
        }
        catch (...)
        {
            ObservationCounters::Add(Metric::AdmissionRejects, 1);
            throw;
        }
        if (!value.complete)
        {
            // Preserve the uncached path's result when layout is not final;
            // absence of readiness is never a positive reusable certificate.
            ObservationCounters::Add(Metric::AdmissionUnready, 1);
            return value.target;
        }
        const size_t bytes = cache_.EntryBytes() + value.DynamicBytes();
        bool inserted = false;
        const Certificate* result = cache_.Publish(key, std::move(value), inserted);
        if (inserted)
        {
            ObservationCounters::Add(Metric::AdmissionEntries, 1);
            ObservationCounters::Add(Metric::AdmissionRetainedBytes, bytes);
        }
        return result->target;
    }

    const Certificate* Find(const AdmissionKey& key) const noexcept { return cache_.Find(key); }
    static size_t BucketBytes() noexcept { return AdmissionCache<Certificate>::BucketBytes(); }
};

}}}
