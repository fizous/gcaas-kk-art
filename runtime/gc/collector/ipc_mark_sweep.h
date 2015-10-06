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
#include "gc/space/space.h"

namespace art {

namespace gc {

namespace collector {

class IPCMarkSweep {
 public:
  space::GCSrvSharableHeapData* meta_;

  InterProcessMutex* phase_mu_;
  InterProcessConditionVariable* phase_cond_;

  InterProcessMutex* barrier_mu_;
  InterProcessConditionVariable* barrier_cond_;


  IPCMarkSweep(space::GCSrvSharableHeapData*);
  void ResetMetaDataUnlocked();
  void DumpValues(void);
}; //class IPCMarkSweep



}
}
}

#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_ */
