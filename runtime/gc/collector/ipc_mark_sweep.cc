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
#include "gc/service/global_allocator.h"
#include "gc/service/service_client.h"
#include "gc/space/dlmalloc_space-inl.h"
#include "gc/space/space-inl.h"
#include "gc/space/space.h"
#include "gc/space/large_object_space.h"
#include "gc/collector/ipc_mark_sweep.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc/accounting/space_bitmap-inl.h"
#include "gc/accounting/space_bitmap.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache.h"
#include "mirror/art_field-inl.h"
#include "gc/collector/mark_sweep.h"
#include "gc/collector/mark_sweep-inl.h"


using ::art::mirror::Class;
using ::art::mirror::Object;
using ::art::gcservice::GCServiceClient;
using ::art::gc::gcservice::GCSrvcMemInfoOOM;
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


accounting::BaseHeapBitmap* IPCMarkSweep::_temp_heap_beetmap = NULL;

IPCHeap::IPCHeap(space::GCSrvSharableHeapData* heap_meta, Heap* heap) :
        ms_lock_("heap-ipc lock"),
        ms_cond_("heap-ipcs::cond_", ms_lock_),
        meta_(heap_meta),
        local_heap_(heap),
        collector_daemon_(NULL),
        ipc_flag_raised_(0),
        collector_entry_(0) {


  heap->SetSubHeapMetaData(&(meta_->sub_record_meta_));

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
  //    IPC_MS_VLOG(ERROR) << "XXXXXXXXX IPCHeap::IPCHeap .. could not initialize collector"
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



  IPC_MS_VLOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon-----------";

  CHECK_PTHREAD_CALL(pthread_create,
                     (&collector_pthread_, NULL,
                         &IPCHeap::RunDaemon, this),
                         "IPCHeap Daemon thread");

  Thread* self = Thread::Current();
  MutexLock mu(self, ms_lock_);
  IPC_MS_VLOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon going " <<
      "waits for daemon initialization";
  while (collector_daemon_ == NULL) {
    ms_cond_.Wait(self);
  }

  IPC_MS_VLOG(ERROR) << "-----------IPCHeap::StartCollectorDaemon done " <<
      "with creating daemon ";

  //CreateCollectors();

  return true;
}

void IPCHeap::ResetHeapMetaDataUnlocked() { // reset data without locking
  //meta_data_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  //  meta_->freed_objects_   = 0;
  //  meta_->freed_bytes_     = 0;
  meta_->barrier_count_   = 0;
  meta_->conc_flag_       = 0;
  //  meta_->is_gc_complete_  = 0;
  meta_->is_gc_running_   = 0;
  meta_->concurrent_gc_ = (local_heap_->concurrent_gc_) ? 1 : 0;
  meta_->collect_index_ = -1;

  /* heap members */
  //  meta_->last_gc_type_ = collector::kGcTypeNone;
  //  meta_->next_gc_type_ = collector::kGcTypePartial;
  //  meta_->total_wait_time_ = 0;
  //  meta_->concurrent_start_bytes_ = local_heap_->GetConcStartBytes();
  //  meta_->last_gc_size_ = local_heap_->GetLastGCSize();
  //  meta_->last_gc_time_ns_ = local_heap_->GetLastGCTime();



  /* heap statistics */
  //  meta_->total_objects_freed_ever_  = local_heap_->GetObjectsFreedEver();
  //  meta_->total_bytes_freed_ever_    = local_heap_->GetBytesFreedEver();
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


//void IPCHeap::AssignNextGCType(void) {
//  meta_->next_gc_type_ = local_heap_->next_gc_type_;
//}


static void ClientDaemonSetThreadAffinity(Thread* th, bool complementary, int cpu_id) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  uint32_t _cpuCount = (uint32_t) sysconf(_SC_NPROCESSORS_CONF);
  uint32_t _cpu_id =  (uint32_t) cpu_id;
  if(complementary) {
    for(uint32_t _ind = 0; _ind < _cpuCount; _ind++) {
      if(_ind != _cpu_id)
        CPU_SET(_ind, &mask);
    }
  } else {
    CPU_SET(_cpu_id, &mask);
  }
  if(sched_setaffinity(th->GetTid(),
                       sizeof(mask), &mask) != 0) {
    if(complementary) {
      GCMMP_VLOG(INFO) << "GCMMP: Complementary";
    }
    LOG(ERROR) << "GCMMP: Error in setting thread affinity tid:" <<
        th->GetTid() << ", cpuid: " <<  _cpu_id;
  } else {
    if(complementary) {
      GCMMP_VLOG(INFO) << "GCMMP: Complementary";
    }
    GCMMP_VLOG(INFO) << "GCMMP: Succeeded in setting assignments tid:" <<
        th->GetTid() << ", cpuid: " <<  _cpu_id;
  }

}



void* IPCHeap::RunDaemon(void* arg) {
  IPC_MS_VLOG(ERROR) << "AbstractIPCMarkSweep::RunDaemon: begin" ;
  IPCHeap* _ipc_heap = reinterpret_cast<IPCHeap*>(arg);
  CHECK(_ipc_heap != NULL);

  Runtime* runtime = Runtime::Current();
  CHECK(runtime->AttachCurrentThread("IPC-MS-Daem", true, NULL, false));

  Thread* self = Thread::Current();


  bool propagate = false;
  int cpu_id = 0;
  bool _setAffin =
      gcservice::GCServiceGlobalAllocator::GCSrvcIsClientDaemonPinned(&cpu_id, &propagate);

  if(_setAffin) {
    ClientDaemonSetThreadAffinity(self, propagate, cpu_id);
  }


  DCHECK_NE(self->GetState(), kRunnable);
  {
    _ipc_heap->SetCollectorDaemon(self);
  }


  IPC_MS_VLOG(ERROR) << "AbstractIPCMarkSweep::RunDaemon: broadcast" ;
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
  //  LOG(ERROR) << "------IPCHeap::ConcurrentGC-------";
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    if (Runtime::Current()->IsShuttingDown()) {
      return;
    }
  }
  GCP_MARK_START_CONC_GC_HW_EVENT;
  if (WaitForConcurrentIPCGcToComplete(self) == collector::kGcTypeNone) {
    CollectGarbageIPC(local_heap_->GetNextGCType(), kGcCauseBackground, false);
  }
  GCP_MARK_END_CONC_GC_HW_EVENT;
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
  //  LOG(ERROR) << "------IPCHeap::ExplicitGC-------";
  Thread* self = Thread::Current();
  WaitForConcurrentIPCGcToComplete(self);
  CollectGarbageIPC(collector::kGcTypeFull, kGcCauseExplicit, clear_soft_references);
}

void IPCHeap::SetLastProcessID(void) { //here we set the process of the app
  int _lastProcessID = local_heap_->GetLastProcessStateID();
  meta_->process_state_ = _lastProcessID;
  IPC_MS_VLOG(ERROR) << "++++++ IPCHeap::SetLastProcessID: " << meta_->process_state_;
}


bool IPCHeap::CheckTrimming(collector::GcType gc_type, uint64_t gc_duration) {
  IPC_MS_VLOG(ERROR) << "bool IPCHeap::CheckTrimming()";

  //local_heap_->ListenForProcessStateChange();

  //  LOG(ERROR) << "isProcessCare about pause times: "
  //      << ((local_heap_->CareAboutPauseTimes()) ? "true" : "false");
  //  double adjusted_max_free = 1.0;
  gc::space::AgentMemInfo* _mem_info_rec =
      GCServiceClient::service_client_->GetMemInfoRec();
  double _resize_factor = _mem_info_rec->resize_factor_;
  size_t _adjusted_max_free = 0;
  bool _pause_care = GCSrvcMemInfoOOM::CareAboutPauseTimes(_mem_info_rec);
  local_heap_->GCSrvcGrowForUtilization(gc_type, gc_duration, _resize_factor,
                                        &_adjusted_max_free,
                                        static_cast<size_t>(allocation_latency_));

  if(GCServiceClient::service_client_->isTrimRequestsEnabled())
    return local_heap_->RequestHeapTrimIfNeeded(_adjusted_max_free,
                                                _pause_care, true);
  return false;
#if 0

  uint64_t ms_time = MilliTime();
  float utilization =
      static_cast<float>(local_heap_->GetAllocSpace()->GetBytesAllocated()) / local_heap_->GetAllocSpace()->Size();
  if ((utilization > 0.75f && !local_heap_->IsLowMemoryMode()) ||
      ((ms_time - local_heap_->GetLastTimeTrim()) < 2 * 1000)) {
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
  local_heap_->SetLastTimeTrim(ms_time);
  local_heap_->ListenForProcessStateChange();

  // Trim only if we do not currently care about pause times.
  if (!local_heap_->care_about_pause_times_) {
#if (ART_GC_SERVICE || true)
    gcservice::GCServiceClient::RequestHeapTrim();
#endif

    //    JNIEnv* env = self->GetJniEnv();
    //    DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
    //    DCHECK(WellKnownClasses::java_lang_Daemons_requestHeapTrim != NULL);
    //    env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
    //                              WellKnownClasses::java_lang_Daemons_requestHeapTrim);
    //    CHECK(!env->ExceptionCheck());
    IPC_MS_VLOG(ERROR) << "bool IPCHeap::Posted a Request()";
    return true;
  }
  return false;
#endif
}


void IPCHeap::TrimHeap(void)  {
  //LOG(ERROR) << "IPCHeap::TrimHeap";
  //  local_heap_->ListenForProcessStateChange();
  gc::space::AgentMemInfo* _mem_info_rec =
      GCServiceClient::service_client_->GetMemInfoRec();
  //  double _resize_factor = _mem_info_rec->resize_factor_;
  size_t _adjusted_max_free = 0;
  bool _pause_care = GCSrvcMemInfoOOM::CareAboutPauseTimes(_mem_info_rec);


  if(local_heap_->RequestHeapTrimIfNeeded(_adjusted_max_free,
                                          _pause_care, false)) {
    //LOG(ERROR) << "IPCHeap::TrimHeap....heap trim condition passed";
    local_heap_->SetLastTimeTrim(MilliTime());
    //size_t managed_advised =
    local_heap_->Trim();
    // Trim the native heap.
    dlmalloc_trim(0);
    size_t native_reclaimed = 0;
    dlmalloc_inspect_all(DlmallocMadviseCallback, &native_reclaimed);
    //    LOG(ERROR) << "IPCHeap::TrimHeap....done trim()..advised=" <<
    //        PrettySize(managed_advised) << ", native_advised="
    //        << PrettySize(native_reclaimed);
  } else {
    LOG(ERROR) << "IPCHeap::TrimHeap....heap trim condition DID NOT PASS";
  }

}

collector::GcType IPCHeap::WaitForConcurrentIPCGcToComplete(Thread* self) {
  IPC_MS_VLOG(ERROR) << "*****Executing the WaitForConcurrentIPCGcToComplete**** " << self->GetTid();
  collector::GcType last_gc_type = collector::kGcTypeNone;
  bool do_wait = false;
  if(meta_->concurrent_gc_) { // the heap is concurrent
    GCP_MARK_START_WAIT_TIME_EVENT(self);
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
        last_gc_type = local_heap_->GetLastGCType();
        wait_time = NanoTime() - wait_start;
        local_heap_->IncTotalWaitTime(wait_time);
      }
      if (wait_time > local_heap_->long_pause_log_threshold_) {
        LOG(INFO) << "WaitForConcurrentIPCGcToComplete blocked for " << PrettyDuration(wait_time);
      }
    }
    GCP_MARK_END_WAIT_TIME_EVENT(self);
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

  ScopedThreadStateChange tsc(self, kWaitingPerformingGc/*kWaitingForGCProcess*/);
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

  GCP_MARK_PRE_COLLECTION;
  if (gc_cause == kGcCauseForAlloc && Runtime::Current()->HasStatsEnabled()) {
    ++Runtime::Current()->GetStats()->gc_for_alloc_count;
    ++Thread::Current()->GetStats()->gc_for_alloc_count;
  }

  uint64_t gc_start_time_ns = NanoTime();
  uint64_t gc_start_size = local_heap_->GetBytesAllocated();


  if(gc_cause == kGcCauseBackground) {
    GCServiceClient::service_client_->updateDeltaConcReq(gc_start_time_ns,
               gc_start_size, &collection_latency_, &allocation_latency_);
  } else if (gc_cause == kGcCauseExplicit) {
    GCServiceClient::service_client_->updateDeltaExplReq(gc_start_time_ns,
                                                         gc_start_size,
                                                         &collection_latency_,
                                                         &allocation_latency_);
  }

  //  LOG(ERROR) << "IPCHeap::CollectGarbageIPC...gc_start_size=" << gc_start_size<<
  //      ", alloc_space->allocBytes="<< local_heap_->alloc_space_->GetBytesAllocated();
  // Approximate allocation rate in bytes / second.
  if (UNLIKELY(gc_start_time_ns == local_heap_->GetLastGCTime())) {
    LOG(WARNING) << "Timers are broken (gc_start_time == last_gc_time_).";
  }
  uint64_t ms_delta = NsToMs(gc_start_time_ns - local_heap_->GetLastGCTime());
  if (ms_delta != 0) {
    local_heap_->SetAllocationRate(((gc_start_size - local_heap_->GetLastGCSize()) * 1000) / ms_delta);
    VLOG(heap) << "Allocation rate: " << PrettySize(local_heap_->GetAllocationRate()) << "/s";
  }

  if (gc_type == collector::kGcTypeSticky &&
      local_heap_->GetAllocSpace()->Size() < local_heap_->GetMinAllocSpaceSizeForSticky()) {
    gc_type = collector::kGcTypePartial;
  }




  collector::MarkSweep* collector = NULL;

  for (const auto& cur_collector : local_heap_->mark_sweep_collectors_) {
    if (cur_collector->IsConcurrent() == meta_->concurrent_gc_ && cur_collector->GetGcType() == gc_type) {
      collector = cur_collector;
      IPC_MS_VLOG(ERROR) << "*** collector: " << collector->GetName();
      break;
    }
  }

  CHECK(collector != NULL)
  << "Could not find garbage collector with concurrent=" << meta_->concurrent_gc_
  << " and type=" << gc_type;

  collector->SetClearSoftReferences(clear_soft_references);
  //  LOG(ERROR) << "GCMMP collect -> " << gc_cause_and_type_strings[gc_cause][gc_type]
  //      << " from thread ID:" << self->GetTid() <<
  //      "\n freed: " << collector->GetFreedObjects() << " objects"
  //      "\n bytes_freed: " << PrettySize(collector->GetFreedBytes()) << " bytes";
  // IPC_MS_VLOG(ERROR) << "GCMMP collect -> " << gc_cause_and_type_strings[gc_cause][gc_type] << " from thread ID:" << self->GetTid();



  collector->Run();

  local_heap_->IncTotalObjectsFreedEver(collector->GetFreedObjects());
  local_heap_->IncTotalBytesFreedEver(collector->GetFreedBytes());

  //  gc_start_size = local_heap_->GetBytesAllocated();
  //  LOG(ERROR) << "IPCHeap::CollectGarbageIPC..." <<
  //        gc_cause_and_type_strings[gc_cause][gc_type] <<
  //        ", gc_end_size=" << gc_start_size <<
  //        ", end_size..alloc_space->allocBytes="<< local_heap_->alloc_space_->GetBytesAllocated();

  //  meta_->total_objects_freed_ever_  += collector->GetFreedObjects();
  //  meta_->total_bytes_freed_ever_    += collector->GetFreedBytes();
  //  LOG(ERROR) << "@@@@@@@@@@ YYYY @@@@@@" << gc_cause << " " << collector->GetName()
  //            << " GC freed "  <<  collector->GetFreedObjects() << "("
  //            << PrettySize(collector->GetFreedBytes()) << ") AllocSpace objects, "
  //            << collector->GetFreedLargeObjects() << "("
  //            << PrettySize(collector->GetFreedLargeObjectBytes()) << ") LOS objects, "
  //            //<< percent_free << "% free, "
  //            << PrettySize(local_heap_->GetBytesAllocated()) << "/"
  //            << PrettySize(local_heap_->GetTotalMemory());
  //<< ", " << "paused " << pause_string.str()
  //<< " total " << PrettyDuration((duration / 1000) * 1000);

  {
    IPMutexLock interProcMu(self, *gc_complete_mu_);
    local_heap_->SetLastGCType(gc_type);
    ResetServerFlag();
    GCP_MARK_POST_COLLECTION;
    // Wake anyone who may have been waiting for the GC to complete.
    meta_->is_gc_running_ = 0;
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
      IPC_MS_VLOG(ERROR) << "\t client: Setting current collector as follows: " <<
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
      IPC_MS_VLOG(ERROR) << "\t client: Client notified completion";
      conc_req_cond_->Broadcast(self);
    }
  }

}


void IPCHeap::ResetServerFlag(void) {
  if (!(curr_gc_cause_ == kGcCauseBackground ||
      curr_gc_cause_ == kGcCauseExplicit))  { //a mutator is performing an allocation. do not involve service to get things done faster
    return;
  }

  int _expected_flag_value, _new_raised_flag;
  do {
    _expected_flag_value = 1;
    _new_raised_flag = 0;

  } while  (android_atomic_cas(_expected_flag_value, _new_raised_flag, &ipc_flag_raised_) != 0);

}

void IPCHeap::NotifyCompleteConcurrentTask(gc::gcservice::GC_SERVICE_TASK task) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    if(true)
      GCServiceClient::RemoveGCSrvcActiveRequest(task);

    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    meta_->conc_flag_ = 5;

    conc_req_cond_->Broadcast(self);

  }
}

bool IPCHeap::RunCollectorDaemon() {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    while(meta_->conc_flag_ != 1) {
      conc_req_cond_->Wait(self);
    }

  }

  gc::gcservice::GC_SERVICE_TASK _task_type = gc::gcservice::GC_SERVICE_TASK_NOP;
  if((meta_->gc_type_ & gc::gcservice::GC_SERVICE_TASK_CONC) > 0) {
    ConcurrentGC(self);
    meta_->conc_count_ = meta_->conc_count_ + 1;
    _task_type = gc::gcservice::GC_SERVICE_TASK_CONC;
  } else if((meta_->gc_type_ & gc::gcservice::GC_SERVICE_TASK_EXPLICIT) > 0) {
    ExplicitGC(false);
    meta_->explicit_count_ = meta_->explicit_count_ + 1;
    _task_type = gc::gcservice::GC_SERVICE_TASK_EXPLICIT;
  } else if((meta_->gc_type_ & gc::gcservice::GC_SERVICE_TASK_TRIM) > 0) {
    TrimHeap();
    _task_type = gc::gcservice::GC_SERVICE_TASK_TRIM;
  }

  NotifyCompleteConcurrentTask(_task_type);
  return true;
}



void IPCHeap::GrowForUtilization(collector::GcType gc_type, uint64_t gc_duration) {
  size_t bytes_allocated = local_heap_->GetLastGCSize();
  size_t target_size;
  if (gc_type != collector::kGcTypeSticky) {
    // Grow the heap for non sticky GC.
    target_size = bytes_allocated / local_heap_->GetTargetHeapUtilization();
    if (target_size > bytes_allocated + local_heap_->GetMaxFree()) {
      target_size = bytes_allocated + local_heap_->GetMaxFree();
    } else if (target_size < bytes_allocated + local_heap_->GetMinFree()) {
      target_size = bytes_allocated + local_heap_->GetMinFree();
    }
    local_heap_->SetNextGCType(collector::kGcTypeSticky);
  } else {
    // Based on how close the current heap size is to the target size, decide
    // whether or not to do a partial or sticky GC next.
    if (bytes_allocated + local_heap_->GetMinFree() <= local_heap_->GetMaxAllowedFootPrint()) {
      local_heap_->SetNextGCType(collector::kGcTypeSticky);
    } else {
      local_heap_->SetNextGCType(collector::kGcTypePartial);
    }

    // If we have freed enough memory, shrink the heap back down.
    if (bytes_allocated + local_heap_->GetMaxFree() < local_heap_->GetMaxAllowedFootPrint()) {
      target_size = bytes_allocated + local_heap_->GetMaxFree();
    } else {
      target_size = std::max(bytes_allocated, local_heap_->GetMaxAllowedFootPrint());
    }
  }

  if (!local_heap_->ignore_max_footprint_) {
    local_heap_->SetIdealFootprint(target_size);

    if (meta_->concurrent_gc_) {
      // Calculate when to perform the next ConcurrentGC.

      // Calculate the estimated GC duration.
      double gc_duration_seconds = NsToMs(gc_duration) / 1000.0;
      // Estimate how many remaining bytes we will have when we need to start the next GC.
      size_t remaining_bytes = local_heap_->GetAllocationRate() * gc_duration_seconds;
      remaining_bytes = std::max(remaining_bytes, kMinConcurrentRemainingBytes);
      if (UNLIKELY(remaining_bytes > local_heap_->GetMaxAllowedFootPrint())) {
        // A never going to happen situation that from the estimated allocation rate we will exceed
        // the applications entire footprint with the given estimated allocation rate. Schedule
        // another GC straight away.
        local_heap_->SetConcStartBytes(bytes_allocated);
      } else {
        // Start a concurrent GC when we get close to the estimated remaining bytes. When the
        // allocation rate is very high, remaining_bytes could tell us that we should start a GC
        // right away.
        local_heap_->SetConcStartBytes(std::max(local_heap_->GetMaxAllowedFootPrint() - remaining_bytes, bytes_allocated));
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
  ResetMetaDataUnlocked();

  DumpValues();
}




void AbstractIPCMarkSweep::ResetMetaDataUnlocked() { // reset data without locking
  // heap_meta_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  //  heap_meta_->freed_objects_ = 0;
  //  heap_meta_->freed_bytes_ = 0;
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
  IPC_MS_VLOG(ERROR) << "Dump AbstractIPCMarkSweep: " << "zygote_begin: "
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
  ScopedThreadStateChange tsc(thread, /*kWaitingPerformingGc*/kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(thread, *phase_mu_);
    while( meta_data_->gc_phase_ != phase) {
      phase_cond_->Wait(thread);
    }
  }
}
///////
//////
/////////////////////

byte* IPCMarkSweep::GetServerSpaceEnd(int index) const {
  return spaces_[index].base_end_;
}
byte* IPCMarkSweep::GetServerSpaceBegin(int index) const {
  return spaces_[index].base_;
}


byte* IPCMarkSweep::GetClientSpaceEnd(int index) const {
  return spaces_[index].client_end_;
}
byte* IPCMarkSweep::GetClientSpaceBegin(int index) const {
  return spaces_[index].client_base_;
}

template <class referenceKlass>
const referenceKlass* IPCMarkSweep::MapValueToServer(
    uint32_t raw_address_value,
    int32_t offset_) const {
  const byte* _raw_address = reinterpret_cast<const byte*>(raw_address_value);
  if(_raw_address == nullptr)
    return nullptr;

  for(int i = 0; i <= 2; i++) {
    if((_raw_address < GetClientSpaceEnd(i)) &&
        (_raw_address >= GetClientSpaceBegin(i))) {
      if(i == 0)
        return reinterpret_cast<const referenceKlass*>(_raw_address);
      return reinterpret_cast<const referenceKlass*>(_raw_address + offset_);
    }
  }

  LOG(FATAL) << "IPCServerMarkerSweep::MapValueToServer....0000--raw_Address_value:"
      << raw_address_value;
  return nullptr;
}
inline uint32_t IPCMarkSweep::GetClassAccessFlags(
    const mirror::Class* klass) const {
  // Check class is loaded or this is java.lang.String that has a
  // circularity issue during loading the names of its members
  uint32_t raw_access_flag_value =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
                                            mirror::Class::AcessFlagsOffset());
  return raw_access_flag_value;
}

// Returns true if the class is an interface.
bool IPCMarkSweep::IsInterfaceMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccInterface) != 0;
}

// Returns true if the class is declared final.
bool IPCMarkSweep::IsFinalMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccFinal) != 0;
}

bool IPCMarkSweep::IsFinalizableMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsFinalizable) != 0;
}

// Returns true if the class is abstract.
bool IPCMarkSweep::IsAbstractMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccAbstract) != 0;
}

// Returns true if the class is an annotation.
bool IPCMarkSweep::IsAnnotationMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccAnnotation) != 0;
}

// Returns true if the class is synthetic.
bool IPCMarkSweep::IsSyntheticMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccSynthetic) != 0;
}

bool IPCMarkSweep::IsReferenceMappedClass(
    const mirror::Class* klass) const {
  uint32_t _access_flags =  GetClassAccessFlags(klass);
  return (_access_flags & kAccClassIsReference) != 0;
}


bool IPCMarkSweep::IsWeakReferenceMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsWeakReference) != 0;
}


bool IPCMarkSweep::IsSoftReferenceMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccReferenceFlagsMask) == kAccClassIsReference;
}

bool IPCMarkSweep::IsFinalizerReferenceMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsFinalizerReference) != 0;
}

bool IPCMarkSweep::IsPhantomReferenceMappedClass(
    const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsPhantomReference) != 0;
}

bool IPCMarkSweep::IsPrimitiveMappedKlass(
    const mirror::Class* klass) const {
  int32_t type_raw_value =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
                                            mirror::Class::PrimitiveTypeOffset());
  Primitive::Type primitive_type =
      static_cast<Primitive::Type>(type_raw_value);
  return (primitive_type != Primitive::kPrimNot);
}

const mirror::Class* IPCMarkSweep::GetComponentTypeMappedClass(
    const mirror::Class* mapped_klass) const {
  uint32_t component_raw_value =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_klass),
                                            mirror::Class::ComponentTypeOffset());
  const mirror::Class* c = MapValueToServer<mirror::Class>(component_raw_value);
  return c;
}

bool IPCMarkSweep::IsMappedArrayClass(mirror::Class* klass) const {
  return (GetComponentTypeMappedClass(klass) != NULL);
}

bool IPCMarkSweep::IsObjectArrayMappedKlass(
    mirror::Class* klass) const {
  const mirror::Class* component_type = GetComponentTypeMappedClass(klass);
  if(component_type != NULL) {
    return (!IsPrimitiveMappedKlass(component_type));
  }
  return false;
}

int IPCMarkSweep::GetMappedClassType(const mirror::Class* klass) const {


  if(UNLIKELY(klass == GetCachedJavaLangClass()))
    return 2;

  if(IsMappedArrayClass(const_cast<mirror::Class*>(klass))) {
    if(IsObjectArrayMappedKlass(const_cast<mirror::Class*>(klass)))
      return 0;
    return 1;
  }

  return 3;
}

inline const mirror::Class* IPCMarkSweep::GetMappedObjectKlass(
    const mirror::Object* mapped_obj_parm,
    const int32_t offset_) {
  uint32_t _raw_class_value =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_obj_parm),
                                            mirror::Object::ClassOffset());
  const mirror::Class* c = MapValueToServer<mirror::Class>(_raw_class_value, offset_);

  return c;
}



size_t IPCMarkSweep::GetNumReferenceStaticFields(
    const mirror::Class* klass_ref) const {
  uint32_t raw_static_fields_number =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass_ref),
                                            mirror::Class::ReferenceStaticFieldsOffset());
  size_t mapped_value = static_cast<size_t>(raw_static_fields_number);
  return mapped_value;
}

size_t IPCMarkSweep::GetNumReferenceInstanceFields(
    const mirror::Class* klass_ref) const {
  uint32_t raw_instance_fields_number =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass_ref),
                                            mirror::Class::ReferenceInstanceFieldsOffset());
  size_t mapped_value = static_cast<size_t>(raw_instance_fields_number);
  return mapped_value;
}

const mirror::Class* IPCMarkSweep::GetSuperMappedClass(
    const mirror::Class* mapped_klass) {
  int32_t raw_super_klass =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_klass),
                                            mirror::Class::SuperClassOffset());
  const mirror::Class* c = MapValueToServer<mirror::Class>(raw_super_klass);
  return c;
}

const mirror::ArtField* IPCMarkSweep::RawClassGetInstanceField(
    const mirror::Class* klass, uint32_t i) {
  uint32_t instance_fields_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
                                            mirror::Class::GetInstanceFieldsOffset());
  const mirror::ObjectArray<mirror::ArtField>* instance_fields =
      MapValueToServer<mirror::ObjectArray<mirror::ArtField>>(instance_fields_raw);
  MemberOffset data_offset(mirror::Array::DataOffset(sizeof(mirror::Object*)).Int32Value()
                           + i * sizeof(mirror::Object*));
  uint32_t instance_field_raw =
      mirror::Object::GetRawValueFromObject(
          reinterpret_cast<const mirror::Object*>(instance_fields),
          data_offset);
  const mirror::ArtField* mapped_art_field =
      MapValueToServer<mirror::ArtField>(instance_field_raw);

  return mapped_art_field;
}


inline const mirror::ArtField* IPCMarkSweep::RawClassGetStaticField(
    const mirror::Class* klass, uint32_t i) {
  uint32_t static_fields_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
                                            mirror::Class::GetStaticFieldsOffset());
  const mirror::ObjectArray<mirror::ArtField>* static_fields =
      MapValueToServer<mirror::ObjectArray<mirror::ArtField>>(static_fields_raw);

  MemberOffset data_offset(mirror::Array::DataOffset(sizeof(mirror::Object*)).Int32Value()
                           + i * sizeof(mirror::Object*));


  uint32_t static_field_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(static_fields),
                                            data_offset);
  const mirror::ArtField* mapped_art_field =
      MapValueToServer<mirror::ArtField>(static_field_raw);


  return mapped_art_field;
}

template <typename Visitor>
inline void IPCMarkSweep::RawVisitFieldsReferences(
    const mirror::Object* obj,
    uint32_t ref_offsets,
    bool is_static,
    const Visitor& visitor) {
  //  if (LIKELY(ref_offsets != CLASS_WALK_SUPER)) {
    //    // Found a reference offset bitmap.  Mark the specified offsets.
    //#ifndef MOVING_COLLECTOR
    //    // Clear the class bit since we mark the class as part of marking the classlinker roots.
    //    DCHECK_EQ(mirror::Object::ClassOffset().Uint32Value(), 0U);
    //    ref_offsets &= (1U << (sizeof(ref_offsets) * 8 - 1)) - 1;
    //#endif
    //    while (ref_offsets != 0) {
      //      size_t right_shift = CLZ(ref_offsets);
      //      MemberOffset field_offset = CLASS_OFFSET_FROM_CLZ(right_shift);
  //      const mirror::Object* ref = obj->GetFieldObject<const mirror::Object*>(field_offset, false);
  //      visitor(obj, ref, field_offset, is_static);
  //      ref_offsets &= ~(CLASS_HIGH_BIT >> right_shift);
  //    }
  //  } else {
  //    // There is no reference offset bitmap.  In the non-static case,
  //    // walk up the class inheritance hierarchy and find reference
  //    // offsets the hard way. In the static case, just consider this
  //    // class.
  //    for (const mirror::Class* klass = is_static ? obj->AsClass() : obj->GetClass();
  //         klass != NULL;
  //         klass = is_static ? NULL : klass->GetSuperClassNoLock()) {
  //      size_t num_reference_fields = (is_static
  //                                     ? klass->NumReferenceStaticFields()
  //                                     : klass->NumReferenceInstanceFields());
  //      for (size_t i = 0; i < num_reference_fields; ++i) {
  //        mirror::ArtField* field = (is_static ? klass->GetStaticFieldNoLock(i)
  //                                   : klass->GetInstanceFieldNoLock(i));
  //        MemberOffset field_offset = field->GetOffset();
  //        const mirror::Object* ref =
  //            obj->GetFieldObject<const mirror::Object*>(field_offset, false);
  //        visitor(obj, ref, field_offset, is_static);
  //      }
  //    }
  //  }
  if (LIKELY(ref_offsets != CLASS_WALK_SUPER)) {
    // Found a reference offset bitmap.  Mark the specified offsets.
#ifndef MOVING_COLLECTOR
    // Clear the class bit since we mark the class as part of marking the classlinker roots.
    ref_offsets &= (1U << (sizeof(ref_offsets) * 8 - 1)) - 1;
#endif
    while (ref_offsets != 0) {
      size_t right_shift = CLZ(ref_offsets);
      MemberOffset field_offset = CLASS_OFFSET_FROM_CLZ(right_shift);
      uint32_t raw_fiel_value =
          mirror::Object::GetRawValueFromObject(obj, field_offset);
      const mirror::Object* mapped_field_object  =
          MapValueToServer<mirror::Object>(raw_fiel_value);
      visitor(obj, mapped_field_object, field_offset, is_static);
      ref_offsets &= ~(CLASS_HIGH_BIT >> right_shift);
    }
  } else {
    // There is no reference offset bitmap.  In the non-static case,
    // walk up the class inheritance hierarchy and find reference
    // offsets the hard way. In the static case, just consider this
    // class.
    for (const mirror::Class* klass = is_static ? down_cast<const mirror::Class*>(obj) : GetMappedObjectKlass(obj);
        klass != nullptr;
        klass = is_static ? nullptr : GetSuperMappedClass(klass)) {
      size_t num_reference_fields = (is_static
          ? GetNumReferenceStaticFields(klass)
              : GetNumReferenceInstanceFields(klass));
      for (size_t i = 0; i < num_reference_fields; ++i) {
        const mirror::ArtField* field = (is_static ? RawClassGetStaticField(klass, i)
            : RawClassGetInstanceField(klass, i));
        uint32_t field_word_value =
            mirror::Object::GetRawValueFromObject(field,
                                                  mirror::ArtField::OffsetOffset());
        MemberOffset field_offset(field_word_value);
        uint32_t raw_field_value =
            mirror::Object::GetRawValueFromObject(obj, field_offset);
        const mirror::Object* mapped_field_object =
            MapValueToServer<mirror::Object>(raw_field_value);
        visitor(obj, mapped_field_object, field_offset, is_static);
      }
    }
  }
}

template <typename Visitor>
inline void IPCMarkSweep::RawVisitInstanceFieldsReferences(
    const mirror::Class* klass,
    const mirror::Object* obj,
    const Visitor& visitor) {
  //  RawVisitFieldsReferences(obj, klass->GetReferenceInstanceOffsets(), false, visitor);
  const int32_t reference_offsets =
      mirror::Class::GetReferenceInstanceOffsetsOffset().Int32Value();
  const byte* raw_addr = reinterpret_cast<const byte*>(klass) + reference_offsets;
  const int32_t* word_addr = reinterpret_cast<const int32_t*>(raw_addr);
  uint32_t reference_instance_offsets_val = *word_addr;
  RawVisitFieldsReferences(obj, reference_instance_offsets_val, false, visitor);
}

template <typename Visitor>
inline void IPCMarkSweep::RawVisitClassReferences(
    const mirror::Class* klass, const mirror::Object* obj,
    const Visitor& visitor)  {
  RawVisitInstanceFieldsReferences(klass, obj, visitor);
  RawVisitStaticFieldsReferences(down_cast<const mirror::Class*>(obj), visitor);

}
template <typename Visitor>
inline void IPCMarkSweep::RawVisitStaticFieldsReferences(
    const mirror::Class* klass,
    const Visitor& visitor) {
  //  RawVisitFieldsReferences(klass, klass->GetReferenceStaticOffsets(), true, visitor);
  MemberOffset reference_static_offset = mirror::Class::ReferenceStaticOffset();
  uint32_t _raw_value_offsets =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
                                            reference_static_offset);
  RawVisitFieldsReferences(klass, _raw_value_offsets, true, visitor);
}

template <typename Visitor>
inline void IPCMarkSweep::RawVisitObjectArrayReferences(
    const mirror::ObjectArray<mirror::Object>* mapped_arr,
    const Visitor& visitor) {

  //  const size_t length = static_cast<size_t>(mapped_arr->GetLength());
  //  for (size_t i = 0; i < length; ++i) {
  //    const mirror::Object* element = mapped_arr->GetWithoutChecksNoLocks(static_cast<int32_t>(i));
  //    const size_t width = sizeof(mirror::Object*);
  //    MemberOffset offset(i * width + mirror::Array::DataOffset(width).Int32Value());
  //    visitor(mapped_arr, element, offset, false);
  //  }

  uint32_t _length_read =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_arr),
                                            mirror::Array::LengthOffset());

  const size_t length =
      static_cast<size_t>(_length_read);

  for (size_t i = 0; i < length; ++i) {//we do not need to map the element from an array
    const size_t width = sizeof(mirror::Object*);
    MemberOffset offset(i * width + mirror::Array::DataOffset(width).Int32Value());
    uint32_t _data_read =
        mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_arr),
                                              offset);
    const mirror::Object* element_content =
        MapValueToServer<mirror::Object>(_data_read);

    visitor(mapped_arr, element_content, offset, false);
  }

}


inline bool IPCMarkSweep::RawIsMarked(const Object* object)  {
  if (IsImmune(object)) {
    return true;
  }
  DCHECK(current_mark_bitmap_ != NULL);
  if (current_mark_bitmap_->HasAddress(object)) {
    return current_mark_bitmap_->Test(object);
  }
  return _temp_heap_beetmap->TestNoLock(object);
}



inline bool IPCMarkSweep::IsMappedObjectMarked(
    const mirror::Object* object)  {

  return RawIsMarked(object);
  //  if (IsMappedObjectImmuned(object)) {
  //    return true;
  //  }
  //  const byte* casted_param = reinterpret_cast<const byte*>(object);
  //  int matching_index = -1;
  //  for(int i = 0; i <= 2; i++) {
  //    if((casted_param <  GetServerSpaceEnd(i)) &&
  //        (casted_param >= GetServerSpaceBegin(i))) {
  //      matching_index = i;
  //    }
  //  }
  //
  //  if(matching_index == -1) {
  //    LOG(FATAL) << "TestMappedBitmap..." << object;
  //    return false;
  //  }
  //
  // // marked_spaces_count_prof_[matching_index] += 1;
  //  accounting::SharedServerSpaceBitmap* obj_beetmap = current_mark_bitmap_;
  //  bool _resultTestFlag = false;
  //  bool _resultHasAddress = obj_beetmap->HasAddress(object);
  //  if(!_resultHasAddress) {
  //    for (const auto& beetmap : mark_bitmaps_) {
  //      _resultHasAddress = beetmap->HasAddress(object);
  //      if(_resultHasAddress) {
  //        obj_beetmap = beetmap;
  //        break;
  //      }
  //    }
  //  }
  //
  //
  //
  //  if(_resultHasAddress) {
  //    _resultTestFlag = obj_beetmap->Test(object);
  //  }
  //
  //  return (_resultHasAddress && _resultTestFlag);
}

bool IPCMarkSweep::IsMappedReferentEnqueued(
    const mirror::Object* mapped_ref) const {
  const int32_t pending_next_raw_value =
      mirror::Object::GetRawValueFromObject(
          reinterpret_cast<const mirror::Object*>(mapped_ref),
          MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_));
  const mirror::Object* mapped_pending_next =
      MapValueToServer<mirror::Object>(pending_next_raw_value);

  return (mapped_pending_next != nullptr);
}


//void IPCMarkSweep::RawEnqPendingReference(mirror::Object* ref,
//    mirror::Object** list) {
//  uint32_t* head_pp = reinterpret_cast<uint32_t*>(list);
//  const mirror::Object* mapped_head = MapValueToServer<mirror::Object>(*head_pp);
//  if(mapped_head == NULL) {
//    // 1 element cyclic queue, ie: Reference ref = ..; ref.pendingNext = ref;
//    SetClientFieldValue(ref,
//        MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), ref);
//    *head_pp = MapReferenceToValueClient(ref);
//  } else {
//    int32_t pending_next_raw_value =
//        mirror::Object::GetRawValueFromObject(
//            reinterpret_cast<const mirror::Object*>(mapped_head),
//            MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_));
//    mirror::Object* mapped_pending_next =
//        const_cast<mirror::Object*>(MapValueToServer<mirror::Object>(pending_next_raw_value));
//    SetClientFieldValue(ref, MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), mapped_head);
//    SetClientFieldValue(mapped_pending_next, MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), ref);
//  }
//}


template <class referenceKlass>
uint32_t IPCMarkSweep::MapReferenceToValueClient(
    const referenceKlass* mapped_reference) const {
  if(mapped_reference == nullptr)
    return 0U;
  const byte* _raw_address = reinterpret_cast<const byte*>(mapped_reference);
  for(int i = 0; i <= 2; i++) {
    if((_raw_address < GetServerSpaceEnd(i)) &&
        (_raw_address >= GetServerSpaceBegin(i))) {
      if(i == 0)
        return reinterpret_cast<uint32_t>(_raw_address);
      return reinterpret_cast<uint32_t>(_raw_address - 0);
    }
  }

  LOG(FATAL) << "IPCServerMarkerSweep::MapReferenceToValueClient....0000--raw_Address_value:"
      << mapped_reference;
  return 0U;
}

void IPCMarkSweep::SetClientFieldValue(const mirror::Object* mapped_object,
                                       MemberOffset field_offset, const mirror::Object* mapped_ref_value) {
  //  uint32_t raw_field_value = reinterpret_cast<uint32_t>(mapped_ref_value);
  byte* raw_addr =
      const_cast<byte*>(reinterpret_cast<const byte*>(mapped_object)) +
      field_offset.Int32Value();
  uint32_t* word_addr = reinterpret_cast<uint32_t*>(raw_addr);
  uint32_t eq_client_value =
      MapReferenceToValueClient<mirror::Object>(mapped_ref_value);
  *word_addr = eq_client_value;
}

void IPCMarkSweep::RawEnqPendingReference(mirror::Object* ref,
                                          mirror::Object** list) {
  mirror::Object* list_content = *list;
  if(list_content == NULL) {
    SetClientFieldValue(ref, MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), ref);
    *list = reinterpret_cast<mirror::Object*>(MapReferenceToValueClient(ref));
  } else {
    MemberOffset off(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_);

    int32_t head_int_value = mirror::Object::GetRawValueFromObject(
        reinterpret_cast<const mirror::Object*>(list_content), off);
    mirror::Object* mapped_head =
        const_cast<mirror::Object*>(MapValueToServer<mirror::Object>(head_int_value));
    SetClientFieldValue(ref, off, mapped_head);
    SetClientFieldValue(list_content, off, ref);

    //    mirror::Object* head =
    //        (*list)->GetFieldObject<mirror::Object*>(off, false);
    //    ref->SetFieldObject(off, head, false);
    //    (*list)->SetFieldObject(off, ref, false);
  }


  //  uint32_t* head_pp = reinterpret_cast<uint32_t*>(list);
  //  const mirror::Object* mapped_head = MapValueToServer<mirror::Object>(*head_pp);
  //  if(mapped_head == NULL) {
  //    // 1 element cyclic queue, ie: Reference ref = ..; ref.pendingNext = ref;
  //    SetClientFieldValue(ref, MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), ref);
  //    *head_pp = MapReferenceToValueClient(ref);
  //  } else {
  //    int32_t pending_next_raw_value =
  //        mirror::Object::GetRawValueFromObject(
  //            reinterpret_cast<const mirror::Object*>(mapped_head),
  //            MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_));
  //    mirror::Object* mapped_pending_next =
  //        const_cast<mirror::Object*>(MapValueToServer<mirror::Object>(pending_next_raw_value));
  //    SetClientFieldValue(ref, MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), mapped_head);
  //    SetClientFieldValue(mapped_pending_next,
  //        MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_pendingNext_offset_), ref);
  //  }
}


// Process the "referent" field in a java.lang.ref.Reference.  If the
// referent has not yet been marked, put it on the appropriate list in
// the heap for later processing.
inline void IPCMarkSweep::RawDelayReferenceReferent(const mirror::Class* klass,
                                                    mirror::Object* obj) {

  //Object* mapped_referent = ipc_heap_->local_heap_->GetReferenceReferent(obj);

  uint32_t referent_raw_value =
      mirror::Object::GetVolatileRawValueFromObject(
          reinterpret_cast<const mirror::Object*>(obj),
          MemberOffset(ipc_heap_->meta_->reference_offsets_.reference_referent_offset_));
  const mirror::Object* mapped_referent =
      MapValueToServer<mirror::Object>(referent_raw_value);
  if (mapped_referent != NULL && !RawIsMarked(mapped_referent)) {//TODO: Implement ismarked /*IsMappedObjectMarked*/
    //Thread* self = Thread::Current();
    // TODO: Remove these locks, and use atomic stacks for storing references?
    // We need to check that the references haven't already been enqueued since we can end up
    // scanning the same reference multiple times due to dirty cards.
    if (IsSoftReferenceMappedClass(klass)) {
      //MutexLock mu(self, *heap_->GetSoftRefQueueLock());
      if (!IsMappedReferentEnqueued(obj)) {
        RawEnqPendingReference(obj, GetSoftReferenceList());
      }
    } else if (IsWeakReferenceMappedClass(klass)) {
      //MutexLock mu(self, *heap_->GetWeakRefQueueLock());
      if (!IsMappedReferentEnqueued(obj)) {
        RawEnqPendingReference(obj, GetWeakReferenceList());
      }
    } else if (IsFinalizerReferenceMappedClass(klass)) {
      //MutexLock mu(self, *heap_->GetFinalizerRefQueueLock());
      if (!IsMappedReferentEnqueued(obj)) {
        RawEnqPendingReference(obj, GetFinalizerReferenceList());
      }
    } else if (IsPhantomReferenceMappedClass(klass)) {
      //MutexLock mu(self, *heap_->GetPhantomRefQueueLock());
      if (!IsMappedReferentEnqueued(obj)) {
        RawEnqPendingReference(obj, GetPhantomReferenceList());
      }
    } else {
      LOG(FATAL) << "Invalid reference type " //<< PrettyClass(klass)
          << " " << std::hex << klass->GetAccessFlags();
    }

    //   // cashed_stats_client_.reference_count_ += 1;
    //    //Thread* self = Thread::Current();
    //    bool is_enqueued_object = IsMappedReferentEnqueued(obj);
    //    if(IsSoftReferenceMappedClass(klass)) {
    //      if(!is_enqueued_object) {
    //        RawEnqPendingReference(const_cast<mirror::Object*>(obj),
    //            &(cashed_references_record_->soft_reference_list_));
    //      }
    //    } else if(IsWeakReferenceMappedClass(klass)) {
    //      if(!is_enqueued_object) {
    //        RawEnqPendingReference(const_cast<mirror::Object*>(obj),
    //            &(cashed_references_record_->weak_reference_list_));
    //      }
    //    } else if(IsFinalizerReferenceMappedClass(klass)) {
    //      if(!is_enqueued_object) {
    //        RawEnqPendingReference(const_cast<mirror::Object*>(obj),
    //            &(cashed_references_record_->finalizer_reference_list_));
    //      }
    //    } else if(IsPhantomReferenceMappedClass(klass)) {
    //      if(!is_enqueued_object) {
    //        RawEnqPendingReference(const_cast<mirror::Object*>(obj),
    //            &(cashed_references_record_->phantom_reference_list_));
    //      }
    //    } else {
    //      LOG(FATAL) << "Invalid reference IPCServerMarkerSweep::ServerDelayReferenceReferent "
    //                  << ", klass: " << klass
    //                  << ", hex..."  << std::hex << GetClassAccessFlags(klass);
    //    }
  }
}


template <typename Visitor>
inline void IPCMarkSweep::RawVisitOtherReferences(const mirror::Class* klass,
                                                  const mirror::Object* obj,
                                                  const Visitor& visitor) {
  RawVisitInstanceFieldsReferences(klass, obj, visitor);
}

class RawMarkObjectVisitor {
 public:
  explicit RawMarkObjectVisitor(IPCMarkSweep* const raw_mark_sweep)
  ALWAYS_INLINE : mark_sweep_(raw_mark_sweep) {}

  // TODO: Fixme when anotatalysis works with visitors.
  void operator()(const Object* /* obj */, const Object* ref,
                  MemberOffset& /* offset */, bool /* is_static */) const ALWAYS_INLINE
                  NO_THREAD_SAFETY_ANALYSIS {
    //    if (kCheckLocks) {
    //      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
    //      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
    //    }
    //mark_sweep_->RawMarkObject(ref);
    if(ref != NULL)
      mark_sweep_->RawMarkObject(ref);
  }

 private:
  IPCMarkSweep* const mark_sweep_;
};


inline void IPCMarkSweep::RawScanObjectVisit(const mirror::Object* obj) {
  RawMarkObjectVisitor visitor(this);
  mirror::Class* klass = const_cast<mirror::Class*>(GetMappedObjectKlass(obj, 0));//obj->GetClass();
  if (UNLIKELY(IsMappedArrayClass(klass))) {

    if (IsObjectArrayMappedKlass(klass)) {
      RawVisitObjectArrayReferences(down_cast<const mirror::ObjectArray<mirror::Object>*>(obj), visitor);
      //VisitObjectArrayReferences(obj->AsObjectArray<mirror::Object>(), visitor);
    }
  } else if (UNLIKELY(klass == GetCachedJavaLangClass())) {

    RawVisitClassReferences(klass, obj, visitor);
  } else {
    RawVisitOtherReferences(klass, obj, visitor);
    if (UNLIKELY(IsReferenceMappedClass(klass))) {
      RawDelayReferenceReferent(klass, const_cast<mirror::Object*>(obj));
    }
  }

  //  RawMarkObjectVisitor visitor(this);
  //  mirror::Class* mapped_klass = obj->GetClass();//GetMappedObjectKlass(obj, 0);
  //
  //  if (UNLIKELY(mapped_klass->IsArrayClass())) {
  //    if (mapped_klass->IsObjectArrayClass()) {
  //      RawVisitObjectArrayReferences(obj->AsObjectArray<mirror::Object>(), visitor);
  //    }
  //  } else if (UNLIKELY(mapped_klass == GetCachedJavaLangClass())) {
  //    RawVisitClassReferences(mapped_klass, obj, visitor);
  //  } else {
  //    RawVisitOtherReferences(mapped_klass, obj, visitor);
  //    if (UNLIKELY(mapped_klass->IsReferenceClass())) {
  //      RawDelayReferenceReferent(mapped_klass, const_cast<mirror::Object*>(obj));
  //    }
  //  }

  //  int mapped_class_type = GetMappedClassType(mapped_klass);
  //  if (UNLIKELY(mapped_class_type < 2)) {
  //    //cashed_stats_client_.array_count_ += 1;
  //    //android_atomic_add(1, &(array_count_));
  //    if(mapped_class_type == 0) {
  //      RawVisitObjectArrayReferences(
  //        down_cast<const mirror::ObjectArray<mirror::Object>*>(obj),
  //                                                                    visitor);
  //    }
  //  } else if (UNLIKELY(mapped_class_type == 2)) {
  //    //cashed_stats_client_.class_count_ += 1;
  //    RawVisitClassReferences(mapped_klass, obj, visitor);
  //  } else if (UNLIKELY(mapped_class_type == 3)) {
  //    //cashed_stats_client_.other_count_ += 1;
  //    RawVisitOtherReferences(mapped_klass, obj, visitor);
  //    if(UNLIKELY(IsReferenceMappedClass(mapped_klass))) {
  //      //is_reference_class_cnt_++;
  //      RawDelayReferenceReferent(mapped_klass,
  //                                  const_cast<mirror::Object*>(obj));
  //    }
  //  }
}


template <class referenceKlass>
inline const referenceKlass* IPCMarkSweep::MapReferenceToClientChecks(
    const referenceKlass* const ref_parm) {
  if(ref_parm == nullptr)
    return nullptr;
  const byte* casted_param = reinterpret_cast<const byte*>(ref_parm);
  for(int i = 0; i <= 2; i++) {
    if((casted_param < GetServerSpaceEnd(i)) &&
        (casted_param >= GetServerSpaceBegin(i))) {
      if(i == 0)
        return ref_parm;
      return reinterpret_cast<const referenceKlass*>(casted_param - 0);
    }
  }
  LOG(FATAL) << "..... MapReferenceToClientChecks: .." << ref_parm;
  return ref_parm;
}

inline void IPCMarkSweep::RawMarkObjectNonNull(const mirror::Object* obj) {
  DCHECK(obj != nullptr);

  //  if(!IsMappedObjectToServer<mirror::Object>(obj)) {
  //    LOG(FATAL) << "IPCServerMarkerSweep::MarkObjectNonNull.." << obj;
  //  }
  if (IsMappedObjectImmuned(obj)) {
    return;
  }

  // Try to take advantage of locality of references within a space, failing this find the space
  // the hard way.
  bool _found = true;
  accounting::SPACE_BITMAP* object_bitmap = current_mark_bitmap_;
  if (UNLIKELY(!object_bitmap->HasAddress(obj))) {
    accounting::SPACE_BITMAP* new_bitmap =
        _temp_heap_beetmap->GetContinuousSpaceBitmap(obj);
    if (LIKELY(new_bitmap != NULL)) {
      object_bitmap = new_bitmap;
      _found = true;
    }
  }
  if(!_found) {
    LOG(FATAL) << "Object belongs to no Beetmaps.." << obj;
  }

  if(true) {
    // This object was not previously marked.
    if(!object_bitmap->Test(obj)) {
      object_bitmap->Set(obj);
      //TODO:: check the need to resize the mark stack here
      //const mirror::Object* oject_pushed = MapReferenceToClientChecks(obj);
      //      if(!BelongsToOldHeap<mirror::Object>(oject_pushed)) {
      //        LOG(FATAL) << "MAPPINGERROR: XXXXXXX does not belong to Heap XXXXXXXXX " << oject_pushed ;
      //      }
      //pushed_back_to_stack_++;
      //      LOG(ERROR) << "MarkObjectNonNull..object stack: " << oject_pushed;
      mark_stack_->PushBack(const_cast<mirror::Object*>(obj));
    } else {
      // LOG(FATAL) << "IPCServerMarkerSweep::MarkObjectNonNull..object test failed.." << obj;
    }
  }
}

inline void IPCMarkSweep::RawMarkObject(const mirror::Object* obj) {
  if (obj != NULL) {
    RawMarkObjectNonNull(obj);
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
    if(ref != NULL)
      MarkSweep::MarkObjectCallbackNoLock(ref, mark_sweep_);
    //mark_sweep_->MarkObject(ref);
  }

 private:
  IPCMarkSweep* const mark_sweep_;
};

static int first_timer_runner_ = false;

void IPCMarkSweep::RawObjectScanner(void) {
  timings_.StartSplit("ProcessMarkStack");
  spaces_[0].client_base_ =
      heap_meta_->image_space_begin_;
  spaces_[0].client_end_ =
      heap_meta_->image_space_end_;
  spaces_[0].base_ = heap_meta_->image_space_begin_;
  spaces_[0].base_end_ = heap_meta_->image_space_end_;

  spaces_[1].client_base_ =
      heap_meta_->zygote_begin_;
  spaces_[1].client_end_ =
      heap_meta_->zygote_end_;
  spaces_[1].base_ = heap_meta_->zygote_begin_;
  spaces_[1].base_end_ = heap_meta_->zygote_end_;


  spaces_[2].client_base_ =
      ipc_heap_->local_heap_->GetAllocSpace()->Begin();
  spaces_[2].client_end_ = spaces_[2].client_base_ +
      ipc_heap_->local_heap_->GetAllocSpace()->Capacity();
  spaces_[2].base_ = ipc_heap_->local_heap_->GetAllocSpace()->Begin();
  spaces_[2].base_end_ =
      spaces_[2].base_ + ipc_heap_->local_heap_->GetAllocSpace()->Capacity();

  _temp_heap_beetmap = ipc_heap_->local_heap_->GetMarkBitmap();

  if(!first_timer_runner_) {
    first_timer_runner_ = true;
    for(int i = 0; i <= 2; i++) {
      LOG(ERROR) << StringPrintf("X...space[%d]  --> client-start=%p, client-end=%p", i,
                                 spaces_[i].client_base_, spaces_[i].client_end_);
    }
    for(int i = 0; i <= 2; i++) {
      LOG(ERROR) << StringPrintf("X...space[%d]  --> server-start=%p, server-end=%p", i,
                                 spaces_[i].base_, spaces_[i].base_end_);
    }
  }

  const mirror::Object* popped_oject = NULL;
  //ClientMarkObjectVisitor visitor(this);

  for (;;) {
    if (mark_stack_->IsEmpty()) {
      break;
    }
    popped_oject = mark_stack_->PopBack();
    //ScanObject(popped_oject);
    RawScanObjectVisit(popped_oject);
  }
}

/////////////////////
/////
////
////

////

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



inline void IPCMarkSweep::ScanObjectVisitVerifyArray(const mirror::Object* obj,
                                                     accounting::BaseHeapBitmap* heap_beetmap) {
  DCHECK(obj != NULL);
  if (kIsDebugBuild && !IsMarked(obj)) {
    heap_->DumpSpaces();
    LOG(FATAL) << "Scanning unmarked object " << obj;
  }
  ArraysVerifierScan(obj, (void*)heap_beetmap);
}



static void IPCSweepExternalScanObjectVisit(mirror::Object* obj,
                                            void* args) {
  IPCMarkSweep* param =
      reinterpret_cast<IPCMarkSweep*>(args);
  //uint32_t calc_offset = (param->offset_ / sizeof(Object*));
  //  uint32_t* calc_offset = reinterpret_cast<uint32_t*>(calculated_offset);


  param->ScanObjectVisitVerifyArray(obj, IPCMarkSweep::_temp_heap_beetmap);
  //param->ClientVerifyObject(obj);
}

IPCMarkSweep::IPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
                           const std::string& name_prefix) :
        AbstractIPCMarkSweep(ipcHeap, is_concurrent),
        MarkSweep(ipcHeap->local_heap_, is_concurrent,
                  &meta_data_->cashed_references_,
                  &meta_data_->cashed_stats_,
                  name_prefix) {
  meta_data_->gc_type_ = collector::kGcTypeFull;
  //  time_stats_ = &meta_data_->time_stats_;
  IPC_MS_VLOG(ERROR) << "############ Initializing IPC: " << GetName() <<
      "; gcType: " << GetGcType() << "; conc:" << IsConcurrent() << " ###########";
}



void IPCMarkSweep::ClearMarkHolders(void) {
  IPC_MS_VLOG(ERROR) << "IPCMarkSweep::ClearMarkHolders..............";
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
  IPC_MS_VLOG(ERROR) << "__________ IPCMarkSweep::PreInitializePhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  ipc_heap_->SetCurrentCollector(this);
  SetCachedJavaLangClass(Class::GetJavaLangClass());
}


void IPCMarkSweep::InitializePhase(void) {
  timings_.Reset();
  base::TimingLogger::ScopedSplit split("InitializePhase", &timings_);
  PreInitializePhase();

  art::Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_INIT);
  IPC_MS_VLOG(ERROR) << "_______IPCMarkSweep::InitializePhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;

  mark_stack_ = ipc_heap_->local_heap_->GetHeapMarkStack();
  SetImmuneRange(nullptr, nullptr);
  SetSoftReferenceList(nullptr);
  SetWeakReferenceList(nullptr);
  SetFinalizerReferenceList(nullptr);
  SetPhantomReferenceList(nullptr);
  SetClearedReferenceList(nullptr);




  ResetCollectorStats();

  large_object_test_ = 0;
  large_object_mark_ = 0;
  classes_marked_ = 0;
  overhead_time_ = 0;
  work_chunks_created_ = 0;
  work_chunks_deleted_ = 0;


  FindDefaultMarkBitmap();
  IPC_MS_VLOG(ERROR) << "_______IPCMarkSweep::InitializePhase. going for GCVerification: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  if(false)
    ipc_heap_->local_heap_->PreGcVerification(this);
}

void IPCMarkSweep::ApplyTrimming(void) {


  //  LOG(ERROR) << "IPCMarkSweep::ApplyTrimming..gcType:"<< GetGcType();

  ipc_heap_->CheckTrimming(GetGcType(), GetDurationNs());
}

void IPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_FINISH);
  IPC_MS_VLOG(INFO) << "_______IPCMarkSweep::FinishPhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  MarkSweep::FinishPhase();


  // IncTotalTimeNs(GetDurationNs());
  // IncTotalPausedTimeNs(std::accumulate(GetPauseTimes().begin(), GetPauseTimes().end(), 0,
  //                                          std::plus<uint64_t>()));
  // IncTotalFreedObjects(GetFreedObjects() + GetFreedLargeObjects());
  // IncTotalFreedBytes(GetFreedBytes() + GetFreedLargeObjectBytes());

  ipc_heap_->ResetCurrentCollector(this);
  //ipc_heap_->AssignNextGCType();
}

void IPCMarkSweep::FindDefaultMarkBitmap(void) {
  //current_mark_bitmap_ = SetMarkBitmap();
  MarkSweep::FindDefaultMarkBitmap();
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
  IPC_MS_VLOG(INFO) << "_______IPCMarkSweep::MarkConcurrentRoots. starting: _______ " <<
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
//void IPCMarkSweep::PostMarkingPhase(void){
////  Thread* currThread = Thread::Current();
////  ThreadList* thread_list = Runtime::Current()->GetThreadList();
////  IPC_MS_VLOG(ERROR) << "IPCMarkSweep::PostMarkingPhase: SSSSSSSSSSSSSSSSSSUspended the "
////      "threads: " << currThread->GetTid();
////  if(false) {
////    thread_list->SuspendAll();
////    IPC_MS_VLOG(ERROR) << "SSSSSSSSSSSSSSSSSSUspended the threads";
////    thread_list->ResumeAll();
////  }
////  {
////    ReaderMutexLock mu_mutator(currThread, *Locks::mutator_lock_);
////    WriterMutexLock mu_heap_bitmap(currThread, *Locks::heap_bitmap_lock_);
////    MarkReachableObjects();
////  }
//
//}

//void IPCMarkSweep::IPCMarkRootsPhase(void) {
//  base::TimingLogger::ScopedSplit split("MarkingPhase", &timings_);
//  Thread* currThread = Thread::Current();
//  UpdateGCPhase(currThread, space::IPC_GC_PHASE_ROOT_MARK);
//  IPC_MS_VLOG(ERROR) << "_______IPCMarkSweep::MarkingPhase. starting: _______ " <<
//      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
//
//  BindBitmaps();
//  FindDefaultMarkBitmap();
//
//  // Process dirty cards and add dirty cards to mod union tables.
//  ipc_heap_->local_heap_->ProcessCards(timings_);
//
//  // Need to do this before the checkpoint since we don't want any threads to add references to
//  // the live stack during the recursive mark.
//  timings_.NewSplit("SwapStacks");
//  //Fizo: here we can make the server gets which one is the right stack
//  ipc_heap_->local_heap_->SwapStacks();
//
//  WriterMutexLock mu(currThread, *Locks::heap_bitmap_lock_);
//  if (Locks::mutator_lock_->IsExclusiveHeld(currThread)) {
//    // If we exclusively hold the mutator lock, all threads must be suspended.
//    MarkRoots();
//    IPC_MS_VLOG(ERROR) << " ##### IPCMarkSweep::MarkingPhase. non concurrent marking: _______ " <<
//        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
//  } else { //concurrent
//    IPC_MS_VLOG(ERROR) << " ##### IPCMarkSweep::MarkingPhase.  concurrent marking: _______ " <<
//        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
//    MarkThreadRoots(currThread);
//    // At this point the live stack should no longer have any mutators which push into it.
//    MarkNonThreadRoots();
//  }
//  live_stack_freeze_size_ = ipc_heap_->local_heap_->GetLiveStack()->Size();
//  MarkConcurrentRoots();
//
//  ipc_heap_->local_heap_->UpdateAndMarkModUnion(this, timings_, GetGcType());
//
//  MarkReachableObjects();
//  MarkReachableObjects();
//}

//void IPCMarkSweep::IPCMarkReachablePhase(void) {
//
//}
void IPCMarkSweep::MarkingPhase(void) {
  base::TimingLogger::ScopedSplit split("MarkingPhase", &timings_);
  Thread* currThread = Thread::Current();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_ROOT_MARK);
  IPC_MS_VLOG(INFO) << "_______IPCMarkSweep::MarkingPhase. starting: _______ " <<
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
    IPC_MS_VLOG(INFO) << " ##### IPCMarkSweep::MarkingPhase. non concurrent marking: _______ " <<
        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  } else { //concurrent
    IPC_MS_VLOG(INFO) << " ##### IPCMarkSweep::MarkingPhase.  concurrent marking: _______ " <<
        currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
    MarkThreadRoots(currThread);
    // At this point the live stack should no longer have any mutators which push into it.
    MarkNonThreadRoots();
  }
  live_stack_freeze_size_ = ipc_heap_->local_heap_->GetLiveStack()->Size();
  MarkConcurrentRoots();

  ipc_heap_->local_heap_->UpdateAndMarkModUnion(this, timings_, GetGcType());

  MarkReachableObjects();

}

void IPCMarkSweep::RequestAppSuspension(accounting::BaseHeapBitmap* heap_beetmap) {
  //ThreadList* thread_list = Runtime::Current()->GetThreadList();
  Thread* currThread = Thread::Current();
  //thread_list->SuspendAll();
  //IPC_MS_VLOG(ERROR) << "SSS Suspended app threads to handshake with service process SS ";


  BlockForGCPhase(currThread, space::IPC_GC_PHASE_MARK_RECURSIVE);

  //IPC_MS_VLOG(ERROR) << "SSS Suspended app threads to handshake with service process SS ";
  //mark_stack_->OperateOnStack(IPCSweepExternalScanObjectVisit, this);
  //thread_list->ResumeAll();
  IPC_MS_VLOG(INFO) << "IPCMarkSweep client changes phase from: " << meta_data_->gc_phase_ <<
      ", stack_size = " << mark_stack_->Size();
  if(false) {
    _temp_heap_beetmap = heap_beetmap;//ipc_heap_->local_heap_->GetMarkBitmap();
    mark_stack_->OperateOnStack(IPCSweepExternalScanObjectVisit, this);
  }
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_CONC_MARK);

}

void IPCMarkSweep::HandshakeIPCSweepMarkingPhase(accounting::BaseHeapBitmap* heap_beetmap) {
  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(INFO) << " #### IPCMarkSweep::HandshakeMarkingPhase. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  //ipc_heap_->local_heap_->DumpSpaces();
  UpdateGCPhase(currThread, space::IPC_GC_PHASE_MARK_REACHABLES);
  int _synchronized = 0;
  if((_synchronized = android_atomic_release_load(&(server_synchronize_))) == 1) {
    //here we are doing the mark reachable on the server side
    //if(true) {
    base::TimingLogger::ScopedSplit split("RecursiveMark", &timings_);
    timings_.StartSplit("ProcessMarkStack");
    //}
    RequestAppSuspension();
    //_temp_heap_beetmap = heap_beetmap;
    //RawObjectScanner();
  } else {
    LOG(FATAL) << "DANGER::::::::#### IPCMarkSweep:: ipc_heap_->ipc_flag_raised_ was zero";
    IPC_MS_VLOG(ERROR) << " #### IPCMarkSweep:: ipc_heap_->ipc_flag_raised_ was zero";
    UpdateGCPhase(currThread, space::IPC_GC_PHASE_CONC_MARK);
  }

  IPC_MS_VLOG(INFO) << "      to : " << meta_data_->gc_phase_;
}

//void IPCMarkSweep::ProcessMarkStack(bool paused) {
//  Thread* currThread = Thread::Current();
//  IPC_MS_VLOG(ERROR) << "_______IPCMarkSweep::ProcessMarkStack. starting: _______ " <<
//      currThread->GetTid() << "... MarkStackSize=" << mark_stack_->Size();
//  MarkSweep::ProcessMarkStack(paused);
//}

// Scan anything that's on the mark stack.
void IPCMarkSweep::ProcessMarkStack(bool paused) {
  timings_.StartSplit("ProcessMarkStack");
  //  Thread* currThread = Thread::Current();
  //  IPC_MS_VLOG(ERROR) << "_______IPCMarkSweep::ProcessMarkStack. starting: _______ " <<
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
  IPC_MS_VLOG(INFO) << "_______IPCMarkSweep::MarkReachableObjects. starting: _______ " <<
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
  if(false)
    RecursiveMark();

  HandshakeIPCSweepMarkingPhase(ipc_heap_->local_heap_->GetMarkBitmap());
  // Recursively mark all the non-image bits set in the mark bitmap.
  //if(false)

  //_temp_heap_beetmap = ipc_heap_->local_heap_->GetMarkBitmap();
  //
  //MarkSweep::RecursiveMark();
  //MarkSweep::MarkReachableObjects();
  IPC_MS_VLOG(INFO) << " >>IPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

// Populates the mark stack based on the set of marked objects and
// recursively marks until the mark stack is emptied.
void IPCMarkSweep::RecursiveMark() {
  //MarkSweep::RecursiveMark();
  base::TimingLogger::ScopedSplit split("RecursiveMark", &timings_);
  RawObjectScanner();
  //ProcessMarkStack(false);
  //MarkSweep::RecursiveMark();
}

void IPCMarkSweep::ProcessMarkStackParallel(size_t thread_count) {
  Thread* self = Thread::Current();
  IPC_MS_VLOG(INFO) << "IPCMarkSweep::ProcessMarkStackParallel: " << thread_count
      << "; tid:" << self->GetTid();
  MarkSweep::ProcessMarkStackParallel(thread_count);
}


void IPCMarkSweep::Sweep(bool swap_bitmaps) {
  //LOG(ERROR) << "IPCMarkSweep::Sweep....";
  Thread* self = Thread::Current();
  UpdateGCPhase(self, space::IPC_GC_PHASE_SWEEP);
  base::TimingLogger::ScopedSplit("Sweep", &timings_);
  int _synchronized = 0;
  if((_synchronized = android_atomic_release_load(&(server_synchronize_))) == 1) {
    BlockForGCPhase(self, space::IPC_GC_PHASE_FINALIZE_SWEEP);
    const bool partial = (GetGcType() == kGcTypePartial);
    SweepCallbackContext scc;
    scc.mark_sweep = this;
    scc.self = self;
    for (const auto& space : GetHeap()->GetContinuousSpaces()) {
      // We always sweep always collect spaces.
      bool sweep_space = (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect);
      if (!partial && !sweep_space) {
        // We sweep full collect spaces when the GC isn't a partial GC (ie its full).
        sweep_space = (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect);
      }
      if (sweep_space) {
        uintptr_t begin = reinterpret_cast<uintptr_t>(space->Begin());
        uintptr_t end = reinterpret_cast<uintptr_t>(space->End());
        scc.space = space->AsDlMallocSpace();

        accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
        accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();

        if (swap_bitmaps) {
          std::swap(live_bitmap, mark_bitmap);
        }
        if (!space->IsZygoteSpace()) {
          base::TimingLogger::ScopedSplit split("SweepAllocSpace", &timings_);
          // Bitmaps are pre-swapped for optimization which enables sweeping with the heap unlocked.
          mirror::Object** objects_array = mark_stack_->GetBaseAddress();
          size_t num_ptrs = mark_stack_->Size();

          //LOG(ERROR) << "==== mark stack size for bulk free is ==== " << num_ptrs;

          //LOG(ERROR) << "CALLING IPCMarkSweep::Sweep...FreeListAgent, num_ptrs:" << num_ptrs << ", objects_array:" << reinterpret_cast<void*>(objects_array);
          space->AsDlMallocSpace()->FreeListAgent(self, num_ptrs, objects_array);
          //          size_t freed_bytes_sweep =
          //          LOG(ERROR) << "IPCMarkSweep::Sweep freed_bytes_sweep..freedBytes = "
          //              << freed_bytes_sweep << "; freedPointers = "<< num_ptrs;
          //space->AsDlMallocSpace()->



          //          mspace_bulk_free(
          //              (reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace()))->GetMspace(),
          //              reinterpret_cast<void**>(objects_array), num_ptrs);


          mark_stack_->Reset();

          //          accounting::SPACE_BITMAP::SweepWalk(*live_bitmap, *mark_bitmap, begin, end,
          //                                               &SweepCallback, reinterpret_cast<void*>(&scc));

        } else {
          base::TimingLogger::ScopedSplit split("SweepZygote", &timings_);
          // Zygote sweep takes care of dirtying cards and clearing live bits, does not free actual
          // memory.

          accounting::SPACE_BITMAP::SweepWalk(*live_bitmap, *mark_bitmap, begin, end,
                                              &ZygoteSweepCallback, reinterpret_cast<void*>(&scc));


        }
      }
    }
  } else {
    const bool partial = (GetGcType() == kGcTypePartial);
    SweepCallbackContext scc;
    scc.mark_sweep = this;
    scc.self = self;
    for (const auto& space : GetHeap()->GetContinuousSpaces()) {
      // We always sweep always collect spaces.
      bool sweep_space = (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyAlwaysCollect);
      if (!partial && !sweep_space) {
        // We sweep full collect spaces when the GC isn't a partial GC (ie its full).
        sweep_space = (space->GetGcRetentionPolicy() == space::kGcRetentionPolicyFullCollect);
      }
      if (sweep_space) {
        uintptr_t begin = reinterpret_cast<uintptr_t>(space->Begin());
        uintptr_t end = reinterpret_cast<uintptr_t>(space->End());
        scc.space = space->AsDlMallocSpace();

        accounting::SPACE_BITMAP* live_bitmap = space->GetLiveBitmap();
        accounting::SPACE_BITMAP* mark_bitmap = space->GetMarkBitmap();

        if (swap_bitmaps) {
          std::swap(live_bitmap, mark_bitmap);
        }
        if (!space->IsZygoteSpace()) {
          base::TimingLogger::ScopedSplit split("SweepAllocSpace", &timings_);
          // Bitmaps are pre-swapped for optimization which enables sweeping with the heap unlocked.



          accounting::SPACE_BITMAP::SweepWalk(*live_bitmap, *mark_bitmap, begin, end,
                                              &SweepCallback, reinterpret_cast<void*>(&scc));


        } else {
          base::TimingLogger::ScopedSplit split("SweepZygote", &timings_);
          // Zygote sweep takes care of dirtying cards and clearing live bits, does not free actual
          // memory.

          accounting::SPACE_BITMAP::SweepWalk(*live_bitmap, *mark_bitmap, begin, end,
                                              &ZygoteSweepCallback, reinterpret_cast<void*>(&scc));


        }
      }
    }
  }


  SweepLargeObjects(swap_bitmaps);
}





IPCPartialMarkSweep::IPCPartialMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
                                         const std::string& name_prefix)
: IPCMarkSweep(ipcHeap, is_concurrent, name_prefix) {
  meta_data_->gc_type_ = collector::kGcTypePartial;
  cumulative_timings_.SetName(GetName());
}

void IPCPartialMarkSweep::BindBitmaps() {
  IPC_MS_VLOG(INFO) << "IPCPartialMarkSweep::BindBitmaps. starting: _______ " <<
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
                      name_prefix) {
  meta_data_->gc_type_ = collector::kGcTypeSticky;
  cumulative_timings_.SetName(GetName());
}

void IPCStickyMarkSweep::BindBitmaps() {
  IPC_MS_VLOG(ERROR) << "IPCStickyMarkSweep::BindBitmaps. starting: _______ " <<
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
  IPC_MS_VLOG(INFO) << "IPCStickyMarkSweep::MarkReachableObjects. starting: _______ " <<
      currThread->GetTid() << "; phase:" << meta_data_->gc_phase_;
  // All reachable objects must be referenced by a root or a dirty card, so we can clear the mark
  // stack here since all objects in the mark stack will get scanned by the card scanning anyways.
  // TODO: Not put these objects in the mark stack in the first place.
  HandshakeIPCSweepMarkingPhase(ipc_heap_->local_heap_->GetMarkBitmap());
  RecursiveMarkDirtyObjects(false, accounting::ConstantsCardTable::kCardDirty - 1);
}

void IPCStickyMarkSweep::Sweep(bool swap_bitmaps) {
  Thread* self = Thread::Current();
  UpdateGCPhase(self, space::IPC_GC_PHASE_SWEEP);
  int _synchronized = 0;
  if((_synchronized = android_atomic_release_load(&(server_synchronize_))) == 1) {
    BlockForGCPhase(self, space::IPC_GC_PHASE_FINALIZE_SWEEP);
  }
  accounting::ATOMIC_OBJ_STACK_T* live_stack = GetHeap()->GetLiveStack();
  //LOG(ERROR) << "~~~~~ IPCStickyMarkSweep::Sweep calling SweepArray";
  SweepArray(live_stack, false);
}

void IPCStickyMarkSweep::MarkThreadRoots(Thread* self) {
  IPC_MS_VLOG(INFO) << "IPCStickyMarkSweep::MarkThreadRoots. starting: _______ " <<
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
//  IPC_MS_VLOG(ERROR) << "###### IPCMarkSweep::SwapBitmaps() #### ";
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
  IPC_MS_VLOG(ERROR) << "IPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void IPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  IPC_MS_VLOG(ERROR) << "IPCMarkSweep::BindLiveToMarkBitmap";
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
  IPC_MS_VLOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: " << GetGcType() << " ###########";
}


void PartialIPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(ERROR) << "     PartialIPCMarkSweep::FinishPhase...begin:" <<
      currThread->GetTid();
  PartialMarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();
}

void PartialIPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    IPC_MS_VLOG(ERROR) << "     PartialIPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    PartialMarkSweep::InitializePhase();
  }
}


void PartialIPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(ERROR) << "     PartialIPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  PartialMarkSweep::MarkingPhase();

}



void PartialIPCMarkSweep::MarkReachableObjects() {

  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(ERROR) << " <<PartialIPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  {

    HandshakeMarkingPhase();
  }
  PartialMarkSweep::MarkReachableObjects();
  IPC_MS_VLOG(ERROR) << " >>PartialIPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}



//void PartialIPCMarkSweep::SwapBitmaps() {
//  IPC_MS_VLOG(ERROR) << "PartialIPCMarkSweep::SwapBitmaps()";
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
  IPC_MS_VLOG(ERROR) << "PartialIPCMarkSweep::SwapBitmaps()";
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
  IPC_MS_VLOG(ERROR) << "PartialIPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void PartialIPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  IPC_MS_VLOG(ERROR) << "PartialIPCMarkSweep::BindLiveToMarkBitmap";
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
  IPC_MS_VLOG(ERROR) << "############ Initializing IPC: " << GetName() << "; gcType: " << GetGcType() << " ###########";
}



void StickyIPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(ERROR) << "     StickyIPCMarkSweep::FinishPhase...begin:" <<
      currThread->GetTid();
  StickyMarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();

}

void StickyIPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    IPC_MS_VLOG(ERROR) << "     StickyIPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    StickyMarkSweep::InitializePhase();
  }
}

void StickyIPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(ERROR) << "     StickyIPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  StickyMarkSweep::MarkingPhase();

}



void StickyIPCMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  IPC_MS_VLOG(ERROR) << " <<StickyIPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  {
    HandshakeMarkingPhase();
  }
  StickyMarkSweep::MarkReachableObjects();
  IPC_MS_VLOG(ERROR) << " >>StickyIPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

 */
//void StickyIPCMarkSweep::SwapBitmaps() {
//  IPC_MS_VLOG(ERROR) << "StickyIPCMarkSweep::SwapBitmaps()";
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
  IPC_MS_VLOG(ERROR) << "StickyIPCMarkSweep::SwapBitmaps()";
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
  IPC_MS_VLOG(ERROR) << "StickyIPCMarkSweep::UnBindBitmaps";
  for (const auto& space : GetHeap()->GetContinuousSpaces()) {
    if (space->IsDlMallocSpace()) {
      space::DL_MALLOC_SPACE* _space =space->AsDlMallocSpace();
//          reinterpret_cast<space::SharableDlMallocSpace*>(space->AsDlMallocSpace());
      _space->UnBindBitmaps();
    }
  }
}

void StickyIPCMarkSweep::BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  IPC_MS_VLOG(ERROR) << "StickyIPCMarkSweep::BindLiveToMarkBitmap";
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





