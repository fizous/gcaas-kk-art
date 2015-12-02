/*
 * ipc_mark_sweep.cc
 *
 *  Created on: Oct 5, 2015
 *      Author: hussein
 */
#include <string>
#include <cutils/ashmem.h>
#include "globals.h"
#include "mem_map.h"
#include "ipcfs/ipcfs.h"
#include "sticky_mark_sweep.h"
#include "base/bounded_fifo.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "thread_list.h"
#include "mirror/object-inl.h"
#include "gc/service/service_client.h"
#include "gc/space/dlmalloc_space-inl.h"
#include "gc/space/space-inl.h"
#include "gc/space/space.h"
#include "gc/space/large_object_space.h"
#include "gc/collector/ipc_mark_sweep.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc/accounting/space_bitmap.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache.h"

using ::art::mirror::Class;
using ::art::mirror::Object;
namespace art {

namespace gc {
static constexpr size_t kMinConcurrentRemainingBytes = 128 * KB;
namespace collector {


// Performance options.
constexpr bool kUseRecursiveMark = false;
constexpr bool kUseMarkStackPrefetch = true;
constexpr size_t kSweepArrayChunkFreeSize = 1024;

// Parallelism options.
constexpr bool kParallelCardScan = true;
constexpr bool kParallelRecursiveMark = true;
// Don't attempt to parallelize mark stack processing unless the mark stack is at least n
// elements. This is temporary until we reduce the overhead caused by allocating tasks, etc.. Not
// having this can add overhead in ProcessReferences since we may end up doing many calls of
// ProcessMarkStack with very small mark stacks.
constexpr size_t kMinimumParallelMarkStackSize = 128;
constexpr bool kParallelProcessMarkStack = true;

// Profiling and information flags.
constexpr bool kCountClassesMarked = false;
constexpr bool kProfileLargeObjects = false;
constexpr bool kMeasureOverhead = false;
constexpr bool kCountTasks = false;
constexpr bool kCountJavaLangRefs = false;

// Turn off kCheckLocks when profiling the GC since it slows the GC down by up to 40%.
constexpr bool kCheckLocks = kDebugLocking;


IPCHeap::IPCHeap(space::GCSrvSharableHeapData* heap_meta, Heap* heap) :
    ms_lock_("heap-ipc lock"),
    ms_cond_("heap-ipcs::cond_", ms_lock_),
    meta_(heap_meta),
    local_heap_(heap),
    collector_daemon_(NULL),
    ipc_flag_raised_(0),
    collector_entry_(0) {

  /* concurrent glags locks */
  SharedFutexData* _conc_futexAddress = &meta_->conc_lock_.futex_head_;
  SharedConditionVarData* _conc_condAddress = &meta_->conc_lock_.cond_var_;
  conc_req_cond_mu_ = new InterProcessMutex("GCConc Mutex", _conc_futexAddress);
  conc_req_cond_ = new InterProcessConditionVariable("GCConc CondVar",
      *conc_req_cond_mu_, _conc_condAddress);

  /* initialize gc complete locks */
  SharedFutexData* _complete_futexAddress = &meta_->gc_complete_lock_.futex_head_;
  SharedConditionVarData* _complete_condAddress = &meta_->gc_complete_lock_.cond_var_;
  gc_complete_mu_ = new InterProcessMutex("GCComplete Mutex", _complete_futexAddress);
  gc_complete_cond_ = new InterProcessConditionVariable("GCcomplete CondVar",
      *gc_complete_mu_, _complete_condAddress);

//  if(!StartCollectorDaemon()) {
//    IPC_MARKSWEEP_VLOG(ERROR) << "XXXXXXXXX IPCHeap::IPCHeap .. could not initialize collector"
//        << " daemon .. XXXXXXXXX";
//  }

  ResetHeapMetaDataUnlocked();

}



void IPCHeap::BlockForServerInitialization(volatile int32_t* addr_val) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    while(android_atomic_release_load(addr_val) != 2)
      conc_req_cond_->Wait(self);
    conc_req_cond_->Broadcast(self);
  }
}

void IPCHeap::SetCollectorDaemon(Thread* thread) {
  MutexLock mu(thread, ms_lock_);
  collector_daemon_ = thread;
  ms_cond_.Broadcast(thread);
}

bool IPCHeap::StartCollectorDaemon(void) {



  IPC_MARKSWEEP_VLOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon-----------";

  CHECK_PTHREAD_CALL(pthread_create,
      (&collector_pthread_, NULL,
      &IPCHeap::RunDaemon, this),
      "IPCHeap Daemon thread");

  Thread* self = Thread::Current();
  MutexLock mu(self, ms_lock_);
  IPC_MARKSWEEP_VLOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon going " <<
      "waits for daemon initialization";
  while (collector_daemon_ == NULL) {
    ms_cond_.Wait(self);
  }

  IPC_MARKSWEEP_VLOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon done " <<
      "with creating daemon ";

  //CreateCollectors();

  return true;
}

void IPCHeap::ResetHeapMetaDataUnlocked() { // reset data without locking
  //meta_data_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  meta_->freed_objects_   = 0;
  meta_->freed_bytes_     = 0;
  meta_->barrier_count_   = 0;
  meta_->conc_flag_       = 0;
//  meta_->is_gc_complete_  = 0;
  meta_->is_gc_running_   = 0;
  meta_->concurrent_gc_ = (local_heap_->concurrent_gc_) ? 1 : 0;;
  meta_->collect_index_ = -1;

  /* heap members */
  meta_->last_gc_type_ = collector::kGcTypeNone;
  meta_->next_gc_type_ = collector::kGcTypePartial;
  meta_->total_wait_time_ = 0;
  meta_->concurrent_start_bytes_ = local_heap_->GetConcStartBytes();
  meta_->last_gc_size_ = local_heap_->last_gc_size_;
  meta_->last_gc_time_ns_ = local_heap_->last_gc_time_ns_;



  /* heap statistics */
  meta_->total_objects_freed_ever_  = local_heap_->GetObjectsFreedEver();
  meta_->total_bytes_freed_ever_    = local_heap_->GetBytesFreedEver();
  meta_->conc_count_ = 0;
  meta_->explicit_count_ = 0;

  /* set the offsets */
  meta_->reference_offsets_.reference_referent_offset_ =
      local_heap_->reference_referent_offset_.Uint32Value();
  meta_->reference_offsets_.reference_queue_offset_ =
      local_heap_->reference_queue_offset_.Uint32Value();
  meta_->reference_offsets_.reference_queueNext_offset_ =
      local_heap_->reference_queueNext_offset_.Uint32Value();
  meta_->reference_offsets_.reference_pendingNext_offset_ =
      local_heap_->reference_pendingNext_offset_.Uint32Value();
  meta_->reference_offsets_.finalizer_reference_zombie_offset_ =
      local_heap_->finalizer_reference_zombie_offset_.Uint32Value();
}


void IPCHeap::AssignNextGCType(void) {
  meta_->next_gc_type_ = local_heap_->next_gc_type_;
}

void* IPCHeap::RunDaemon(void* arg) {
  IPC_MARKSWEEP_VLOG(ERROR) << "AbstractIPCMarkSweep::RunDaemon: begin" ;
  IPCHeap* _ipc_heap = reinterpret_cast<IPCHeap*>(arg);
  CHECK(_ipc_heap != NULL);

  Runtime* runtime = Runtime::Current();
  CHECK(runtime->AttachCurrentThread("IPC-MS-Daem", true, NULL, false));

  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    _ipc_heap->SetCollectorDaemon(self);
  }


  IPC_MARKSWEEP_VLOG(ERROR) << "AbstractIPCMarkSweep::RunDaemon: broadcast" ;
  bool collector_loop = true;
  while(collector_loop) {
    collector_loop = _ipc_heap->RunCollectorDaemon();
  }

  return NULL;
}


void IPCHeap::CreateCollectors(void) {
  bool _conc_flag = false;
  for(int i = 0 ; i < 2 ; i ++) {
    _conc_flag = (i != 0);
    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(new IPCMarkSweep(this, _conc_flag,
        "ipcMS")));
    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(new IPCPartialMarkSweep(this, _conc_flag,
        "partialIPC")));
    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(new IPCStickyMarkSweep(this, _conc_flag,
        "stickyIPC")));

  }
}



void IPCHeap::ConcurrentGC(Thread* self) {
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    if (Runtime::Current()->IsShuttingDown()) {
      return;
    }
  }
  if (WaitForConcurrentIPCGcToComplete(self) == collector::kGcTypeNone) {
    CollectGarbageIPC(meta_->next_gc_type_, kGcCauseBackground, false);
  }
//  local_heap_->ConcurrentGC(self);
//  {
//    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
//    if (Runtime::Current()->IsShuttingDown()) {
//      return;
//    }
//  }
//  if (WaitForConcurrentIPCGcToComplete(self) == collector::kGcTypeNone) {
//    CollectGarbageIPC(next_gc_type_, kGcCauseBackground, false);
//  }
}

void IPCHeap::ExplicitGC(bool clear_soft_references)  {
  Thread* self = Thread::Current();
  WaitForConcurrentIPCGcToComplete(self);
  CollectGarbageIPC(collector::kGcTypeFull, kGcCauseExplicit, clear_soft_references);
}

void IPCHeap::SetLastProcessID(void) { //here we set the process of the app
  int _lastProcessID = local_heap_->GetLastProcessStateID();
  meta_->process_state_ = _lastProcessID;
  IPC_MARKSWEEP_VLOG(ERROR) << "++++++ IPCHeap::SetLastProcessID: " << meta_->process_state_;
}


bool IPCHeap::CheckTrimming() {
  IPC_MARKSWEEP_VLOG(ERROR) << "bool IPCHeap::CheckTrimming()";
  uint64_t ms_time = MilliTime();
  float utilization =
      static_cast<float>(local_heap_->GetAllocSpace()->GetBytesAllocated()) / local_heap_->GetAllocSpace()->Size();
  if ((utilization > 0.75f && !local_heap_->IsLowMemoryMode()) ||
      ((ms_time - meta_->last_trim_time_ms_) < 2 * 1000)) {
    // Don't bother trimming the alloc space if it's more than 75% utilized and low memory mode is
    // not enabled, or if a heap trim occurred in the last two seconds.
    return false;
  }

  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    Runtime* runtime = Runtime::Current();
    if (runtime == NULL || !runtime->IsFinishedStarting() || runtime->IsShuttingDown()) {
      // Heap trimming isn't supported without a Java runtime or Daemons (such as at dex2oat time)
      // Also: we do not wish to start a heap trim if the runtime is shutting down (a racy check
      // as we don't hold the lock while requesting the trim).
      return false;
    }
  }

  SetLastProcessID();


  //todo : we will need to get rid of that. and use static initialization defined at the service
  meta_->last_trim_time_ms_ = ms_time;
  local_heap_->ListenForProcessStateChange();

  // Trim only if we do not currently care about pause times.
  if (!local_heap_->care_about_pause_times_) {
    #if (true || ART_GC_SERVICE)
      gcservice::GCServiceClient::RequestHeapTrim();
    #endif

//    JNIEnv* env = self->GetJniEnv();
//    DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
//    DCHECK(WellKnownClasses::java_lang_Daemons_requestHeapTrim != NULL);
//    env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
//                              WellKnownClasses::java_lang_Daemons_requestHeapTrim);
//    CHECK(!env->ExceptionCheck());
    IPC_MARKSWEEP_VLOG(ERROR) << "bool IPCHeap::Posted a Request()";
    return true;
  }
  return false;
}


void IPCHeap::TrimHeap(void)  {
  local_heap_->Trim();
}

collector::GcType IPCHeap::WaitForConcurrentIPCGcToComplete(Thread* self) {
  IPC_MARKSWEEP_VLOG(ERROR) << "*****Executing the WaitForConcurrentIPCGcToComplete**** " << self->GetTid();
  collector::GcType last_gc_type = collector::kGcTypeNone;
  bool do_wait = false;
  if(meta_->concurrent_gc_) { // the heap is concurrent
    uint64_t wait_start = NanoTime();
    {
      IPMutexLock interProcMu(self, *gc_complete_mu_);
      do_wait = meta_->is_gc_running_;
    }
    if(do_wait) {
      uint64_t wait_time;
      // We must wait, change thread state then sleep on gc_complete_cond_;
      ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGcToComplete);
      {
        IPMutexLock interProcMu(self, *gc_complete_mu_);
        while (meta_->is_gc_running_ == 1) {
          gc_complete_cond_->Wait(self);
        }
        last_gc_type = meta_->last_gc_type_;
        wait_time = NanoTime() - wait_start;
        meta_->total_wait_time_ += wait_time;
      }
      if (wait_time > local_heap_->long_pause_log_threshold_) {
        LOG(INFO) << "WaitForConcurrentIPCGcToComplete blocked for " << PrettyDuration(wait_time);
      }
    }
  }
  return last_gc_type;
}

const char* gc_cause_and_type_strings[4][4] = {
    {"", "GC Alloc Sticky", "GC Alloc Partial", "GC Alloc Full"},
    {"", "GC Background Sticky", "GC Background Partial", "GC Background Full"},
    {"", "GC Explicit Sticky", "GC Explicit Partial", "GC Explicit Full"},
    {"", "GC Profile Sticky", "GC Profile Partial", "GC Profile Full"}};
collector::GcType IPCHeap::CollectGarbageIPC(collector::GcType gc_type,
    GcCause gc_cause, bool clear_soft_references) {
  Thread* self = Thread::Current();

  ScopedThreadStateChange tsc(self, kWaitingPerformingGc);
  Locks::mutator_lock_->AssertNotHeld(self);

  if (self->IsHandlingStackOverflow()) {
    LOG(WARNING) << "Performing GC on a thread that is handling a stack overflow.";
  }

  // Ensure there is only one GC at a time.
  bool start_collect = false;
  while (!start_collect) {
    {
      IPMutexLock interProcMu(self, *gc_complete_mu_);
      if (meta_->is_gc_running_ == 0) {
        meta_->is_gc_running_ = 1;
        start_collect = true;
        curr_gc_cause_ = gc_cause;
        RaiseServerFlag();

      }
    }
    if (!start_collect) {
      // TODO: timinglog this.
      WaitForConcurrentIPCGcToComplete(self);

      // TODO: if another thread beat this one to do the GC, perhaps we should just return here?
      //       Not doing at the moment to ensure soft references are cleared.
    }
  }

  if (gc_cause == kGcCauseForAlloc && Runtime::Current()->HasStatsEnabled()) {
    ++Runtime::Current()->GetStats()->gc_for_alloc_count;
    ++Thread::Current()->GetStats()->gc_for_alloc_count;
  }

  uint64_t gc_start_time_ns = NanoTime();
  uint64_t gc_start_size = local_heap_->GetBytesAllocated();
  // Approximate allocation rate in bytes / second.
  if (UNLIKELY(gc_start_time_ns == meta_->last_gc_time_ns_)) {
    LOG(WARNING) << "Timers are broken (gc_start_time == last_gc_time_).";
  }
  uint64_t ms_delta = NsToMs(gc_start_time_ns - meta_->last_gc_time_ns_);
  if (ms_delta != 0) {
    meta_->allocation_rate_ = ((gc_start_size - meta_->last_gc_size_) * 1000) / ms_delta;
    VLOG(heap) << "Allocation rate: " << PrettySize(meta_->allocation_rate_) << "/s";
  }

  if (gc_type == collector::kGcTypeSticky &&
      local_heap_->GetAllocSpace()->Size() < local_heap_->GetMinAllocSpaceSizeForSticky()) {
    gc_type = collector::kGcTypePartial;
  }

  collector::MarkSweep* collector = NULL;

  for (const auto& cur_collector : local_heap_->mark_sweep_collectors_) {
    if (cur_collector->IsConcurrent() == meta_->concurrent_gc_ && cur_collector->GetGcType() == gc_type) {
      collector = cur_collector;
      IPC_MARKSWEEP_VLOG(ERROR) << "*** collector: " << collector->GetName();
      break;
    }
  }

  CHECK(collector != NULL)
      << "Could not find garbage collector with concurrent=" << meta_->concurrent_gc_
      << " and type=" << gc_type;

  collector->SetClearSoftReferences(clear_soft_references);
  IPC_MARKSWEEP_VLOG(ERROR) << "GCMMP collect -> " << gc_cause_and_type_strings[gc_cause][gc_type] << " from thread ID:" << self->GetTid();
  collector->Run();

  meta_->total_objects_freed_ever_  += collector->GetFreedObjects();
  meta_->total_bytes_freed_ever_    += collector->GetFreedBytes();


  {
      IPMutexLock interProcMu(self, *gc_complete_mu_);
      meta_->is_gc_running_ = 0;
      meta_->last_gc_type_ = gc_type;
      ResetServerFlag();
      // Wake anyone who may have been waiting for the GC to complete.
      gc_complete_cond_->Broadcast(self);
  }

  return gc_type;
}


void IPCHeap::RaiseServerFlag(void) {
  if (!(curr_gc_cause_ == kGcCauseBackground || curr_gc_cause_ == kGcCauseExplicit)) { //a mutator is performing an allocation. do not involve service to get things done faster
    return;
  }

  int _expected_flag_value, _new_raised_flag;
  do {
    _expected_flag_value = 0;
    _new_raised_flag = 1;

  } while  (android_atomic_cas(_expected_flag_value, _new_raised_flag, &ipc_flag_raised_) != 0);

}

void IPCHeap::SetCurrentCollector(IPCMarkSweep* collector) {
  android_atomic_acquire_store(ipc_flag_raised_, &(collector->server_synchronize_));
  Thread* self = Thread::Current();
  int _synchronized = 0;
  if((_synchronized = android_atomic_release_load(&(collector->server_synchronize_))) > 0) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *conc_req_cond_mu_);
      meta_->conc_flag_ = 2;
      meta_->collect_index_ = collector->collector_index_;
      meta_->current_collector_ = collector->meta_data_;
      IPC_MARKSWEEP_VLOG(ERROR) << "\t client: Setting current collector as follows: " <<
          "index = " << meta_->collect_index_ <<
          "\n\t    address = " << reinterpret_cast<void*>(meta_->current_collector_);
      conc_req_cond_->Broadcast(self);
    }
  }

}


void IPCHeap::ResetCurrentCollector(IPCMarkSweep* collector) {
  Thread* self = Thread::Current();
  int _value_stored = 0;
  while((_value_stored = android_atomic_release_load(&(collector->server_synchronize_))) > 0) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *conc_req_cond_mu_);
      while(meta_->conc_flag_ < 3) {
        conc_req_cond_->Wait(self);
      }
      meta_->conc_flag_ = 4;
      while(android_atomic_cas(_value_stored, 0, &collector->server_synchronize_) != 0);
      meta_->collect_index_ = -1;
      meta_->current_collector_ = NULL;
      IPC_MARKSWEEP_VLOG(ERROR) << "\t client: Client notified completion";
      conc_req_cond_->Broadcast(self);
    }
  }

}


void IPCHeap::ResetServerFlag(void) {
  if (!(curr_gc_cause_ == kGcCauseBackground || curr_gc_cause_ == kGcCauseExplicit))  { //a mutator is performing an allocation. do not involve service to get things done faster
    return;
  }

  int _expected_flag_value, _new_raised_flag;
  do {
    _expected_flag_value = 1;
    _new_raised_flag = 0;

  } while  (android_atomic_cas(_expected_flag_value, _new_raised_flag, &ipc_flag_raised_) != 0);

}

void IPCHeap::NotifyCompleteConcurrentTask(void) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    meta_->conc_flag_ = 5;
    conc_req_cond_->Broadcast(self);
  }
}

bool IPCHeap::RunCollectorDaemon() {
  Thread* self = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCHeap::WaitForRequest.." << self->GetTid();

  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    IPC_MARKSWEEP_VLOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- before while: conc flag = " << meta_->conc_flag_;
    while(meta_->conc_flag_ != 1) {
      conc_req_cond_->Wait(self);
    }
    IPC_MARKSWEEP_VLOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- leaving wait: conc flag = " << meta_->conc_flag_
        << ", gctype = " << meta_->gc_type_;

  }
  //Runtime* runtime = Runtime::Current();

//  ScopedThreadStateChange tscConcA(self, kWaitingForGCProcess);
//  {
//    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
//    meta_->is_gc_running_ = 1;
//    //meta_->conc_flag_ = 0;
//   // meta_->is_gc_complete_ = 0;
//    conc_req_cond_->Broadcast(self);
//  }
  IPC_MARKSWEEP_VLOG(ERROR) << ">>>>>>>>>IPCHeap::ConcurrentGC...Starting: " << self->GetTid() << " <<<<<<<<<<<<<<<";
  if(meta_->gc_type_ == 1) {
    ConcurrentGC(self);
    meta_->conc_count_ = meta_->conc_count_ + 1;
  } else if(meta_->gc_type_ == 2) {
    ExplicitGC(false);
    meta_->explicit_count_ = meta_->explicit_count_ + 1;
  }

  IPC_MARKSWEEP_VLOG(ERROR) << "<<<<<<<<<IPCHeap::ConcurrentGC...Done: " << self->GetTid() <<
      " >>>>>>>>>>>>>>> conc_count=" << meta_->conc_count_
      <<"; explicit_count:" << meta_->explicit_count_;
  NotifyCompleteConcurrentTask();
//  ScopedThreadStateChange tscConcB(self, kWaitingForGCProcess);
//  {
//    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
////    meta_->is_gc_complete_ = 1;
//    meta_->conc_flag_ = 2;
//    conc_req_cond_->Broadcast(self);
//  }
//  {
//    ScopedThreadStateChange tscConcB(self, kWaitingForGCProcess);
//    {
//      IPMutexLock interProcMu(self, *conc_req_cond_mu_);
//      while(meta_->is_gc_complete_ == 1) {
//        IPC_MARKSWEEP_VLOG(ERROR) << "      IPCHeap::RunCollectorDaemon: waiting for gc_complete reset";
//        conc_req_cond_->Wait(self);
//      }
//      conc_req_cond_->Broadcast(self);
//      IPC_MARKSWEEP_VLOG(ERROR) << "      IPCHeap::RunCollectorDaemon: leave waiting for gc_complete reset";
//    }
//  }
  return true;
}



void IPCHeap::GrowForUtilization(collector::GcType gc_type, uint64_t gc_duration) {
  size_t bytes_allocated = meta_->last_gc_size_;
  size_t target_size;
  if (gc_type != collector::kGcTypeSticky) {
    // Grow the heap for non sticky GC.
    target_size = bytes_allocated / local_heap_->GetTargetHeapUtilization();
    if (target_size > bytes_allocated + local_heap_->max_free_) {
      target_size = bytes_allocated + local_heap_->max_free_;
    } else if (target_size < bytes_allocated + local_heap_->min_free_) {
      target_size = bytes_allocated + local_heap_->min_free_;
    }
    meta_->next_gc_type_ = collector::kGcTypeSticky;
  } else {
    // Based on how close the current heap size is to the target size, decide
    // whether or not to do a partial or sticky GC next.
    if (bytes_allocated + local_heap_->min_free_ <= local_heap_->max_allowed_footprint_) {
      local_heap_->SetNextGCType(collector::kGcTypeSticky);
    } else {
      local_heap_->SetNextGCType(collector::kGcTypePartial);
    }

    // If we have freed enough memory, shrink the heap back down.
    if (bytes_allocated + local_heap_->max_free_ < local_heap_->max_allowed_footprint_) {
      target_size = bytes_allocated + local_heap_->max_free_;
    } else {
      target_size = std::max(bytes_allocated, local_heap_->max_allowed_footprint_);
    }
  }

  if (!local_heap_->ignore_max_footprint_) {
    local_heap_->SetIdealFootprint(target_size);

    if (meta_->concurrent_gc_) {
      // Calculate when to perform the next ConcurrentGC.

      // Calculate the estimated GC duration.
      double gc_duration_seconds = NsToMs(gc_duration) / 1000.0;
      // Estimate how many remaining bytes we will have when we need to start the next GC.
      size_t remaining_bytes = meta_->allocation_rate_ * gc_duration_seconds;
      remaining_bytes = std::max(remaining_bytes, kMinConcurrentRemainingBytes);
      if (UNLIKELY(remaining_bytes > local_heap_->max_allowed_footprint_)) {
        // A never going to happen situation that from the estimated allocation rate we will exceed
        // the applications entire footprint with the given estimated allocation rate. Schedule
        // another GC straight away.
        meta_->concurrent_start_bytes_ = bytes_allocated;
      } else {
        // Start a concurrent GC when we get close to the estimated remaining bytes. When the
        // allocation rate is very high, remaining_bytes could tell us that we should start a GC
        // right away.
        meta_->concurrent_start_bytes_ = std::max(local_heap_->max_allowed_footprint_ - remaining_bytes, bytes_allocated);
      }
//      DCHECK_LE(meta_->concurrent_start_bytes_, max_allowed_footprint_);
//      DCHECK_LE(max_allowed_footprint_, growth_limit_);
    }
  }

  local_heap_->UpdateMaxNativeFootprint();
}

AbstractIPCMarkSweep::AbstractIPCMarkSweep(IPCHeap* ipcHeap, bool concurrent):
    ipc_heap_(ipcHeap),
    collector_index_(ipcHeap->collector_entry_++),
    server_synchronize_(0),
    heap_meta_(ipcHeap->meta_),
    meta_data_(&(heap_meta_->collectors_[collector_index_])) {

  /* initialize locks */
  SharedFutexData* _futexAddress = &heap_meta_->phase_lock_.futex_head_;
  SharedConditionVarData* _condAddress = &heap_meta_->phase_lock_.cond_var_;

  phase_mu_   = new InterProcessMutex("HandShake Mutex", _futexAddress);
  phase_cond_ = new InterProcessConditionVariable("HandShake CondVar",
      *phase_mu_, _condAddress);

  SharedFutexData* _futexBarrierAdd =
      &heap_meta_->gc_barrier_lock_.futex_head_;
  SharedConditionVarData* _condBarrierAdd =
      &heap_meta_->gc_barrier_lock_.cond_var_;


  barrier_mu_   = new InterProcessMutex("HandShake Mutex", _futexBarrierAdd);
  barrier_cond_ = new InterProcessConditionVariable("HandShake CondVar",
      *barrier_mu_, _condBarrierAdd);


  meta_data_->is_concurrent_ = concurrent ? 1 : 0;
  IPC_MARKSWEEP_VLOG(ERROR) << "############ Initializing IPC: " << collector_index_;
  ResetMetaDataUnlocked();

  DumpValues();
}




void AbstractIPCMarkSweep::ResetMetaDataUnlocked() { // reset data without locking
 // heap_meta_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  heap_meta_->freed_objects_ = 0;
  heap_meta_->freed_bytes_ = 0;
  heap_meta_->barrier_count_ = 0;
  heap_meta_->conc_flag_ = 0;
//  heap_meta_->is_gc_complete_ = 0;
  heap_meta_->is_gc_running_ = 0;



  /////////
  meta_data_->cashed_references_.immune_begin_ = nullptr;
  meta_data_->cashed_references_.immune_end_ = nullptr;
  meta_data_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  meta_data_->current_mark_bitmap_ = NULL;
}

void AbstractIPCMarkSweep::DumpValues(void){
  IPC_MARKSWEEP_VLOG(ERROR) << "Dump AbstractIPCMarkSweep: " << "zygote_begin: "
      << reinterpret_cast<void*>(heap_meta_->zygote_begin_)
      << "\n     zygote_end: " << reinterpret_cast<void*>(heap_meta_->zygote_end_)
      << "\n     image_begin: " << reinterpret_cast<void*>(heap_meta_->image_space_begin_)
      << "\n     image_end: " << reinterpret_cast<void*>(heap_meta_->image_space_end_)
  << "\n     alloc_begin: " << reinterpret_cast<void*>(ipc_heap_->local_heap_->GetAllocSpace()->Begin())
  << "\n     alloc_end: " << reinterpret_cast<void*>(ipc_heap_->local_heap_->GetAllocSpace()->End())
  << "\n     image_heap_begin: " << reinterpret_cast<void*>(ipc_heap_->local_heap_->GetImageSpace()->Begin())
  << "\n     image_heap_end: " << reinterpret_cast<void*>(ipc_heap_->local_heap_->GetImageSpace()->End());
}


accounting::SPACE_BITMAP* AbstractIPCMarkSweep::SetMarkBitmap(void) {
  accounting::SPACE_BITMAP* _bitmap =
      ipc_heap_->local_heap_->GetAllocSpace()->GetMarkBitmap();

  return _bitmap;
}

//void AbstractIPCMarkSweep::HandshakeMarkingPhase(void) {
//  if(true)
//    return;
//  Thread* currThread = Thread::Current();
//  {
//    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_MARK_REACHABLES, currThread);
//    phase_cond_->Broadcast(currThread);
//  }
//
//  if(ipc_heap_->ipc_flag_raised_ == 1) {
//    IPC_MARKSWEEP_VLOG(ERROR) << "the client changes phase from: : " << heap_meta_->gc_phase_;
//    GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_PRE_CONC_ROOT_MARK, currThread);
//    heap_meta_->gc_phase_ = space::IPC_GC_PHASE_CONC_MARK;
//    IPC_MARKSWEEP_VLOG(ERROR) << "      to : " << heap_meta_->gc_phase_;
//    ipc_heap_->ipc_flag_raised_ = 0;
//    phase_cond_->Broadcast(currThread);
//  } else {
//    IPC_MARKSWEEP_VLOG(ERROR) << "ipc_heap_->ipc_flag_raised_ was zero";
//    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_PRE_CONC_ROOT_MARK, currThread);
//    phase_cond_->Broadcast(currThread);
//  }
//}


void AbstractIPCMarkSweep::UpdateGCPhase(Thread* thread,
    space::IPC_GC_PHASE_ENUM phase) {
  ScopedThreadStateChange tsc(thread, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(thread, *phase_mu_);
    meta_data_->gc_phase_ = phase;
    phase_cond_->Broadcast(thread);
  }

}


void AbstractIPCMarkSweep::BlockForGCPhase(Thread* thread,
    space::IPC_GC_PHASE_ENUM phase) {
  ScopedThreadStateChange tsc(thread, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(thread, *phase_mu_);
    while( meta_data_->gc_phase_ != phase) {
      phase_cond_->Wait(thread);
    }
  }
}


class ClientMarkObjectVisitor {
 public:
  explicit ClientMarkObjectVisitor(IPCMarkSweep* const client_mark_sweep)
          ALWAYS_INLINE : mark_sweep_(client_mark_sweep) {}

  // TODO: Fixme when anotatalysis works with visitors.
  void operator()(const Object* /* obj */, const Object* ref, MemberOffset& /* offset */,
                  bool /* is_static */) const ALWAYS_INLINE
      NO_THREAD_SAFETY_ANALYSIS {
//    if (kCheckLocks) {
//      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
//      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
//    }
    if(false)
      mark_sweep_->MarkObject(ref);
  }

 private:
  IPCMarkSweep* const mark_sweep_;
};

template <typename MarkVisitor>
inline void IPCMarkSweep::ClientScanObjectVisit(const mirror::Object* obj,
    const MarkVisitor& visitor) {

  ipc_heap_->local_heap_->VerifyObjectImpl(obj);

  if(obj == NULL) {
    LOG(FATAL) << "XX ELEment cannot be null here IPCMarkSweep::ClientScanObjectVisit";
  }

  bool found = false;
  const byte* casted_obj = reinterpret_cast<const byte*>(obj);
  if((casted_obj >= heap_meta_->image_space_begin_ &&
      casted_obj < heap_meta_->image_space_end_))
    found = true;
  if((casted_obj >= heap_meta_->zygote_begin_ &&
      casted_obj < heap_meta_->zygote_end_))
    found = true;
  if((casted_obj >= ipc_heap_->local_heap_->GetAllocSpace()->Begin() &&
      casted_obj < ipc_heap_->local_heap_->GetAllocSpace()->End())) {
    found = true;
  }
  if(!found)
    LOG(FATAL) << "A- IPCMarkSweep::ServerScanObjectVisit...error." << obj;


  mirror::Class* klass = obj->GetClass();
  if (klass->IsObjectArrayClass()) {
    const mirror::ObjectArray<mirror::Object>* array =
        obj->AsObjectArray<mirror::Object>();
    const size_t length = static_cast<size_t>(array->GetLength());
    for (size_t i = 0; i < length; ++i) {
      const mirror::Object* element = array->GetWithoutChecksNoLocks(static_cast<int32_t>(i));
      const size_t width = sizeof(mirror::Object*);
      MemberOffset offset(i * width + mirror::Array::DataOffset(width).Int32Value());
      if(element == NULL)
        continue;
      if(false)
        visitor(array, element, offset, false);
      casted_obj = reinterpret_cast<const byte*>(element);
      if((casted_obj >= heap_meta_->image_space_begin_ &&
          casted_obj < heap_meta_->image_space_end_))
        continue;
      if((casted_obj >= heap_meta_->zygote_begin_ &&
          casted_obj < heap_meta_->zygote_end_))
        continue;
      if((casted_obj >= ipc_heap_->local_heap_->GetAllocSpace()->Begin() &&
          casted_obj < ipc_heap_->local_heap_->GetAllocSpace()->End())) {
        continue;
      }
      LOG(FATAL) << "B- IPCMarkSweep::ServerScanObjectVisit...error. array = " <<
          array << "object = " << element << ", index = " << i;
    }

  }



}

void IPCMarkSweep::ClientVerifyObject(const mirror::Object* obj) {
  //obj = (obj + calculated_offset);

  ClientMarkObjectVisitor visitor(this);
  ClientScanObjectVisit(obj, visitor);
 // mirror::Object* mapped_obj = MapClientReference(obj);

}




inline void IPCMarkSweep::ScanObjectVisitVerifyArray(const mirror::Object* obj) {
  DCHECK(obj != NULL);
  if (kIsDebugBuild && !IsMarked(obj)) {
    heap_->DumpSpaces();
    LOG(FATAL) << "Scanning unmarked object " << obj;
  }
  mirror::Class* klass = obj->GetClass();
  DCHECK(klass != NULL);
  if (UNLIKELY(klass->IsArrayClass())) {
    if (kCountScannedTypes) {
      ++array_count_;
    }
    if (klass->IsObjectArrayClass()) {
      const mirror::ObjectArray<mirror::Object>* _arr =
          obj->AsObjectArray<mirror::Object>();
      size_t length = static_cast<size_t>(_arr->GetLength());
      for (size_t i = 0; i < length; ++i) {
        LOG(ERROR) << "client; arr; " << obj << "; length; " << length <<
            "; index; " << i << "; elem; " << _arr->GetWithoutChecks(static_cast<int32_t>(i));;
      }


    }
  }
}

static void IPCSweepExternalScanObjectVisit(mirror::Object* obj,
    void* args) {
  IPCMarkSweep* param =
      reinterpret_cast<IPCMarkSweep*>(args);
  //uint32_t calc_offset = (param->offset_ / sizeof(Object*));
//  uint32_t* calc_offset = reinterpret_cast<uint32_t*>(calculated_offset);


  param->ScanObjectVisitVerifyArray(obj);
  //param->ClientVerifyObject(obj);
}

IPCMarkSweep::IPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap, is_concurrent),
    MarkSweep(ipcHeap->local_heap_, is_concurrent,
        &meta_data_->cashed_references_,
        name_prefix + (name_prefix.empty() ? "" : " ") + "ipcMS") {
  IPC_MARKSWEEP_VLOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: "
      << GetGcType() << "; conc:" << IsConcurrent() <<" ###########";
}



void IPCMarkSweep::ClearMarkHolders(void) {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep::ClearMarkHolders..............";
  // Clear all of the spaces' mark bitmaps.
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->GetGcRetentionPolicy() != space::kGcRetentionPolicyNeverCollect) {
      space->GetMarkBitmap()->Clear();
    }
  }
  mark_stack_->Reset();
}



void IPCMarkSweep::PreInitializePhase(void) {
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_PRE_INIT);
  IPC_MARKSWEEP_VLOG(ERROR) << "__________ IPCMarkSweep::PreInitializePhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  ipc_heap_->SetCurrentCollector(this);

}


void IPCMarkSweep::InitializePhase(void) {
  timings_.Reset();
  base::TimingLogger::ScopedSplit split("InitializePhase", &timings_);
  PreInitializePhase();

  art::Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_INIT);
  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::InitializePhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;

  mark_stack_ = ipc_heap_->local_heap_->GetHeapMarkStack();
  SetImmuneRange(nullptr, nullptr);
  SetSoftReferenceList(nullptr);
  SetWeakReferenceList(nullptr);
  SetFinalizerReferenceList(nullptr);
  SetPhantomReferenceList(nullptr);
  SetClearedReferenceList(nullptr);
  SetCachedJavaLangClass(Class::GetJavaLangClass());
  freed_bytes_ = 0;
  freed_large_object_bytes_ = 0;
  freed_objects_ = 0;
  freed_large_objects_ = 0;
  class_count_ = 0;
  array_count_ = 0;
  other_count_ = 0;
  large_object_test_ = 0;
  large_object_mark_ = 0;
  classes_marked_ = 0;
  overhead_time_ = 0;
  work_chunks_created_ = 0;
  work_chunks_deleted_ = 0;
  reference_count_ = 0;

  FindDefaultMarkBitmap();
  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::InitializePhase. going for GCVerification: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  if(false)
    ipc_heap_->local_heap_->PreGcVerification(this);
}

void IPCMarkSweep::ApplyTrimming(void) {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep::ApplyTrimming";
  ipc_heap_->CheckTrimming();
}

void IPCMarkSweep::FinishPhase(void) {
 Thread* currThread = Thread::Current();
 UpdateGCPhase(currThread, space::IPC_GC_PHASE_FINISH);
 IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::FinishPhase. starting: _______ " <<
     currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
 MarkSweep::FinishPhase();
 ipc_heap_->ResetCurrentCollector(this);
 //ipc_heap_->AssignNextGCType();
}

void IPCMarkSweep::FindDefaultMarkBitmap(void) {
  current_mark_bitmap_ = SetMarkBitmap();
  meta_data_->current_mark_bitmap_ =
      (reinterpret_cast<accounting::SharedSpaceBitmap*>(current_mark_bitmap_))->bitmap_data_;
}

void IPCMarkSweep::SetImmuneRange(mirror::Object* begin, mirror::Object* end) {
  meta_data_->cashed_references_.immune_begin_ = begin;
  meta_data_->cashed_references_.immune_end_ = end;
}


void IPCMarkSweep::MarkConcurrentRoots() {
  timings_.StartSplit("MarkConcurrentRoots");
  // Visit all runtime roots and clear dirty flags.
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_ROOT_CONC_MARK);
  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::MarkConcurrentRoots. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  Runtime::Current()->VisitConcurrentRoots(MarkObjectCallback, this, false, true);
  timings_.EndSplit();
}


/*
 * here we can handshake with the  GC service to request recursive marking of
 * reachable objects.
 * the srvice needs to:
 *   1- get the bitmap,
 *   2- scan the objects in the allocation space..we need to push any objects
 *      allocated in immuned space to the mark stack so that the agent do it on our
 *      behalf.
 *   3- loop on mark stack to rescan objects in the allocation space;
 *   4-
 */
void IPCMarkSweep::PostMarkingPhase(void){
//  Thread* currThread = Thread::Current();
//  ThreadList* thread_list = Runtime::Current()->GetThreadList();
//  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep::PostMarkingPhase: SSSSSSSSSSSSSSSSSSUspended the "
//      "threads: " << currThread->GetTid();
//  if(false) {
//    thread_list->SuspendAll();
//    IPC_MARKSWEEP_VLOG(ERROR) << "SSSSSSSSSSSSSSSSSSUspended the threads";
//    thread_list->ResumeAll();
//  }
//  {
//    ReaderMutexLock mu_mutator(currThread, *Locks::mutator_lock_);
//    WriterMutexLock mu_heap_bitmap(currThread, *Locks::heap_bitmap_lock_);
//    MarkReachableObjects();
//  }

}

void IPCMarkSweep::IPCMarkRootsPhase(void) {
  base::TimingLogger::ScopedSplit split("MarkingPhase", &timings_);
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_ROOT_MARK);
  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::MarkingPhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;

  BindBitmaps();
  FindDefaultMarkBitmap();

  // Process dirty cards and add dirty cards to mod union tables.
  ipc_heap_->local_heap_->ProcessCards(timings_);

  // Need to do this before the checkpoint since we don't want any threads to add references to
  // the live stack during the recursive mark.
  timings_.NewSplit("SwapStacks");
  //Fizo: here we can make the server gets which one is the right stack
  ipc_heap_->local_heap_->SwapStacks();

  WriterMutexLock mu(currThread, *Locks::heap_bitmap_lock_);
  if (Locks::mutator_lock_->IsExclusiveHeld(currThread)) {
    // If we exclusively hold the mutator lock, all threads must be suspended.
    MarkRoots();
    IPC_MARKSWEEP_VLOG(ERROR) << " ##### IPCMarkSweep::MarkingPhase. non concurrent marking: _______ " <<
        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  } else { //concurrent
    IPC_MARKSWEEP_VLOG(ERROR) << " ##### IPCMarkSweep::MarkingPhase.  concurrent marking: _______ " <<
        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
    MarkThreadRoots(currThread);
    // At this point the live stack should no longer have any mutators which push into it.
    MarkNonThreadRoots();
  }
  live_stack_freeze_size_ = ipc_heap_->local_heap_->GetLiveStack()->Size();
  MarkConcurrentRoots();

  ipc_heap_->local_heap_->UpdateAndMarkModUnion(this, timings_, GetGcType());

  MarkReachableObjects();
  //MarkReachableObjects();
}

void IPCMarkSweep::IPCMarkReachablePhase(void) {

}
void IPCMarkSweep::MarkingPhase(void) {
  IPCMarkRootsPhase();

}

void IPCMarkSweep::RequestAppSuspension(void) {
  //ThreadList* thread_list = Runtime::Current()->GetThreadList();
  Thread* currThread = Thread::Current();
  //thread_list->SuspendAll();
  //IPC_MARKSWEEP_VLOG(ERROR) << "SSS Suspended app threads to handshake with service process SS ";


  BlockForGCPhase(currThread, space::IPC_GC_PHASE_MARK_RECURSIVE);

  //IPC_MARKSWEEP_VLOG(ERROR) << "SSS Suspended app threads to handshake with service process SS ";
  //mark_stack_->OperateOnStack(IPCSweepExternalScanObjectVisit, this);
  //thread_list->ResumeAll();
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep client changes phase from: " << meta_data_->gc_phase_ <<
      ", stack_size = " << mark_stack_->Size();
  if(true)
    mark_stack_->OperateOnStack(IPCSweepExternalScanObjectVisit, this);
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_CONC_MARK);

}

void IPCMarkSweep::HandshakeIPCSweepMarkingPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << " #### IPCMarkSweep::HandshakeMarkingPhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  //ipc_heap_->local_heap_->DumpSpaces();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_MARK_REACHABLES);
  int _synchronized = 0;
  if((_synchronized = android_atomic_release_load(&(server_synchronize_))) == 1) {
    RequestAppSuspension();
  } else {
    IPC_MARKSWEEP_VLOG(ERROR) << " #### IPCMarkSweep:: ipc_heap_->ipc_flag_raised_ was zero";
    UpdateGCPhase(currThread, space::IPC_GC_PHASE_CONC_MARK);
  }

  IPC_MARKSWEEP_VLOG(ERROR) << "      to : " << meta_data_->gc_phase_;
}

//void IPCMarkSweep::ProcessMarkStack(bool paused) {
//  Thread* currThread = Thread::Current();
//  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::ProcessMarkStack. starting: _______ " <<
//      currThread->GetTid() << "... MarkStackSize=" << mark_stack_->Size();
//  MarkSweep::ProcessMarkStack(paused);
//}

// Scan anything that's on the mark stack.
void IPCMarkSweep::ProcessMarkStack(bool paused) {
  timings_.StartSplit("ProcessMarkStack");
//  Thread* currThread = Thread::Current();
//  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::ProcessMarkStack. starting: _______ " <<
//      currThread->GetTid() << "... MarkStackSize=" << mark_stack_->Size();
//  timings_.StartSplit("ProcessMarkStack");
  size_t thread_count = GetThreadCount(paused);
  if (kParallelProcessMarkStack && thread_count > 1 &&
      mark_stack_->Size() >= kMinimumParallelMarkStackSize) {
    ProcessMarkStackParallel(thread_count);
  } else {
    // TODO: Tune this.
    static const size_t kFifoSize = 4;
    BoundedFifoPowerOfTwo<const Object*, kFifoSize> prefetch_fifo;
    for (;;) {
      const Object* obj = NULL;
      if (kUseMarkStackPrefetch) {
        while (!mark_stack_->IsEmpty() && prefetch_fifo.size() < kFifoSize) {
          const Object* obj = mark_stack_->PopBack();
          DCHECK(obj != NULL);
          __builtin_prefetch(obj);
          prefetch_fifo.push_back(obj);
        }
        if (prefetch_fifo.empty()) {
          break;
        }
        obj = prefetch_fifo.front();
        prefetch_fifo.pop_front();
      } else {
        if (mark_stack_->IsEmpty()) {
          break;
        }
        obj = mark_stack_->PopBack();
      }
      DCHECK(obj != NULL);
      ScanObject(obj);
    }
  }
  timings_.EndSplit();
}


void IPCMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "_______IPCMarkSweep::MarkReachableObjects. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_ <<
      "... MarkStackSize=" << mark_stack_->Size();
//  UpdateGCPhase(currThread, space::IPC_GC_PHASE_MARK_REACHABLES);


  // Mark everything allocated since the last as GC live so that we can sweep concurrently,
  // knowing that new allocations won't be marked as live.
  timings_.StartSplit("MarkStackAsLive");
  accounting::ATOMIC_OBJ_STACK_T* live_stack = heap_->GetLiveStack();
  space::LargeObjectSpace* _LOS = heap_->GetLargeObjectsSpace();
  heap_->MarkAllocStack(heap_->GetAllocSpace()->GetLiveBitmap(),
      _LOS == NULL? NULL : _LOS->GetLiveObjects(), live_stack);
  live_stack->Reset();
  timings_.EndSplit();
  HandshakeIPCSweepMarkingPhase();
  // Recursively mark all the non-image bits set in the mark bitmap.
  RecursiveMark();

  //MarkSweep::MarkReachableObjects();
  IPC_MARKSWEEP_VLOG(ERROR) << " >>IPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

// Populates the mark stack based on the set of marked objects and
// recursively marks until the mark stack is emptied.
void IPCMarkSweep::RecursiveMark() {
  //base::TimingLogger::ScopedSplit split("RecursiveMark", &timings_);
  //ProcessMarkStack(false);
  MarkSweep::RecursiveMark();
}

void IPCMarkSweep::ProcessMarkStackParallel(size_t thread_count) {
  Thread* self = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep::ProcessMarkStackParallel: " << thread_count
      << "; tid:" << self->GetTid();
  MarkSweep::ProcessMarkStackParallel(thread_count);
}

IPCPartialMarkSweep::IPCPartialMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix)
    : IPCMarkSweep(ipcHeap, is_concurrent, name_prefix + (name_prefix.empty() ? "" : " ") + "partial") {
  cumulative_timings_.SetName(GetName());
}

void IPCPartialMarkSweep::BindBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCPartialMarkSweep::BindBitmaps. starting: _______ " <<
      "; phase:" << meta_data_->gc_phase_;
  IPCMarkSweep::BindBitmaps();

  WriterMutexLock mu(Thread::Current(), *Locks::heap_bitmap_lock_);
  // For partial GCs we need to bind the bitmap of the zygote space so that all objects in the
  // zygote space are viewed as marked.
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect) {
      CHECK(space->IsZygoteSpace());
      ImmuneSpace(space);
    }
  }
}



IPCStickyMarkSweep::IPCStickyMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix)
    : IPCPartialMarkSweep(ipcHeap, is_concurrent,
                       name_prefix + (name_prefix.empty() ? "" : " ") + "sticky") {
  cumulative_timings_.SetName(GetName());
}

void IPCStickyMarkSweep::BindBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCStickyMarkSweep::BindBitmaps. starting: _______ " <<
      "; phase:" << meta_data_->gc_phase_;
  IPCPartialMarkSweep::BindBitmaps();

  WriterMutexLock mu(Thread::Current(), *Locks::heap_bitmap_lock_);
  // For sticky GC, we want to bind the bitmaps of all spaces as the allocation stack lets us
  // know what was allocated since the last GC. A side-effect of binding the allocation space mark
  // and live bitmap is that marking the objects will place them in the live bitmap.
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect) {
      if(!space->HasBitmapsBound()) {
        BindLiveToMarkBitmap(space);
      }
      //BindLiveToMarkBitmap(space);
    }
  }
  space::LargeObjectSpace* _LOS = GetHeap()->GetLargeObjectsSpace();
  if (_LOS != NULL)
    GetHeap()->GetLargeObjectsSpace()->CopyLiveToMarked();
}

void IPCStickyMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  mark_stack_->Reset();
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCStickyMarkSweep::MarkReachableObjects. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  // All reachable objects must be referenced by a root or a dirty card, so we can clear the mark
  // stack here since all objects in the mark stack will get scanned by the card scanning anyways.
  // TODO: Not put these objects in the mark stack in the first place.
  HandshakeIPCSweepMarkingPhase();
  RecursiveMarkDirtyObjects(false, accounting::ConstantsCardTable::kCardDirty - 1);
}

void IPCStickyMarkSweep::Sweep(bool swap_bitmaps) {
  accounting::ATOMIC_OBJ_STACK_T* live_stack = GetHeap()->GetLiveStack();
  SweepArray(live_stack, false);
}

void IPCStickyMarkSweep::MarkThreadRoots(Thread* self) {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCStickyMarkSweep::MarkThreadRoots. starting: _______ " <<
      "; phase:" << meta_data_->gc_phase_;
  MarkRootsCheckpoint(self);
}


/*

bool IPCMarkSweep::IsConcurrent() const {
  return (meta_data_->is_concurrent_ != 0);
}








*/




/*

*/

//void IPCMarkSweep::SwapBitmaps() {
//  IPC_MARKSWEEP_VLOG(ERROR) << "###### IPCMarkSweep::SwapBitmaps() #### ";
//  // Swap the live and mark bitmaps for each alloc space. This is needed since sweep re-swaps
//  // these bitmaps. The bitmap swapping is an optimization so that we do not need to clear the live
//  // bits of dead objects in the live bitmap.
//  const GcType gc_type = GetGcType();
//  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
//    // We never allocate into zygote spaces.
//    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect ||
//        (gc_type == kGcTypeFull &&
//         space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect)) {
//      accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
//      accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();
//
//      if (live_bitmap != mark_bitmap) {
//        heap_->GetLiveBitmap()->ReplaceBitmap(live_bitmap, mark_bitmap);
//        heap_->GetMarkBitmap()->ReplaceBitmap(mark_bitmap, live_bitmap);
//        space->AsDlMallocSpace()->SwapBitmaps();
//      }
//    }
//  }
//  for (const auto& disc_space : GetHeap()->GetDiscontinuousSpaces()) {
//    space::LargeObjectSpace* space = down_cast<space::LargeObjectSpace*>(disc_space);
//    accounting::SpaceSetMap* live_set = space->GetLiveObjects();
//    accounting::SpaceSetMap* mark_set = space->GetMarkObjects();
//    heap_->GetLiveBitmap()->ReplaceObjectSet(live_set, mark_set);
//    heap_->GetMarkBitmap()->ReplaceObjectSet(mark_set, live_set);
//    down_cast<space::LargeObjectSpace*>(space)->SwapBitmaps();
//  }
//}
/*
void IPCMarkSweep::UnBindBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void IPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  IPC_MARKSWEEP_VLOG(ERROR) << "IPCMarkSweep::BindLiveToMarkBitmap";
  CHECK(space->IsDlMallocSpace());
  space::DL_MALLOC_SPACE* _space = space->AsDlMallocSpace();
//  space::SharableDlMallocSpace* _space =
//      reinterpret_cast<space::SharableDlMallocSpace*>(alloc_space);
  _space->BindLiveToMarkBitmap();
}

*/
/*
IPCPartialMarkSweep::IPCPartialMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap, is_concurrent),
    PartialMarkSweep(ipcHeap->local_heap_, is_concurrent,
        name_prefix + (name_prefix.empty() ? "" : " ") + "partialIpcMS") {
  IPC_MARKSWEEP_VLOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: " << GetGcType() << " ###########";
}


void PartialIPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "     PartialIPCMarkSweep::FinishPhase...begin:" <<
      currThread->GetTid();
  PartialMarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();
}

void PartialIPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    IPC_MARKSWEEP_VLOG(ERROR) << "     PartialIPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    PartialMarkSweep::InitializePhase();
  }
}


void PartialIPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "     PartialIPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  PartialMarkSweep::MarkingPhase();

}



void PartialIPCMarkSweep::MarkReachableObjects() {

  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << " <<PartialIPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  {

    HandshakeMarkingPhase();
  }
  PartialMarkSweep::MarkReachableObjects();
  IPC_MARKSWEEP_VLOG(ERROR) << " >>PartialIPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}



//void PartialIPCMarkSweep::SwapBitmaps() {
//  IPC_MARKSWEEP_VLOG(ERROR) << "PartialIPCMarkSweep::SwapBitmaps()";
//  // Swap the live and mark bitmaps for each alloc space. This is needed since sweep re-swaps
//  // these bitmaps. The bitmap swapping is an optimization so that we do not need to clear the live
//  // bits of dead objects in the live bitmap.
//  const GcType gc_type = GetGcType();
//  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
//    // We never allocate into zygote spaces.
//    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect ||
//        (gc_type == kGcTypeFull &&
//         space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect)) {
//      accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
//      accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();
//
//      if (live_bitmap != mark_bitmap) {
//        heap_->GetLiveBitmap()->ReplaceBitmap(live_bitmap, mark_bitmap);
//        heap_->GetMarkBitmap()->ReplaceBitmap(mark_bitmap, live_bitmap);
//        space->AsDlMallocSpace()->SwapBitmaps();
//      }
//    }
//  }
//  for (const auto& disc_space : GetHeap()->GetDiscontinuousSpaces()) {
//    space::LargeObjectSpace* space = down_cast<space::LargeObjectSpace*>(disc_space);
//    accounting::SpaceSetMap* live_set = space->GetLiveObjects();
//    accounting::SpaceSetMap* mark_set = space->GetMarkObjects();
//    heap_->GetLiveBitmap()->ReplaceObjectSet(live_set, mark_set);
//    heap_->GetMarkBitmap()->ReplaceObjectSet(mark_set, live_set);
//    down_cast<space::LargeObjectSpace*>(space)->SwapBitmaps();
//  }
//}

void PartialIPCMarkSweep::SwapBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "PartialIPCMarkSweep::SwapBitmaps()";
  // Swap the live and mark bitmaps for each alloc space. This is needed since sweep re-swaps
  // these bitmaps. The bitmap swapping is an optimization so that we do not need to clear the live
  // bits of dead objects in the live bitmap.
  const GcType gc_type = GetGcType();
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    // We never allocate into zygote spaces.
    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect ||
        (gc_type == kGcTypeFull &&
         space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect)) {
      accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
      accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();

      if (live_bitmap != mark_bitmap) {
//        heap_->GetLiveBitmap()->ReplaceBitmap(live_bitmap, mark_bitmap);
//        heap_->GetMarkBitmap()->ReplaceBitmap(mark_bitmap, live_bitmap);
        space->AsDlMallocSpace()->SwapBitmaps();
      }
    }
  }
  for (const auto& disc_space : GetHeap()->GetDiscontinuousSpaces()) {
    space::LargeObjectSpace* space = down_cast<space::LargeObjectSpace*>(disc_space);
    accounting::SpaceSetMap* live_set = space->GetLiveObjects();
    accounting::SpaceSetMap* mark_set = space->GetMarkObjects();
    heap_->GetLiveBitmap()->ReplaceObjectSet(live_set, mark_set);
    heap_->GetMarkBitmap()->ReplaceObjectSet(mark_set, live_set);
    down_cast<space::LargeObjectSpace*>(space)->SwapBitmaps();
  }
}


void PartialIPCMarkSweep::UnBindBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "PartialIPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void PartialIPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  IPC_MARKSWEEP_VLOG(ERROR) << "PartialIPCMarkSweep::BindLiveToMarkBitmap";
  CHECK(space->IsDlMallocSpace());
  space::DL_MALLOC_SPACE* _space = space->AsDlMallocSpace();
//  space::SharableDlMallocSpace* _space =
//      reinterpret_cast<space::SharableDlMallocSpace*>(alloc_space);
  _space->BindLiveToMarkBitmap();
}


StickyIPCMarkSweep::StickyIPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap, is_concurrent),
    StickyMarkSweep(ipcHeap->local_heap_, is_concurrent,
        name_prefix + (name_prefix.empty() ? "" : " ") + "stickyIpcMS") {
  IPC_MARKSWEEP_VLOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: " << GetGcType() << " ###########";
}



void StickyIPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "     StickyIPCMarkSweep::FinishPhase...begin:" <<
      currThread->GetTid();
  StickyMarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();

}

void StickyIPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    IPC_MARKSWEEP_VLOG(ERROR) << "     StickyIPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    StickyMarkSweep::InitializePhase();
  }
}

void StickyIPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << "     StickyIPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  StickyMarkSweep::MarkingPhase();

}



void StickyIPCMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  IPC_MARKSWEEP_VLOG(ERROR) << " <<StickyIPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  {
    HandshakeMarkingPhase();
  }
  StickyMarkSweep::MarkReachableObjects();
  IPC_MARKSWEEP_VLOG(ERROR) << " >>StickyIPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

*/
//void StickyIPCMarkSweep::SwapBitmaps() {
//  IPC_MARKSWEEP_VLOG(ERROR) << "StickyIPCMarkSweep::SwapBitmaps()";
//  // Swap the live and mark bitmaps for each alloc space. This is needed since sweep re-swaps
//  // these bitmaps. The bitmap swapping is an optimization so that we do not need to clear the live
//  // bits of dead objects in the live bitmap.
//  const GcType gc_type = GetGcType();
//  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
//    // We never allocate into zygote spaces.
//    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect ||
//        (gc_type == kGcTypeFull &&
//         space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect)) {
//      accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
//      accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();
//
//      if (live_bitmap != mark_bitmap) {
//        heap_->GetLiveBitmap()->ReplaceBitmap(live_bitmap, mark_bitmap);
//        heap_->GetMarkBitmap()->ReplaceBitmap(mark_bitmap, live_bitmap);
//        space->AsDlMallocSpace()->SwapBitmaps();
//      }
//    }
//  }
//  for (const auto& disc_space : GetHeap()->GetDiscontinuousSpaces()) {
//    space::LargeObjectSpace* space = down_cast<space::LargeObjectSpace*>(disc_space);
//    accounting::SpaceSetMap* live_set = space->GetLiveObjects();
//    accounting::SpaceSetMap* mark_set = space->GetMarkObjects();
//    heap_->GetLiveBitmap()->ReplaceObjectSet(live_set, mark_set);
//    heap_->GetMarkBitmap()->ReplaceObjectSet(mark_set, live_set);
//    down_cast<space::LargeObjectSpace*>(space)->SwapBitmaps();
//  }
//}

/*
void StickyIPCMarkSweep::SwapBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "StickyIPCMarkSweep::SwapBitmaps()";
  // Swap the live and mark bitmaps for each alloc space. This is needed since sweep re-swaps
  // these bitmaps. The bitmap swapping is an optimization so that we do not need to clear the live
  // bits of dead objects in the live bitmap.
  const GcType gc_type = GetGcType();
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    // We never allocate into zygote spaces.
    if (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect ||
        (gc_type == kGcTypeFull &&
         space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect)) {
      accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
      accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();

      if (live_bitmap != mark_bitmap) {
//        heap_->GetLiveBitmap()->ReplaceBitmap(live_bitmap, mark_bitmap);
//        heap_->GetMarkBitmap()->ReplaceBitmap(mark_bitmap, live_bitmap);
        space->AsDlMallocSpace()->SwapBitmaps();
      }
    }
  }
  for (const auto& disc_space : GetHeap()->GetDiscontinuousSpaces()) {
    space::LargeObjectSpace* space = down_cast<space::LargeObjectSpace*>(disc_space);
    accounting::SpaceSetMap* live_set = space->GetLiveObjects();
    accounting::SpaceSetMap* mark_set = space->GetMarkObjects();
    heap_->GetLiveBitmap()->ReplaceObjectSet(live_set, mark_set);
    heap_->GetMarkBitmap()->ReplaceObjectSet(mark_set, live_set);
    down_cast<space::LargeObjectSpace*>(space)->SwapBitmaps();
  }
}

void StickyIPCMarkSweep::UnBindBitmaps() {
  IPC_MARKSWEEP_VLOG(ERROR) << "StickyIPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void StickyIPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  IPC_MARKSWEEP_VLOG(ERROR) << "StickyIPCMarkSweep::BindLiveToMarkBitmap";
  CHECK(space->IsDlMallocSpace());
  space::DL_MALLOC_SPACE* _space = space->AsDlMallocSpace();
//  space::SharableDlMallocSpace* _space =
//      reinterpret_cast<space::SharableDlMallocSpace*>(alloc_space);
  _space->BindLiveToMarkBitmap();
}
*/
#if 0

class ClientIpcCollectorTask : public Task {
 public:
  ClientIpcCollectorTask(InterProcessMutex* ipcMutex,
      InterProcessConditionVariable* ipcCond) : ipcMutex_(ipcMutex),
      ipcCond_(ipcCond) {
    Thread* currThread = Thread::Current();
    LOG(ERROR) << "ClientIpcCollectorTask: create new task " << currThread->GetTid();
  }
//      : barrier_(barrier),
//        count1_(count1),
//        count2_(count2),
//        count3_(count3) {}

  void Run(Thread* self) {
    LOG(ERROR) << "Running collector task: " << self->GetTid();
    Runtime* runtime = Runtime::Current();
    runtime->GetHeap()->GCServiceSignalConcGC(self);

    LOG(ERROR) << "leaving collector task: " << self->GetTid();
//    LOG(INFO) << "Before barrier 1 " << *self;
//    ++*count1_;
//    barrier_->Wait(self);
//    ++*count2_;
//    LOG(INFO) << "Before barrier 2 " << *self;
//    barrier_->Wait(self);
//    ++*count3_;
//    LOG(INFO) << "After barrier 2 " << *self;
  }

  virtual void Finalize() {
    delete this;
  }

  private:
   InterProcessMutex* ipcMutex_;
   InterProcessConditionVariable* ipcCond_;
//  Barrier* const barrier_;
//  AtomicInteger* const count1_;
//  AtomicInteger* const count2_;
//  AtomicInteger* const count3_;
};




//void IPCMarkSweep::InitialPhase(){
//  Thread* currThread = Thread::Current();
//  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_INIT, currThread);
//  ResetMetaDataUnlocked();
//}
//
//
//void IPCMarkSweep::MarkRootPhase(void){
//  Thread* currThread = Thread::Current();
//  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_ROOT_MARK, currThread);
//  phase_cond_->Broadcast(currThread);
//}
//

//
//
//void IPCMarkSweep::ReclaimPhase(void){
//  Thread* currThread = Thread::Current();
//  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_RECLAIM, currThread);
//}
//
//

//
//
//void IPCMarkSweep::ServerRun(void) {
//  InitialPhase();
//  /* block until client marks the roots */
//  MarkRootPhase();
//
//  ConcMarkPhase();
//
//
//  ReclaimPhase();
//
//
//  FinishPhase();
//
//}
//
//
//
//void IPCMarkSweep::ClientInitialPhase(void) {
//  Thread* currThread = Thread::Current();
//  GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_ROOT_MARK, currThread);
//}
//
//void IPCMarkSweep::ClientMarkRootPhase(void) {
//  Thread* currThread = Thread::Current();
//  // do marking roots here
//  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_CONC_MARK, currThread);
//  phase_cond_->Broadcast(currThread);
//}
//
//void IPCMarkSweep::ClientConcMarkPhase(void) {
//
//}
//
//
//void IPCMarkSweep::ClientReclaimPhase(void) {
//
//}
//
//void IPCMarkSweep::ClientFinishPhase(void) {
//
//}
//
//void IPCMarkSweep::ClientRun(void) {
//  //wait for signal to mark the roots
//  ClientInitialPhase();
//  /* start the root marking phase */
//  ClientMarkRootPhase();
//
//  ClientConcMarkPhase();
//
//  ClientReclaimPhase();
//
//
//
//  ClientFinishPhase();
//
//}
//
//
void IPCMarkSweep::PreInitCollector(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::PreInitCollector. starting: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  {
    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_PRE_INIT, currThread);
    phase_cond_->Broadcast(currThread);
  }
//  //GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_INIT, currThread);
  LOG(ERROR) << "     IPCMarkSweep::PreInitCollector. leaving: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
}

void IPCMarkSweep::ReclaimClientPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::ReclaimClientPhase. starting: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  {
    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_RECLAIM, currThread);
    phase_cond_->Broadcast(currThread);
  }
  LOG(ERROR) << "     IPCMarkSweep::ReclaimClientPhase. ending: " <<
        currThread->GetTid() << "; phase:" << meta_->gc_phase_;
}

void IPCMarkSweep::ConcMarkPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::ConcMarkPhase. starting: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  {
    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_CONC_MARK, currThread);
    phase_cond_->Broadcast(currThread);
  }
  LOG(ERROR) << "     IPCMarkSweep::ConcMarkPhase. ending: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  //do the conc marking here
}

void IPCMarkSweep::FinalizePhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::FinalizePhase. starting: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  //GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_POST_FINISH, currThread);
  {
    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_POST_FINISH, currThread);
    phase_cond_->Broadcast(currThread);
  }
  LOG(ERROR) << "     IPCMarkSweep::FinalizePhase. ending: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
}


void IPCMarkSweep::ResetPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::ResetPhase. starting: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  {
    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_NONE, currThread);
    phase_cond_->Broadcast(currThread);
  }
  LOG(ERROR) << "     IPCMarkSweep::ResetPhase. ending: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
}
//void IPCMarkSweep::ReclaimPhase(void){
//  Thread* currThread = Thread::Current();
//  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_RECLAIM, currThread);
//}



bool IPCMarkSweep::RunCollectorDaemon() {
  Thread* self = Thread::Current();
  LOG(ERROR) << "IPCMarkSweep::WaitForRequest.." << self->GetTid();

  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    LOG(ERROR) << "-------- IPCMarkSweep::RunCollectorDaemon --------- before while: conc flag = " << meta_->conc_flag_;
    while(meta_->conc_flag_ == 0) {
      conc_req_cond_->Wait(self);
    }
    LOG(ERROR) << "-------- IPCMarkSweep::RunCollectorDaemon --------- leaving wait: conc flag = " << meta_->conc_flag_;

  }
  Runtime* runtime = Runtime::Current();

  ScopedThreadStateChange tscConcA(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    meta_->is_gc_running_ = 1;
    //meta_->conc_flag_ = 0;
   // meta_->is_gc_complete_ = 0;
    conc_req_cond_->Broadcast(self);
  }
  LOG(ERROR) << ">>>>>>>>>Heap::ConcurrentGC...Starting: " << self->GetTid() << " <<<<<<<<<<<<<<<";
  runtime->GetHeap()->ConcurrentGC(self);
  meta_->conc_count_ = meta_->conc_count_ + 1;
  LOG(ERROR) << "<<<<<<<<<Heap::ConcurrentGC...Done: " << self->GetTid() <<
      " >>>>>>>>>>>>>>> conc_count=" << meta_->conc_count_;
  ScopedThreadStateChange tscConcB(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    meta_->is_gc_complete_ = 1;
    meta_->is_gc_running_  = 0;
    meta_->conc_flag_ = 0;
    conc_req_cond_->Broadcast(self);
  }
  {
    ScopedThreadStateChange tscConcB(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *conc_req_cond_mu_);
      while(meta_->is_gc_complete_ == 1) {
        LOG(ERROR) << "      IPCMarkSweep::RunCollectorDaemon: waiting for gc_complete reset";
        conc_req_cond_->Wait(self);
      }
      conc_req_cond_->Broadcast(self);
      LOG(ERROR) << "      IPCMarkSweep::RunCollectorDaemon: leave waiting for gc_complete reset";
    }
  }
  return true;
}



void IPCMarkSweep::PreConcMarkingPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::PreConcMarkingPhase. starting: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  {
    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_PRE_CONC_ROOT_MARK, currThread);
    //GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_PRE_CONC_ROOT_MARK, currThread);
    phase_cond_->Broadcast(currThread);
  }
  LOG(ERROR) << "     IPCMarkSweep::PreConcMarkingPhase. ending: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
}

/******* overriding marksweep code *************/

void IPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "IPCMarkSweep::FinishPhase...begin:" << currThread->GetTid();
//  {
//    LOG(ERROR) << "     IPCMarkSweep::FinishPhase. starting: " <<
//        currThread->GetTid() << "; phase:" << meta_->gc_phase_;
//    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_FINISH, currThread);
//    phase_cond_->Broadcast(currThread);
//    LOG(ERROR) << "     IPCMarkSweep::FinishPhase. ending: " <<
//        currThread->GetTid() << "; phase:" << meta_->gc_phase_;
//  }
  MarkSweep::FinishPhase();
  //FinalizePhase();
  //ResetPhase();
  //LOG(ERROR) << "IPCMarkSweep::FinishPhase...Left:" << currThread->GetTid();
}

void IPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
 // PreInitCollector();

  {
    LOG(ERROR) << "     IPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << meta_->gc_phase_;
    //GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_INIT, currThread);
   // phase_cond_->Broadcast(currThread);
    StickyMarkSweep::InitializePhase();
    //LOG(ERROR) << "     IPCMarkSweep::InitializePhase. endingB: " <<
    //    currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  }
 // LOG(ERROR) << "IPCMarkSweep::InitializePhase...end:" << currThread->GetTid();
}


void IPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
//  {
//    GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_ROOT_MARK, currThread);
//    phase_cond_->Broadcast(currThread);
//  }
//  LOG(ERROR) << "     IPCMarkSweep::MarkingPhase. endingA: " <<
//      currThread->GetTid() << "; phase:" << meta_->gc_phase_;
  MarkSweep::MarkingPhase();
//  PreConcMarkingPhase();
//  ConcMarkPhase();
//  ReclaimClientPhase();
}


#endif
}
}
}


//
//
//





