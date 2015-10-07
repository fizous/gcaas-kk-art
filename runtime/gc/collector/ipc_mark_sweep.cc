/*
 * ipc_mark_sweep.cc
 *
 *  Created on: Oct 5, 2015
 *      Author: hussein
 */

#include "globals.h"
#include "mem_map.h"
#include "ipcfs/ipcfs.h"
#include "mark_sweep.h"
#include "scoped_thread_state_change.h"
#include "gc/space/space.h"
#include "gc/collector/ipc_mark_sweep.h"

namespace art {

namespace gc {

namespace collector {


IPCMarkSweep::IPCMarkSweep(space::GCSrvSharableHeapData* meta_alloc,
              Heap* heap, bool is_concurrent, const std::string& name_prefix)
    : MarkSweep(heap, false, name_prefix + (name_prefix.empty() ? "" : " ") + "ipc"), meta_(meta_alloc) {
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
}


void IPCMarkSweep::DumpValues(void){
  LOG(ERROR) << "IPCMARKSWEEP: " << "zygote_begin: " << reinterpret_cast<void*>(meta_->zygote_begin_)
      << "\n zygote_end: " << reinterpret_cast<void*>(meta_->zygote_end_)
      << "\n image_begin: " << reinterpret_cast<void*>(meta_->image_space_begin_)
      << "\n image_end: " << reinterpret_cast<void*>(meta_->image_space_end_);
}


void IPCMarkSweep::InitialPhase(){
  Thread* currThread = Thread::Current();
  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_INIT, currThread);
  ResetMetaDataUnlocked();
}


void IPCMarkSweep::MarkRootPhase(void){
  Thread* currThread = Thread::Current();
  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_ROOT_MARK, currThread);
  phase_cond_->Broadcast(currThread);
}

void IPCMarkSweep::ConcMarkPhase(void){
  Thread* currThread = Thread::Current();
  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_CONC_MARK, currThread);

  //do the conc marking here
}


void IPCMarkSweep::ReclaimPhase(void){
  Thread* currThread = Thread::Current();
  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_RECLAIM, currThread);
}


void IPCMarkSweep::FinishPhase(void){
  Thread* currThread = Thread::Current();
  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_FINISH, currThread);
}


void IPCMarkSweep::ServerRun(void) {
  InitialPhase();
  /* block until client marks the roots */
  MarkRootPhase();

  ConcMarkPhase();


  ReclaimPhase();


  FinishPhase();

}



void IPCMarkSweep::ClientInitialPhase(void) {
  Thread* currThread = Thread::Current();
  GC_IPC_BLOCK_ON_PHASE(space::IPC_GC_PHASE_ROOT_MARK, currThread);
}

void IPCMarkSweep::ClientMarkRootPhase(void) {
  Thread* currThread = Thread::Current();
  // do marking roots here
  GC_IPC_COLLECT_PHASE(space::IPC_GC_PHASE_CONC_MARK, currThread);
  phase_cond_->Broadcast(currThread);
}

void IPCMarkSweep::ClientConcMarkPhase(void) {

}


void IPCMarkSweep::ClientReclaimPhase(void) {

}

void IPCMarkSweep::ClientFinishPhase(void) {

}

void IPCMarkSweep::ClientRun(void) {
  //wait for signal to mark the roots
  ClientInitialPhase();
  /* start the root marking phase */
  ClientMarkRootPhase();

  ClientConcMarkPhase();

  ClientReclaimPhase();



  ClientFinishPhase();

}




}
}
}


//
//
//





