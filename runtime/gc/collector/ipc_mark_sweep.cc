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
#include "mark_sweep.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "gc/space/space.h"
#include "gc/collector/ipc_mark_sweep.h"

namespace art {

namespace gc {

namespace collector {


IPCMarkSweep::IPCMarkSweep(space::GCSrvSharableHeapData* meta_alloc,
              Heap* heap, bool is_concurrent, const std::string& name_prefix)
    : MarkSweep(heap, is_concurrent, name_prefix + (name_prefix.empty() ? "" : " ") + "ipc"), meta_(meta_alloc),
      ms_lock_("ipc lock"),
      ms_cond_("ipcs::cond_", ms_lock_),
      collector_daemon_(NULL){

  /* initialize locks */
  SharedFutexData* _futexAddress = &meta_->phase_lock_.futex_head_;
  SharedConditionVarData* _condAddress = &meta_->phase_lock_.cond_var_;

  phase_mu_   = new InterProcessMutex("HandShake Mutex", _futexAddress);
  phase_cond_ = new InterProcessConditionVariable("HandShake CondVar",
      *phase_mu_, _condAddress);


  SharedFutexData* _futexBarrierAdd =
      &meta_->gc_barrier_lock_.futex_head_;
  SharedConditionVarData* _condBarrierAdd =
      &meta_->gc_barrier_lock_.cond_var_;

  barrier_mu_   = new InterProcessMutex("HandShake Mutex", _futexBarrierAdd);
  barrier_cond_ = new InterProcessConditionVariable("HandShake CondVar",
      *barrier_mu_, _condBarrierAdd);


  /* initialize locks */
  SharedFutexData* _conc_futexAddress = &meta_->conc_lock_.futex_head_;
  SharedConditionVarData* _conc_condAddress = &meta_->conc_lock_.cond_var_;


  conc_req_cond_mu_ = new InterProcessMutex("GCConc Mutex", _conc_futexAddress);
  conc_req_cond_ = new InterProcessConditionVariable("GCConc CondVar",
      *conc_req_cond_mu_, _conc_condAddress);



  ResetMetaDataUnlocked();
  DumpValues();
}

//IPCMarkSweep::IPCMarkSweep(space::GCSrvSharableHeapData* meta_alloc) :
//    meta_(meta_alloc) {
//
//
//}


void IPCMarkSweep::ResetMetaDataUnlocked() { // reset data without locking
  meta_->gc_phase_ = space::IPC_GC_PHASE_NONE;
  meta_->freed_objects_ = 0;
  meta_->freed_bytes_ = 0;
  meta_->barrier_count_ = 0;
  meta_->conc_flag_ = 0;
  meta_->is_gc_complete_ = 0;
  meta_->is_gc_running_ = 0;
}


void IPCMarkSweep::DumpValues(void){
  LOG(ERROR) << "IPCMARKSWEEP: " << "zygote_begin: " << reinterpret_cast<void*>(meta_->zygote_begin_)
      << "\n zygote_end: " << reinterpret_cast<void*>(meta_->zygote_end_)
      << "\n image_begin: " << reinterpret_cast<void*>(meta_->image_space_begin_)
      << "\n image_end: " << reinterpret_cast<void*>(meta_->image_space_end_);
}



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


bool IPCMarkSweep::StartCollectorDaemon(void) {
  LOG(ERROR) << "------------------Start Collector IPC Daemon";

  thread_pool_.reset(new ThreadPool(1));

  CHECK_PTHREAD_CALL(pthread_create,
      (&collector_pthread_, NULL,
      &IPCMarkSweep::RunDaemon, this),
      "IPC mark-sweep Daemon thread");

  Thread* self = Thread::Current();
  MutexLock mu(self, ms_lock_);
  LOG(ERROR) << "------------------StartCollectorDaemon--->going to wait";
  while (collector_daemon_ == NULL) {
    ms_cond_.Wait(self);
  }

  LOG(ERROR) << "------------------StartCollectorDaemon--->leaving";

 // thread_pool_->AddTask(self,
 //     new ClientIpcCollectorTask(conc_req_cond_mu_, conc_req_cond_));



  return true;
}

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
//void IPCMarkSweep::ConcMarkPhase(void){
//  Thread* currThread = Thread::Current();
//  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_CONC_MARK, currThread);
//
//  //do the conc marking here
//}
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
  LOG(ERROR) << " pending inside preInit";
  Thread* currThread = Thread::Current();

//  //GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_INIT, currThread);
  LOG(ERROR) << " left blocking on init condition inside preInit: " << currThread->GetTid();
}

void IPCMarkSweep::FinalizePhase(void){
  Thread* currThread = Thread::Current();
  //GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_FINISH, currThread);
  LOG(ERROR) << "IPCMarkSweep::FinalizePhase...end:" << currThread->GetTid();
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
    meta_->is_gc_complete_ = 0;
    conc_req_cond_->Broadcast(self);
  }
  LOG(ERROR) << ">>>>>>>>>Heap::ConcurrentGC...Starting: " << self->GetTid() + " <<<<<<<<<<<<<<<";
  runtime->GetHeap()->ConcurrentGC(self);
  LOG(ERROR) << "<<<<<<<<<Heap::ConcurrentGC...Done: " << self->GetTid() + " >>>>>>>>>>>>>>>";
  ScopedThreadStateChange tscConcB(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    //meta_->conc_flag_ = 0;
    meta_->is_gc_complete_ = 1;
    meta_->is_gc_running_ = 0;
    conc_req_cond_->Broadcast(self);
  }
  return true;
}

void* IPCMarkSweep::RunDaemon(void* arg) {
  LOG(ERROR) << "IPCMarkSweep::RunDaemon::Begin" ;
  IPCMarkSweep* _ipc_ms = reinterpret_cast<IPCMarkSweep*>(arg);
  CHECK(_ipc_ms != NULL);

  Runtime* runtime = Runtime::Current();
  CHECK(runtime->AttachCurrentThread("IPC-MS-Daem", true, NULL, false));

  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    MutexLock mu(self, _ipc_ms->ms_lock_);
    _ipc_ms->collector_daemon_ = self;
    _ipc_ms->ms_cond_.Broadcast(self);
  }


  LOG(ERROR) << "IPCMarkSweep::RunDaemon::Broadcast" ;
  bool collector_loop = true;
  while(collector_loop) {
    collector_loop = _ipc_ms->RunCollectorDaemon();
  }

  return NULL;
}

/******* overriding marksweep code *************/

void IPCMarkSweep::FinishPhase() {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "IPCMarkSweep::FinishPhase...begin:" << currThread->GetTid();
  MarkSweep::FinishPhase();


  //GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_FINISH, currThread);
  //phase_cond_->Broadcast(currThread);
  FinalizePhase();
  LOG(ERROR) << "IPCMarkSweep::FinishPhase...Left:" << currThread->GetTid();
}

void IPCMarkSweep::InitializePhase() {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "IPCMarkSweep::InitializePhase...begin:" << currThread->GetTid();
  PreInitCollector();
  MarkSweep::InitializePhase();

  LOG(ERROR) << "IPCMarkSweep::InitializePhase...end:" << currThread->GetTid();
}




}
}
}


//
//
//





