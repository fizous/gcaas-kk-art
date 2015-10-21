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
//    LOG(ERROR) << "XXXXXXXXX IPCHeap::IPCHeap .. could not initialize collector"
//        << " daemon .. XXXXXXXXX";
//  }

  ResetHeapMetaDataUnlocked();

}


void IPCHeap::SetCollectorDaemon(Thread* thread) {
  MutexLock mu(thread, ms_lock_);
  collector_daemon_ = thread;
  ms_cond_.Broadcast(thread);
}

bool IPCHeap::StartCollectorDaemon(void) {



  LOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon-----------";

  CHECK_PTHREAD_CALL(pthread_create,
      (&collector_pthread_, NULL,
      &IPCHeap::RunDaemon, this),
      "IPCHeap Daemon thread");

  Thread* self = Thread::Current();
  MutexLock mu(self, ms_lock_);
  LOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon going " <<
      "waits for daemon initialization";
  while (collector_daemon_ == NULL) {
    ms_cond_.Wait(self);
  }

  LOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon done " <<
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
  meta_->conc_count_      = 0;
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
}


void IPCHeap::AssignNextGCType(void) {
  meta_->next_gc_type_ = local_heap_->next_gc_type_;
}

void* IPCHeap::RunDaemon(void* arg) {
  LOG(ERROR) << "AbstractIPCMarkSweep::RunDaemon: begin" ;
  IPCHeap* _ipc_heap = reinterpret_cast<IPCHeap*>(arg);
  CHECK(_ipc_heap != NULL);

  Runtime* runtime = Runtime::Current();
  CHECK(runtime->AttachCurrentThread("IPC-MS-Daem", true, NULL, false));

  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    _ipc_heap->SetCollectorDaemon(self);
  }


  LOG(ERROR) << "AbstractIPCMarkSweep::RunDaemon: broadcast" ;
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
  LOG(ERROR) << "++++++ IPCHeap::SetLastProcessID: " << meta_->process_state_;
}


bool IPCHeap::CheckTrimming() {
  LOG(ERROR) << "bool IPCHeap::CheckTrimming()";
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
    LOG(ERROR) << "bool IPCHeap::Posted a Request()";
    return true;
  }
  return false;
}


void IPCHeap::TrimHeap(void)  {
  local_heap_->Trim();
}

collector::GcType IPCHeap::WaitForConcurrentIPCGcToComplete(Thread* self) {
  LOG(ERROR) << "*****Executing the WaitForConcurrentIPCGcToComplete**** " << self->GetTid();
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
      LOG(ERROR) << "*** collector: " << collector->GetName();
      break;
    }
  }

  CHECK(collector != NULL)
      << "Could not find garbage collector with concurrent=" << meta_->concurrent_gc_
      << " and type=" << gc_type;

  collector->SetClearSoftReferences(clear_soft_references);
  LOG(ERROR) << "GCMMP collect -> "
      << gc_cause_and_type_strings[gc_cause][gc_type]
      << " from thread ID:" << self->GetTid();
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
  if (!((curr_gc_cause_ == kGcCauseBackground) || (curr_gc_cause_ == kGcCauseExplicit))) { //a mutator is performing an allocation. do not involve service to get things done faster
    return;
  }

  int _expected_flag_value, _new_raised_flag;
  do {
    _expected_flag_value = 0;
    _new_raised_flag = 1;

  } while  (android_atomic_cas(_expected_flag_value, _new_raised_flag, &ipc_flag_raised_) != 0);

}

void IPCHeap::SetCurrentCollector(IPCMarkSweep* collector) {
  collector->server_synchronize_ = ipc_flag_raised_;
  Thread* self = Thread::Current();
  if(collector->server_synchronize_) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *conc_req_cond_mu_);
      meta_->conc_flag_ = 2;
      meta_->collect_index_ = collector->collector_index_;
      meta_->current_collector_ = collector->meta_data_;
      LOG(ERROR) << "\t client: Setting current collector as follows: " <<
          "index = " << meta_->collect_index_ <<
          "\n\t    address = " << reinterpret_cast<void*>(meta_->current_collector_);
      conc_req_cond_->Broadcast(self);
    }
  }

}


void IPCHeap::ResetCurrentCollector(IPCMarkSweep* collector) {
  Thread* self = Thread::Current();
  if(collector->server_synchronize_) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *conc_req_cond_mu_);
      while(meta_->conc_flag_ < 3) {
        conc_req_cond_->Wait(self);
      }
      meta_->conc_flag_ = 4;
      collector->server_synchronize_ = 0;
      meta_->collect_index_ = -1;
      meta_->current_collector_ = NULL;
      LOG(ERROR) << "\t client: Client notified completion";
      conc_req_cond_->Broadcast(self);
    }
  }

}


void IPCHeap::ResetServerFlag(void) {
  if (!((curr_gc_cause_ == kGcCauseBackground) ||
      (curr_gc_cause_ == kGcCauseExplicit))) { //a mutator is performing an allocation. do not involve service to get things done faster
    return;
  }

  int _expected_flag_value, _new_raised_flag;
  do {
    _expected_flag_value = 1;
    _new_raised_flag = 0;

  } while (android_atomic_cas(_expected_flag_value, _new_raised_flag, &ipc_flag_raised_) != 0);

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
  LOG(ERROR) << "IPCHeap::WaitForRequest.." << self->GetTid();

 // int _gc_type = 1; //concurrent
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    LOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- before while: conc flag = " << meta_->conc_flag_;
    while(meta_->conc_flag_  == 0) {
      conc_req_cond_->Wait(self);
    }
//    if(meta_->conc_flag_ & KGCAgentExplicitGCSignal) {
//      _gc_type = 2;
//
//    }
    LOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- leaving wait: conc flag = "
        << meta_->conc_flag_ << "; gctype = " << meta_->gc_type_;

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
  LOG(ERROR) << ">>>>>>>>>IPCHeap::ConcurrentGC...Starting: " << self->GetTid()
      << " <<<<<<<<<<<<<<<";
  if(((GcType) meta_->gc_type_) == kGcTypeFull) {
    ExplicitGC(false);
  } else {
    ConcurrentGC(self);
  }

  meta_->conc_count_ = meta_->conc_count_ + 1;
  LOG(ERROR) << "<<<<<<<<<IPCHeap::ConcurrentGC...Done: " << self->GetTid() <<
      " >>>>>>>>>>>>>>> conc_count=" << meta_->conc_count_;
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
//        LOG(ERROR) << "      IPCHeap::RunCollectorDaemon: waiting for gc_complete reset";
//        conc_req_cond_->Wait(self);
//      }
//      conc_req_cond_->Broadcast(self);
//      LOG(ERROR) << "      IPCHeap::RunCollectorDaemon: leave waiting for gc_complete reset";
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
  LOG(ERROR) << "############ Initializing IPC: " << collector_index_;
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
  heap_meta_->conc_count_ = 0;


  /////////
  meta_data_->immune_begin_ = NULL;
  meta_data_->immune_end_ = NULL;
  meta_data_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  meta_data_->current_mark_bitmap_ = NULL;
}

void AbstractIPCMarkSweep::DumpValues(void){
  LOG(ERROR) << "Dump AbstractIPCMarkSweep: " << "zygote_begin: "
      << reinterpret_cast<void*>(heap_meta_->zygote_begin_)
      << "\n     zygote_end: " << reinterpret_cast<void*>(heap_meta_->zygote_end_)
      << "\n     image_begin: " << reinterpret_cast<void*>(heap_meta_->image_space_begin_)
      << "\n     image_end: " << reinterpret_cast<void*>(heap_meta_->image_space_end_);
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
//    LOG(ERROR) << "the client changes phase from: : " << heap_meta_->gc_phase_;
//    GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_PRE_CONC_ROOT_MARK, currThread);
//    heap_meta_->gc_phase_ = space::IPC_GC_PHASE_CONC_MARK;
//    LOG(ERROR) << "      to : " << heap_meta_->gc_phase_;
//    ipc_heap_->ipc_flag_raised_ = 0;
//    phase_cond_->Broadcast(currThread);
//  } else {
//    LOG(ERROR) << "ipc_heap_->ipc_flag_raised_ was zero";
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




IPCMarkSweep::IPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap, is_concurrent),
    MarkSweep(ipcHeap->local_heap_, is_concurrent,
        name_prefix + (name_prefix.empty() ? "" : " ") + "ipcMS") {
  LOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: "
      << GetGcType() << "; conc:" << IsConcurrent() <<" ###########";
}



void IPCMarkSweep::ClearMarkHolders(void) {
  LOG(ERROR) << "IPCMarkSweep::ClearMarkHolders..............";
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
  LOG(ERROR) << "__________ IPCMarkSweep::PreInitializePhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  ipc_heap_->SetCurrentCollector(this);

}


void IPCMarkSweep::InitializePhase(void) {
  timings_.Reset();
  base::TimingLogger::ScopedSplit split("InitializePhase", &timings_);
  PreInitializePhase();

  art::Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_INIT);
  LOG(ERROR) << "_______IPCMarkSweep::InitializePhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;

  mark_stack_ = ipc_heap_->local_heap_->GetHeapMarkStack();
  SetImmuneRange(nullptr, nullptr);
  soft_reference_list_ = nullptr;
  weak_reference_list_ = nullptr;
  finalizer_reference_list_ = nullptr;
  phantom_reference_list_ = nullptr;
  cleared_reference_list_ = nullptr;
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
  java_lang_Class_ = Class::GetJavaLangClass();

  FindDefaultMarkBitmap();
  LOG(ERROR) << "_______IPCMarkSweep::InitializePhase. going for GCVerification: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  ipc_heap_->local_heap_->PreGcVerification(this);
}

void IPCMarkSweep::ApplyTrimming(void) {
  LOG(ERROR) << "IPCMarkSweep::ApplyTrimming";
  ipc_heap_->CheckTrimming();
}

void IPCMarkSweep::FinishPhase(void) {
 Thread* currThread = Thread::Current();
 UpdateGCPhase(currThread, space::IPC_GC_PHASE_FINISH);
 LOG(ERROR) << "_______IPCMarkSweep::FinishPhase. starting: _______ " <<
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
  meta_data_->immune_begin_ = begin;
  meta_data_->immune_end_ = end;
}


void IPCMarkSweep::MarkConcurrentRoots() {
  timings_.StartSplit("MarkConcurrentRoots");
  // Visit all runtime roots and clear dirty flags.
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_ROOT_CONC_MARK);
  LOG(ERROR) << "_______IPCMarkSweep::MarkConcurrentRoots. starting: _______ " <<
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
  Thread* currThread = Thread::Current();
  ThreadList* thread_list = Runtime::Current()->GetThreadList();
  LOG(ERROR) << "IPCMarkSweep::PostMarkingPhase: SSSSSSSSSSSSSSSSSSUspended the "
      "threads: " << currThread->GetTid();
  thread_list->SuspendAll();
  LOG(ERROR) << "SSSSSSSSSSSSSSSSSSUspended the threads";
  thread_list->ResumeAll();

  {
    ReaderMutexLock mu_mutator(currThread, *Locks::mutator_lock_);
    WriterMutexLock mu_heap_bitmap(currThread, *Locks::heap_bitmap_lock_);
    MarkReachableObjects();
  }

}

void IPCMarkSweep::IPCMarkRootsPhase(void) {
  base::TimingLogger::ScopedSplit split("MarkingPhase", &timings_);
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_ROOT_MARK);
  LOG(ERROR) << "_______IPCMarkSweep::MarkingPhase. starting: _______ " <<
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
    LOG(ERROR) << " ##### IPCMarkSweep::MarkingPhase. non concurrent marking: _______ " <<
        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  } else { //concurrent
    LOG(ERROR) << " ##### IPCMarkSweep::MarkingPhase.  concurrent marking: _______ " <<
        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
    MarkThreadRoots(currThread);
    // At this point the live stack should no longer have any mutators which push into it.
    MarkNonThreadRoots();
  }
  live_stack_freeze_size_ = ipc_heap_->local_heap_->GetLiveStack()->Size();
  MarkConcurrentRoots();

  ipc_heap_->local_heap_->UpdateAndMarkModUnion(this, timings_, GetGcType());

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
  LOG(ERROR) << "SSSSSSSSSSS Suspended app threads to handshake with service process SSSSSSSSSSSSSSS";
  BlockForGCPhase(currThread, space::IPC_GC_PHASE_MARK_RECURSIVE);
  //thread_list->ResumeAll();
  LOG(ERROR) << "IPCMarkSweep client changes phase from: " << meta_data_->gc_phase_;
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_CONC_MARK);

}

void IPCMarkSweep::HandshakeIPCSweepMarkingPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << " #### IPCMarkSweep::HandshakeMarkingPhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;

  if(server_synchronize_ == 1) {
    RequestAppSuspension();
  } else {
    LOG(ERROR) << " #### IPCMarkSweep:: ipc_heap_->ipc_flag_raised_ was zero";
    UpdateGCPhase(currThread, space::IPC_GC_PHASE_CONC_MARK);
  }

  LOG(ERROR) << "      to : " << meta_data_->gc_phase_;
}

void IPCMarkSweep::ProcessMarkStack(bool paused) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "_______IPCMarkSweep::ProcessMarkStack. starting: _______ " <<
      currThread->GetTid() << "... MarkStackSize=" << mark_stack_->Size();
  MarkSweep::ProcessMarkStack(paused);
}
void IPCMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "_______IPCMarkSweep::MarkReachableObjects. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_ << "... MarkStackSize=" << mark_stack_->Size();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_MARK_REACHABLES);
  HandshakeIPCSweepMarkingPhase();
  MarkSweep::MarkReachableObjects();
  LOG(ERROR) << " >>IPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

void IPCMarkSweep::ProcessMarkStackParallel(size_t thread_count) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "IPCMarkSweep::ProcessMarkStackParallel: " << thread_count
      << "; tid:" << self->GetTid();
  MarkSweep::ProcessMarkStackParallel(thread_count);
}

IPCPartialMarkSweep::IPCPartialMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix)
    : IPCMarkSweep(ipcHeap, is_concurrent, name_prefix + (name_prefix.empty() ? "" : " ") + "partial") {
  cumulative_timings_.SetName(GetName());
}

void IPCPartialMarkSweep::BindBitmaps() {
  LOG(ERROR) << "IPCPartialMarkSweep::BindBitmaps. starting: _______ " <<
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
  LOG(ERROR) << "IPCStickyMarkSweep::BindBitmaps. starting: _______ " <<
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
  LOG(ERROR) << "IPCStickyMarkSweep::MarkReachableObjects. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_MARK_REACHABLES);
  HandshakeIPCSweepMarkingPhase();
  // All reachable objects must be referenced by a root or a dirty card, so we can clear the mark
  // stack here since all objects in the mark stack will get scanned by the card scanning anyways.
  // TODO: Not put these objects in the mark stack in the first place.
  mark_stack_->Reset();
  RecursiveMarkDirtyObjects(false, accounting::ConstantsCardTable::kCardDirty - 1);
}

void IPCStickyMarkSweep::Sweep(bool swap_bitmaps) {
  accounting::ATOMIC_OBJ_STACK_T* live_stack = GetHeap()->GetLiveStack();
  SweepArray(live_stack, false);
}

void IPCStickyMarkSweep::MarkThreadRoots(Thread* self) {
  LOG(ERROR) << "IPCStickyMarkSweep::MarkThreadRoots. starting: _______ " <<
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
//  LOG(ERROR) << "###### IPCMarkSweep::SwapBitmaps() #### ";
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
  LOG(ERROR) << "IPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void IPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  LOG(ERROR) << "IPCMarkSweep::BindLiveToMarkBitmap";
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
  LOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: " << GetGcType() << " ###########";
}


void PartialIPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     PartialIPCMarkSweep::FinishPhase...begin:" <<
      currThread->GetTid();
  PartialMarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();
}

void PartialIPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    LOG(ERROR) << "     PartialIPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    PartialMarkSweep::InitializePhase();
  }
}


void PartialIPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     PartialIPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  PartialMarkSweep::MarkingPhase();

}



void PartialIPCMarkSweep::MarkReachableObjects() {

  Thread* currThread = Thread::Current();
  LOG(ERROR) << " <<PartialIPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  {

    HandshakeMarkingPhase();
  }
  PartialMarkSweep::MarkReachableObjects();
  LOG(ERROR) << " >>PartialIPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}



//void PartialIPCMarkSweep::SwapBitmaps() {
//  LOG(ERROR) << "PartialIPCMarkSweep::SwapBitmaps()";
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
  LOG(ERROR) << "PartialIPCMarkSweep::SwapBitmaps()";
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
  LOG(ERROR) << "PartialIPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void PartialIPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  LOG(ERROR) << "PartialIPCMarkSweep::BindLiveToMarkBitmap";
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
  LOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: " << GetGcType() << " ###########";
}



void StickyIPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     StickyIPCMarkSweep::FinishPhase...begin:" <<
      currThread->GetTid();
  StickyMarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();

}

void StickyIPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    LOG(ERROR) << "     StickyIPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    StickyMarkSweep::InitializePhase();
  }
}

void StickyIPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     StickyIPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  StickyMarkSweep::MarkingPhase();

}



void StickyIPCMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << " <<StickyIPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  {
    HandshakeMarkingPhase();
  }
  StickyMarkSweep::MarkReachableObjects();
  LOG(ERROR) << " >>StickyIPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

*/
//void StickyIPCMarkSweep::SwapBitmaps() {
//  LOG(ERROR) << "StickyIPCMarkSweep::SwapBitmaps()";
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
  LOG(ERROR) << "StickyIPCMarkSweep::SwapBitmaps()";
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
  LOG(ERROR) << "StickyIPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void StickyIPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  LOG(ERROR) << "StickyIPCMarkSweep::BindLiveToMarkBitmap";
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





