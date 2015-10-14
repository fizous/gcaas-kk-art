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

  /* initialize locks */
  SharedFutexData* _conc_futexAddress = &meta_->conc_lock_.futex_head_;
  SharedConditionVarData* _conc_condAddress = &meta_->conc_lock_.cond_var_;

  conc_req_cond_mu_ = new InterProcessMutex("GCConc Mutex", _conc_futexAddress);
  conc_req_cond_ = new InterProcessConditionVariable("GCConc CondVar",
      *conc_req_cond_mu_, _conc_condAddress);

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
  meta_->is_gc_complete_  = 0;
  meta_->is_gc_running_   = 0;
  meta_->conc_count_      = 0;
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
//  ipc_mark_sweep_collectors_.push_back(new IPCMarkSweep(this, true,
//      "fullIPC"));
//  ipc_mark_sweep_collectors_.push_back(new PartialIPCMarkSweep(this, true,
//      "partialIPC"));

  ipc_mark_sweep_collectors_.push_back(new IPCMarkSweep(this, true,
      "ipcMS"));
  ipc_mark_sweep_collectors_.push_back(new StickyIPCMarkSweep(this, true,
      "stickyIPC"));
  ipc_mark_sweep_collectors_.push_back(new PartialIPCMarkSweep(this, true,
      "partialIPC"));

  // Reset the cumulative loggers since we now have a few additional timing phases.
  for (const auto& ipcCollector : ipc_mark_sweep_collectors_) {
    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(ipcCollector));
  }
//  local_heap_->GCPSrvcReinitMarkSweep(iPCMS);
//  local_heap_->GCPSrvcReinitMarkSweep(partialIPCMS);
//  local_heap_->GCPSrvcReinitMarkSweep(stickyIPCMS);

//  ipc_mark_sweep_collectors_.push_back(new IPCMarkSweep(this, true,
//      "partialIPC"));
//  ipc_mark_sweep_collectors_.push_back(new StickyIPCMarkSweep(this, true,
//      "stickyIPC"));
//  ipc_mark_sweep_collectors_.push_back(new PartialIPCMarkSweep(this, true,
//      "partialIPC"));
//
//  std::vector<collector::AbstractIPCMarkSweep*>::iterator iter =
//      ipc_mark_sweep_collectors_.begin();
//  while( iter != ipc_mark_sweep_collectors_.end()) {
//
//    local_heap_->GCPSrvcReinitMarkSweep(reinterpret_cast<collector::MarkSweep*>(*iter));
//    ++iter;
//  }

}



void IPCHeap::ConcurrentGC(Thread* self) {
  local_heap_->ConcurrentGC(self);
}

void IPCHeap::CollectGarbage(bool clear_soft_references)  {
//  Thread* self = Thread::Current();
//  WaitForConcurrentGcToComplete(self);
}


bool IPCHeap::RunCollectorDaemon() {
  Thread* self = Thread::Current();
  LOG(ERROR) << "IPCHeap::WaitForRequest.." << self->GetTid();

  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    LOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- before while: conc flag = " << meta_->conc_flag_;
    while(meta_->conc_flag_ == 0) {
      conc_req_cond_->Wait(self);
    }
    LOG(ERROR) << "-------- IPCHeap::RunCollectorDaemon --------- leaving wait: conc flag = " << meta_->conc_flag_;

  }
  //Runtime* runtime = Runtime::Current();

  ScopedThreadStateChange tscConcA(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    meta_->is_gc_running_ = 1;
    //meta_->conc_flag_ = 0;
   // meta_->is_gc_complete_ = 0;
    conc_req_cond_->Broadcast(self);
  }
  LOG(ERROR) << ">>>>>>>>>IPCHeap::ConcurrentGC...Starting: " << self->GetTid() << " <<<<<<<<<<<<<<<";
  ConcurrentGC(self);
  meta_->conc_count_ = meta_->conc_count_ + 1;
  LOG(ERROR) << "<<<<<<<<<IPCHeap::ConcurrentGC...Done: " << self->GetTid() <<
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
        LOG(ERROR) << "      IPCHeap::RunCollectorDaemon: waiting for gc_complete reset";
        conc_req_cond_->Wait(self);
      }
      conc_req_cond_->Broadcast(self);
      LOG(ERROR) << "      IPCHeap::RunCollectorDaemon: leave waiting for gc_complete reset";
    }
  }
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
  heap_meta_->is_gc_complete_ = 0;
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





