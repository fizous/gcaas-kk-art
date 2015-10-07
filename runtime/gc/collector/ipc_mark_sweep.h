/*
 * icp_mark_sweep.h
 *
 *  Created on: Oct 5, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_
#define ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_

#include "ipcfs/ipcfs.h"
#include "thread.h"
#include "gc/heap.h"
#include "mark_sweep.h"
#include "gc/space/space.h"


#define GC_IPC_COLLECT_PHASE(PHASE, THREAD) \
    ScopedThreadStateChange tsc(THREAD, kWaitingForGCProcess);  \
    IPMutexLock interProcMu(THREAD, *phase_mu_); \
    meta_->gc_phase_ = PHASE;

#define GC_IPC_BLOCK_ON_PHASE(PHASE, THREAD) \
    ScopedThreadStateChange tsc(THREAD, kWaitingForGCProcess); \
    IPMutexLock interProcMu(THREAD, *phase_mu_); \
    while(meta_->gc_phase_ != PHASE) \
      phase_cond_->Wait(THREAD);

namespace art {

namespace gc {

namespace collector {

class IPCMarkSweep : public MarkSweep {
 public:
  space::GCSrvSharableHeapData* meta_;

  InterProcessMutex* phase_mu_;
  InterProcessConditionVariable* phase_cond_;

  InterProcessMutex* barrier_mu_;
  InterProcessConditionVariable* barrier_cond_;


 // IPCMarkSweep(space::GCSrvSharableHeapData*);
  IPCMarkSweep(space::GCSrvSharableHeapData* alloc_meta, Heap* heap, bool is_concurrent, const std::string& name_prefix = "");
  void ResetMetaDataUnlocked();
  void DumpValues(void);



  /** GC Phases **/
  void InitialPhase(void);
  void MarkRootPhase(void);
  void ConcMarkPhase(void);
  void ReclaimPhase(void);
  void FinishPhase(void);
  void ServerRun(void);

  void ClientRun(void);
  void ClientInitialPhase(void);
  void ClientMarkRootPhase(void);
  void ClientConcMarkPhase(void);
  void ClientReclaimPhase(void);
  void ClientFinishPhase(void);
}; //class IPCMarkSweep



}
}
}

#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_ */