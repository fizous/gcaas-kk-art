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
#include "sticky_mark_sweep.h"
#include "partial_mark_sweep.h"
#include "gc/space/space.h"


#define GC_IPC_COLLECT_PHASE(PHASE, THREAD) \
    ScopedThreadStateChange tsc(THREAD, kWaitingForGCProcess);  \
    IPMutexLock interProcMu(THREAD, *phase_mu_); \
    heap_meta_->gc_phase_ = PHASE;

#define GC_IPC_BLOCK_ON_PHASE(PHASE, THREAD) \
    ScopedThreadStateChange tsc(THREAD, kWaitingForGCProcess); \
    IPMutexLock interProcMu(THREAD, *phase_mu_); \
    while(heap_meta_->gc_phase_ != PHASE) \
      phase_cond_->Wait(THREAD);

namespace art {
namespace mirror {
  class Class;
  class Object;
  template<class T> class ObjectArray;
}  // namespace mirror
namespace gc {

namespace collector {


class IPCHeap;

class AbstractIPCMarkSweep {
 public:
  IPCHeap* ipc_heap_;
  InterProcessMutex* phase_mu_;
  InterProcessConditionVariable* phase_cond_;

  InterProcessMutex* barrier_mu_;
  InterProcessConditionVariable* barrier_cond_;



  space::GCSrvSharableHeapData* heap_meta_;
  space::GCSrvSharableCollectorData* meta_data_;

  // IPCMarkSweep(space::GCSrvSharableHeapData*);
  AbstractIPCMarkSweep(IPCHeap* ipcHeap, bool concurrent);

  void ResetMetaDataUnlocked();

  void DumpValues(void);

  ~AbstractIPCMarkSweep() {}

  void HandshakeMarkingPhase(void);

  /************************
   * cumulative statistics
   ************************/

  // Cumulative statistics.
//  uint64_t total_time_ns_;
//  uint64_t total_paused_time_ns_;
//  uint64_t total_freed_objects_;
//  uint64_t total_freed_bytes_;
};//AbstractIPCMarkSweep


class IPCHeap {
 public:
  mutable Mutex ms_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  ConditionVariable ms_cond_ GUARDED_BY(ms_lock_);

  IPCHeap(space::GCSrvSharableHeapData*, Heap*);

  space::GCSrvSharableHeapData* meta_;
  Heap* local_heap_;

  InterProcessMutex* conc_req_cond_mu_;
  InterProcessConditionVariable* conc_req_cond_;


  InterProcessMutex* gc_complete_mu_;
  InterProcessConditionVariable* gc_complete_cond_;


  Thread*   collector_daemon_ GUARDED_BY(ms_lock_);
  pthread_t collector_pthread_ GUARDED_BY(ms_lock_);

  void SetCollectorDaemon(Thread* thread);

  static void* RunDaemon(void* arg);
  bool StartCollectorDaemon(void);
  bool RunCollectorDaemon(void);

  void ResetHeapMetaDataUnlocked(void);
  void CreateCollectors(void);

  void AssignNextGCType(void);

  ~IPCHeap() {}

  /* Collection methods */
  void ConcurrentGC(Thread* self);
  void ExplicitGC(bool clear_soft_references);
  void TrimHeap(void);
  collector::GcType WaitForConcurrentIPCGcToComplete(Thread* self);

  collector::GcType CollectGarbageIPC(collector::GcType gc_type,
      GcCause gc_cause, bool clear_soft_references);

  /* members replacing the heap main members */
  //Last Gc type we ran. Used by WaitForConcurrentGc to know which Gc was waited on.
  //protected by gc_complete_lock_
  volatile collector::GcType last_gc_type_;
  collector::GcType next_gc_type_;

  //indicated that the cycle was executed by a ipc collector due to signal from server.
  volatile int ipc_flag_raised_;


  int collector_entry_;
};


class IPCMarkSweep : public AbstractIPCMarkSweep, public MarkSweep {
 public:

  // Parallel GC data structures.
//  UniquePtr<ThreadPool> thread_pool_;

 // bool halt_ GUARDED_BY(ms_lock_);

  bool RunCollectorDaemon(void);


  IPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
      const std::string& name_prefix = "");
  ~IPCMarkSweep() {}

  /* overriding the Marksweep code*/
  void InitializePhase(void);
  // Everything inside the immune range is assumed to be marked.
  void SetImmuneRange(mirror::Object* begin, mirror::Object* end);
  void FinishPhase();
  void MarkingPhase(void) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void MarkReachableObjects()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  void SwapBitmaps() EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);


  mirror::Object* GetImmuneBegin() {
    return meta_data_->immune_begin_;
  }

  mirror::Object* GetImmuneEnd() {
    return meta_data_->immune_end_;
  }

//
//  void BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space)
//      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//
//  void UnBindBitmaps()
//      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  /** GC Phases **/

//  void ReclaimClientPhase(void);
//  void PreInitCollector(void);
//  void PreConcMarkingPhase(void);
//  void ConcMarkPhase(void);
//  void ResetPhase(void);
//  void FinalizePhase(void);


//
//  void InitialPhase(void);
//  void MarkRootPhase(void);
//
//  void ReclaimPhase(void);

//  void ServerRun(void);
//
//  void ClientRun(void);
//  void ClientInitialPhase(void);
//  void ClientMarkRootPhase(void);
//  void ClientConcMarkPhase(void);
//  void ClientReclaimPhase(void);
//  void ClientFinishPhase(void);
}; //class IPCMarkSweep


class PartialIPCMarkSweep : public AbstractIPCMarkSweep, public PartialMarkSweep {
 public:
  PartialIPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
      const std::string& name_prefix = "");
  ~PartialIPCMarkSweep() {}
  /* overriding the PartialMarkSweep code*/
  void InitializePhase(void);
  void FinishPhase();
  void MarkingPhase(void) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void MarkReachableObjects()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  void SwapBitmaps() EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//  void BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space)
//      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//
//  void UnBindBitmaps()
//      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
};

class StickyIPCMarkSweep : public AbstractIPCMarkSweep, public StickyMarkSweep {
 public:
  StickyIPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
      const std::string& name_prefix = "");
  ~StickyIPCMarkSweep() {}
  /* overriding the PartialMarkSweep code*/
  void InitializePhase(void);
  void FinishPhase();
  void MarkingPhase(void) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void MarkReachableObjects()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  void SwapBitmaps() EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//  void BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space)
//      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//
//  void UnBindBitmaps()
//      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
};




}
}
}

#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_ */
