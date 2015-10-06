/*
 * ipc_mark_sweep.cc
 *
 *  Created on: Oct 5, 2015
 *      Author: hussein
 */


#include "gc/space/space.h"
#include "gc/collector/ipc_mark_sweep.h"

namespace art {

namespace gc {

namespace collector {

IPCMarkSweep::IPCMarkSweep(space::GCSrvSharableHeapData* meta_alloc) :
    meta_(meta_alloc) {

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
  ResetMetaDataUnlocked();
}


void IPCMarkSweep::MarkRootPhase(void){

}

void IPCMarkSweep::ConcMarkPhase(void){
  Thread* currThread = Thread::Current();
  ScopedThreadStateChange tsc(currThread, kWaitingForGCProcess);
  IPMutexLock interProcMu(currThread, *phase_mu_);
}


void IPCMarkSweep::ReclaimPhase(void){

}


void IPCMarkSweep::FinishPhase(void){

}



}
}
}
