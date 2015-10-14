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
#include "gc/space/dlmalloc_space-inl.h"
#include "gc/space/space.h"
#include "gc/collector/ipc_mark_sweep.h"

namespace art {

namespace gc {

namespace collector {


IPCHeap::IPCHeap(space::GCSrvSharableHeapData* heap_meta, Heap* heap) :
    ms_lock_("heap-ipc lock"),
    ms_cond_("heap-ipcs::cond_", ms_lock_),
    meta_(heap_meta),
    local_heap_(heap),
    collector_daemon_(NULL) {

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
  meta_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  meta_->freed_objects_   = 0;
  meta_->freed_bytes_     = 0;
  meta_->barrier_count_   = 0;
  meta_->conc_flag_       = 0;
//  meta_->is_gc_complete_  = 0;
  meta_->is_gc_running_   = 0;
  meta_->conc_count_      = 0;
  meta_->concurrent_gc_ = (local_heap_->concurrent_gc_) ? 1 : 0;;
  /* heap members */
  last_gc_type_ = collector::kGcTypeNone;
  next_gc_type_ = collector::kGcTypePartial;

  /* heap statistics */
  meta_->total_objects_freed_ever_  = local_heap_->GetObjectsFreedEver();
  meta_->total_bytes_freed_ever_    = local_heap_->GetBytesFreedEver();
}


void IPCHeap::AssignNextGCType(void) {
  next_gc_type_ = local_heap_->next_gc_type_;
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
    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(new StickyIPCMarkSweep(this, _conc_flag,
        "stickyIPC")));
    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(new PartialIPCMarkSweep(this, _conc_flag,
        "partialIPC")));
  }
}



void IPCHeap::ConcurrentGC(Thread* self) {
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    if (Runtime::Current()->IsShuttingDown()) {
      return;
    }
  }
  if (local_heap_->WaitForConcurrentGcToComplete(self) == collector::kGcTypeNone) {
    CollectGarbageIPC(next_gc_type_, kGcCauseBackground, false);
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

void IPCHeap::TrimHeap(void)  {
  local_heap_->Trim();
}

collector::GcType IPCHeap::WaitForConcurrentIPCGcToComplete(Thread* self) {
  collector::GcType last_gc_type = collector::kGcTypeNone;
  bool do_wait = false;
  if(meta_->concurrent_gc_) { // the heap is concurrent
    {
      IPMutexLock interProcMu(self, *gc_complete_mu_);
      do_wait = meta_->is_gc_running_;
    }
    if(do_wait) {
      // We must wait, change thread state then sleep on gc_complete_cond_;
      ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGcToComplete);
      IPMutexLock interProcMu(self, *gc_complete_mu_);
      while (meta_->is_gc_running_ == 1) {
        gc_complete_cond_->Wait(self);
      }
      last_gc_type = last_gc_type_;
    }
  }
  return last_gc_type;
}


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

  if (gc_type == collector::kGcTypeSticky &&
      local_heap_->GetAllocSpace()->Size() < local_heap_->GetMinAllocSpaceSizeForSticky()) {
    gc_type = collector::kGcTypePartial;
  }

  collector::MarkSweep* collector = NULL;

  for (const auto& cur_collector : local_heap_->mark_sweep_collectors_) {
    if (cur_collector->IsConcurrent() == meta_->concurrent_gc_ && cur_collector->GetGcType() == gc_type) {
      collector = cur_collector;
      if(gc_cause == kGcCauseBackground)
        LOG(ERROR) << "========collector: " << collector->GetName();
      break;
    }
  }

  CHECK(collector != NULL)
      << "Could not find garbage collector with concurrent=" << meta_->concurrent_gc_
      << " and type=" << gc_type;

  collector->SetClearSoftReferences(clear_soft_references);
  collector->Run();
  meta_->total_objects_freed_ever_  += collector->GetFreedObjects();
  meta_->total_bytes_freed_ever_    += collector->GetFreedBytes();


  {
      IPMutexLock interProcMu(self, *gc_complete_mu_);
      meta_->is_gc_running_ = 0;
      last_gc_type_ = gc_type;
      // Wake anyone who may have been waiting for the GC to complete.
      gc_complete_cond_->Broadcast(self);
  }

  return gc_type;
}

bool IPCHeap::RunCollectorDaemon() {
  Thread* self = Thread::Current();
  LOG(ERROR) << "IPCHeap::WaitForRequest.." << self->GetTid();

  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    LOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- before while: conc flag = " << meta_->conc_flag_;
    while(meta_->conc_flag_ != 1) {
      conc_req_cond_->Wait(self);
    }
    LOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- leaving wait: conc flag = " << meta_->conc_flag_;

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
  LOG(ERROR) << ">>>>>>>>>IPCHeap::ConcurrentGC...Starting: " << self->GetTid() << " <<<<<<<<<<<<<<<";
  ConcurrentGC(self);
  meta_->conc_count_ = meta_->conc_count_ + 1;
  LOG(ERROR) << "<<<<<<<<<IPCHeap::ConcurrentGC...Done: " << self->GetTid() <<
      " >>>>>>>>>>>>>>> conc_count=" << meta_->conc_count_;
  ScopedThreadStateChange tscConcB(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
//    meta_->is_gc_complete_ = 1;
    meta_->conc_flag_ = 2;
    conc_req_cond_->Broadcast(self);
  }
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

AbstractIPCMarkSweep::AbstractIPCMarkSweep(IPCHeap* ipcHeap):
    ipc_heap_(ipcHeap),
    heap_meta_(ipc_heap_->meta_) {

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


  ResetMetaDataUnlocked();

  DumpValues();
}

void AbstractIPCMarkSweep::ResetMetaDataUnlocked() { // reset data without locking
  heap_meta_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  heap_meta_->freed_objects_ = 0;
  heap_meta_->freed_bytes_ = 0;
  heap_meta_->barrier_count_ = 0;
  heap_meta_->conc_flag_ = 0;
//  heap_meta_->is_gc_complete_ = 0;
  heap_meta_->is_gc_running_ = 0;
  heap_meta_->conc_count_ = 0;
}

void AbstractIPCMarkSweep::DumpValues(void){
  LOG(ERROR) << "Dump AbstractIPCMarkSweep: " << "zygote_begin: "
      << reinterpret_cast<void*>(heap_meta_->zygote_begin_)
      << "\n     zygote_end: " << reinterpret_cast<void*>(heap_meta_->zygote_end_)
      << "\n     image_begin: " << reinterpret_cast<void*>(heap_meta_->image_space_begin_)
      << "\n     image_end: " << reinterpret_cast<void*>(heap_meta_->image_space_end_);
}






IPCMarkSweep::IPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap),
    MarkSweep(ipcHeap->local_heap_, is_concurrent,
        name_prefix + (name_prefix.empty() ? "" : " ") + "ipcMS") {

}

void IPCMarkSweep::FinishPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "IPCMarkSweep::FinishPhase...begin:" << currThread->GetTid();
  MarkSweep::FinishPhase();
  ipc_heap_->AssignNextGCType();
}

void IPCMarkSweep::InitializePhase(void) {
  Thread* currThread = Thread::Current();
  {
    LOG(ERROR) << "     IPCMarkSweep::InitializePhase. startingB: " <<
        currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;
    MarkSweep::InitializePhase();
  }
}


void IPCMarkSweep::MarkingPhase(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "     IPCMarkSweep::MarkingPhase. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_meta_->gc_phase_;

  MarkSweep::MarkingPhase();

}



void IPCMarkSweep::MarkReachableObjects() {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << " <<IPCMarkSweep::MarkReachableObjects. starting: " <<
      currThread->GetTid() ;
  MarkSweep::MarkReachableObjects();
  LOG(ERROR) << " >>IPCMarkSweep::MarkReachableObjects. ending: " <<
      currThread->GetTid() ;
}

PartialIPCMarkSweep::PartialIPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap),
    PartialMarkSweep(ipcHeap->local_heap_, is_concurrent,
        name_prefix + (name_prefix.empty() ? "" : " ") + "partialIpcMS") {

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

StickyIPCMarkSweep::StickyIPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
    const std::string& name_prefix) :
    AbstractIPCMarkSweep(ipcHeap),
    StickyMarkSweep(ipcHeap->local_heap_, is_concurrent,
        name_prefix + (name_prefix.empty() ? "" : " ") + "stickyIpcMS") {

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





