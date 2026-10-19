#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

struct Il2CppClass {};
namespace il2cpp {namespace vm {
enum class AssemblyShadowState { Disabled=0, Committed=6, Failed=9 };
struct ActiveSnapshot {uint64_t generation=1;};
inline ActiveSnapshot s_mainActive{4},s_altActive{5};
inline std::atomic<const ActiveSnapshot*> s_active{&s_mainActive};
inline std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
inline constexpr size_t kMaximumExecutionClasses=1024;
inline std::array<Il2CppClass*,kMaximumExecutionClasses*2> s_executionClasses{};
inline size_t s_executionClassCount=0;
inline std::function<void()> g_onClassSample;
inline std::function<void()> g_onFreeze;
inline int g_classSamples=0,g_lockDepth=0;
struct ExecutionObservationLock {
    ExecutionObservationLock() {++g_classSamples;++g_lockDepth;if(g_onClassSample)g_onClassSample();}
    ~ExecutionObservationLock() {--g_lockDepth;}
};
struct AssemblyShadowDiagnostics {
    static const char* StateName(AssemblyShadowState state) {
       return state==AssemblyShadowState::Committed?"Committed":state==AssemblyShadowState::Failed?"Failed":"Disabled";
    }
};
namespace assembly_shadow_diagnostic {
struct QuotedText {const char* data=nullptr;size_t bytes=0;};
inline QuotedText Quoted(const char* p) {return {p,std::strlen(p)};}
}
namespace assembly_shadow_r02 {
enum class Metric : uint8_t { MethodChecks, DroppedClasses, BaselineCctors, Count };
inline int g_level=2, g_captureCalls=0, g_failCaptureAt=0;
inline uint64_t g_dropped=0,g_methodChecks=8,g_droppedClasses=0,g_baselineCctors=1;
inline bool g_saturated=false;
inline std::function<void()> g_onCounterSample;
struct SnapshotReceipt {int error=0;int samples=0;bool observedReadFenceStable=false;};
class Snapshot {
  std::array<uint64_t,3> values_{};
  int level_=0;bool good_=false, saturated_=false;
  uint64_t dropped_=0;
public:
  static constexpr size_t kMetricCount=3;
  static bool TryCaptureCurrent(Snapshot& out,SnapshotReceipt& receipt) {
    ++g_captureCalls;receipt.samples=2;
    if(g_failCaptureAt==g_captureCalls){receipt.error=1;return false;}
    out.level_=g_level;
    out.dropped_=g_dropped;
    out.saturated_=g_saturated;
    out.values_={g_methodChecks,g_droppedClasses,g_baselineCctors};
    out.good_=true;receipt.observedReadFenceStable=true;
    if(g_onCounterSample)g_onCounterSample();
    return true;
  }
  bool Complete() const {return good_;}
  int DiagnosticsLevel() const {return level_;}
  size_t ThreadCapacity() const {return 64;}
  size_t MemoTlsBytesPerThread() const {return level_<2?0:128;}
  size_t RetainedBytes() const {return level_==0?0:123;}
  uint64_t DroppedThreads() const {return dropped_;}
  bool Saturated() const {return saturated_;}
  uint64_t Read(Metric v) const {return values_.at(static_cast<size_t>(v));}
};
}
namespace assembly_shadow_frozen_execution_record {
namespace r02=assembly_shadow_r02;
using assembly_shadow_diagnostic::QuotedText;
enum class RecordError :uint8_t {None,InvalidInput,Other};
struct Identity {int32_t stateCode;QuotedText state;uint64_t generation;};
class Record {
  bool complete_=false,failed_=false,started_=false;
public:
  bool failBegin=false,failFreeze=false;
  uint64_t recordedGeneration=0;std::string recordedState;
  bool Complete() const {return complete_ && !failed_;}
  RecordError FirstError() const {return failed_?RecordError::InvalidInput:RecordError::None;}
  bool RejectInvalidSource(){failed_=true;complete_=false;return false;}
  bool Begin(Identity id,const r02::Snapshot& snapshot,size_t rows) {
     if(g_lockDepth!=0 || failBegin || started_ || failed_ || !snapshot.Complete() || rows!=0)return false;
     started_=true;recordedGeneration=id.generation;
     recordedState.assign(id.state.data,id.state.bytes);return true;
  }
  bool Freeze() {
      if(g_onFreeze)g_onFreeze();
      if(g_lockDepth!=0 || !started_ || failFreeze || failed_)return false;
      complete_=true;return true;
  }
};
}
namespace assembly_shadow_metadata_lifetime {
inline bool g_admissionOpen=true;inline int g_readers=0;
struct Gate {
 struct Reader {bool good_;explicit Reader(Gate&) : good_(g_admissionOpen){if(good_)++g_readers;}
    ~Reader(){if(good_)--g_readers;}explicit operator bool()const{return good_;}};
};inline Gate g_MetadataLifetimeGate{};
}
class AssemblyShadow;
}}
