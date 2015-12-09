/*
 * icp_mark_sweep.h
 *
 *  Created on: Oct 5, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_
#define ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_

#include "ipcfs/ipcfs.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "gc/space/space.h"
#include "gc/heap.h"
#include "mark_sweep.h"
#include "sticky_mark_sweep.h"
#include "partial_mark_sweep.h"


#define IPC_MARKSWEEP_VLOG_ON 0
#define IPC_MARKSWEEP_VLOG(severity)  if (IPC_MARKSWEEP_VLOG_ON) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream()


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

class IPCMarkSweep;
class IPCHeap;


typedef struct GCSrverRAWCollectorSpace_S {
  // Immune range, every object inside the immune range is assumed to be marked.
  byte* base_;
  byte* base_end_;
  byte* client_base_;
  byte* client_end_;
} __attribute__((aligned(8))) GCSrverRAWCollectorSpace;

class AbstractIPCMarkSweep {
 public:

  IPCHeap* ipc_heap_;
  volatile int collector_index_;
  volatile int server_synchronize_;
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

//  virtual ~AbstractIPCMarkSweep() {}

  //void HandshakeMarkingPhase(void);

  void UpdateGCPhase(Thread*, space::IPC_GC_PHASE_ENUM phase);
  void BlockForGCPhase(Thread*, space::IPC_GC_PHASE_ENUM phase);
  accounting::SPACE_BITMAP* SetMarkBitmap(void);



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
  void BlockForServerInitialization(volatile int32_t* addr_val);
  static void* RunDaemon(void* arg);
  bool StartCollectorDaemon(void);
  bool RunCollectorDaemon(void);
  void NotifyCompleteConcurrentTask(void);
  void ResetHeapMetaDataUnlocked(void);
  void CreateCollectors(void);

//  void AssignNextGCType(void);
  void RaiseServerFlag(void);
  void ResetServerFlag(void);
  void SetCurrentCollector(IPCMarkSweep* collector);
  void ResetCurrentCollector(IPCMarkSweep* collector);
  ~IPCHeap() {}

  /* Collection methods */
  void ConcurrentGC(Thread* self);
  void ExplicitGC(bool clear_soft_references);
  void TrimHeap(void);
  bool CheckTrimming();
  void SetLastProcessID(void);
  collector::GcType WaitForConcurrentIPCGcToComplete(Thread* self);

  collector::GcType CollectGarbageIPC(collector::GcType gc_type,
      GcCause gc_cause, bool clear_soft_references);

  void GrowForUtilization(collector::GcType gc_type, uint64_t gc_duration);


  /* protected by gc_complete_mu_ */
  GcCause curr_gc_cause_;
  /* members replacing the heap main members */
  //Last Gc type we ran. Used by WaitForConcurrentGc to know which Gc was waited on.
  //protected by gc_complete_lock_

//  volatile collector::GcType last_gc_type_;
//  collector::GcType next_gc_type_;

  //indicated that the cycle was executed by a ipc collector due to signal from server.
  volatile int ipc_flag_raised_;


  int collector_entry_;
};


class IPCMarkSweep : public AbstractIPCMarkSweep, public MarkSweep {
 public:

  static accounting::BaseHeapBitmap* _temp_heap_beetmap;
  // Parallel GC data structures.
//  UniquePtr<ThreadPool> thread_pool_;

 // bool halt_ GUARDED_BY(ms_lock_);

  bool RunCollectorDaemon(void);


  IPCMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
      const std::string& name_prefix = "");
  ~IPCMarkSweep() {}

  /* overriding the Marksweep code*/

  bool IsInterprocess() const {
    return true;
  }
  template <typename MarkVisitor>
  inline void ClientScanObjectVisit(const mirror::Object* obj,
      const MarkVisitor& visitor);
  void ScanObjectVisitVerifyArray(const mirror::Object* obj,
      accounting::BaseHeapBitmap* heap_beetmap);
  void ClientVerifyObject(const mirror::Object* obj);
  virtual void FinishPhase();
  virtual void InitializePhase(void);
  // Everything inside the immune range is assumed to be marked.
  void SetImmuneRange(mirror::Object* begin, mirror::Object* end);
  virtual void MarkConcurrentRoots()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  virtual void MarkingPhase(void) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Builds a mark stack and recursively mark until it empties.
  void RecursiveMark()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  virtual void MarkReachableObjects()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  virtual void ProcessMarkStackParallel(size_t thread_count)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  // Recursively blackens objects on the mark stack.
  virtual void ProcessMarkStack(bool paused)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  virtual void ApplyTrimming(void);
  void ClearMarkHolders(void);

  // Mark all reachable objects, done concurrently.
  //void PostMarkingPhase(void);
  /*



  bool IsConcurrent() const;*/
  //void SwapBitmaps() EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);


  mirror::Object* GetImmuneBegin() const {
    return meta_data_->cashed_references_.immune_begin_;
  }

  mirror::Object* GetImmuneEnd() const {
    return meta_data_->cashed_references_.immune_end_;
  }

  // Find the default mark bitmap.
  void FindDefaultMarkBitmap();

  void PreInitializePhase(void);
  void HandshakeIPCSweepMarkingPhase(accounting::BaseHeapBitmap* heap_beetmap = NULL);
  void RequestAppSuspension(accounting::BaseHeapBitmap* heap_beetmap = NULL);
//  void IPCMarkReachablePhase(void) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);



  GCSrverRAWCollectorSpace spaces_[3];
  byte* GetClientSpaceEnd(int index) const;
  byte* GetClientSpaceBegin(int index) const;
  byte* GetServerSpaceEnd(int index) const;
  byte* GetServerSpaceBegin(int index) const;
  void RawObjectScanner(void);
  template <typename MarkVisitor>
  void RawScanObjectVisit(const mirror::Object* obj, const MarkVisitor& visitor);

  const mirror::Class* GetMappedObjectKlass(
                                        const mirror::Object* mapped_obj_parm,
                                        const int32_t offset_);

  template <class referenceKlass>
  const referenceKlass* MapValueToServer(
                                        const uint32_t raw_address_value,
                                        const int32_t offset_) const;

  template <typename Visitor>
  void RawVisitObjectArrayReferences(
                            const mirror::ObjectArray<mirror::Object>* mapped_arr,
                                                    const Visitor& visitor);

  template <typename Visitor>
  void RawVisitClassReferences(
                          const mirror::Class* klass, const mirror::Object* obj,
                                              const Visitor& visitor);

  template <typename Visitor>
  void RawVisitStaticFieldsReferences(const mirror::Class* klass,
                                                     const Visitor& visitor);

  template <typename Visitor>
  void RawVisitInstanceFieldsReferences(const mirror::Class* klass,
                                                       const mirror::Object* obj,
                                                       const Visitor& visitor);
  template <typename Visitor>
  void RawVisitOtherReferences(const mirror::Class* klass,
                                                        const mirror::Object* obj,
                                                        const Visitor& visitor);

  template <typename Visitor>
  void RawVisitFieldsReferences(const mirror::Object* obj,
                              uint32_t ref_offsets, bool is_static,
                              const Visitor& visitor);
  void RawDelayReferenceReferent(const mirror::Class* klass,
                                                mirror::Object* obj);
  void RawEnqPendingReference(mirror::Object* ref,
      mirror::Object** list);
  bool IsMappedReferentEnqueued(const mirror::Object* mapped_ref) const;
  template <class referenceKlass>
  uint32_t MapReferenceToValueClient(
                                  const referenceKlass* mapped_reference) const;
  void SetClientFieldValue(const mirror::Object* mapped_object,
            MemberOffset field_offset, const mirror::Object* mapped_ref_value);
  bool IsMappedObjectMarked(const mirror::Object* object);
  bool IsMappedObjectImmuned(const mirror::Object* obj) const {
    return obj >= GetImmuneBegin() && obj < GetImmuneEnd();
  }

  uint32_t GetClassAccessFlags(const mirror::Class* klass) const;
  int GetMappedClassType(const mirror::Class* klass) const;
  bool IsMappedArrayClass(const mirror::Class* klass) const;
  bool IsObjectArrayMappedKlass(const mirror::Class* klass) const;
  const mirror::Class* GetComponentTypeMappedClass(const mirror::Class* mapped_klass)const;
  bool IsPrimitiveMappedKlass(const mirror::Class* klass) const;
  bool IsInterfaceMappedClass(const mirror::Class* klass) const;
  bool IsFinalMappedClass(const mirror::Class* klass) const;
  bool IsFinalizableMappedClass(const mirror::Class* klass) const;
  bool IsReferenceMappedClass(const mirror::Class* klass) const;
  // Returns true if the class is abstract.
  bool IsAbstractMappedClass(const mirror::Class* klass)const;
  // Returns true if the class is an annotation.
  bool IsAnnotationMappedClass(const mirror::Class* klass) const;
  // Returns true if the class is synthetic.
  bool IsSyntheticMappedClass(const mirror::Class* klass) const;
  bool IsWeakReferenceMappedClass(const mirror::Class* klass) const;
  bool IsFinalizerReferenceMappedClass(const mirror::Class* klass)const;
  bool IsSoftReferenceMappedClass(const mirror::Class* klass) const;
  bool IsPhantomReferenceMappedClass(const mirror::Class* klass) const;
  template <class referenceKlass>
  inline const referenceKlass* MapValueToServer(
                                        const uint32_t raw_address_value) const;


  //void IPCMarkRootsPhase(void) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

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


class IPCPartialMarkSweep : public IPCMarkSweep {
 public:
  IPCPartialMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
      const std::string& name_prefix = "");
  ~IPCPartialMarkSweep() {}

  virtual GcType GetGcType() const {
    return kGcTypePartial;
  }
 protected:
  // Bind the live bits to the mark bits of bitmaps for spaces that aren't collected for partial
  // collections, ie the Zygote space. Also mark this space is immune.
  virtual void BindBitmaps() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
 private:
  DISALLOW_COPY_AND_ASSIGN(IPCPartialMarkSweep);
};

class IPCStickyMarkSweep : public IPCPartialMarkSweep {
 public:
  IPCStickyMarkSweep(IPCHeap* ipcHeap, bool is_concurrent,
      const std::string& name_prefix = "");
  ~IPCStickyMarkSweep() {}

  GcType GetGcType() const {
    return kGcTypeSticky;
  }
 protected:
  // Bind the live bits to the mark bits of bitmaps for all spaces, all spaces other than the
  // alloc space will be marked as immune.
  void BindBitmaps() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void MarkReachableObjects()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void MarkThreadRoots(Thread* self)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void Sweep(bool swap_bitmaps) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

 private:
  DISALLOW_COPY_AND_ASSIGN(IPCStickyMarkSweep);
};




}
}
}

#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_MARK_SWEEP_H_ */
