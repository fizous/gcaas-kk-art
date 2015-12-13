#ifndef ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_
#define ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_


#include "ipcfs/ipcfs.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "gc/service/global_allocator.h"
#include "gc/space/space.h"
#include "gc/heap.h"


namespace art {

namespace mirror {
  class Class;
  class Object;
  template<class T> class ObjectArray;
}  // namespace mirror


namespace gc {
namespace collector {


typedef struct GCSrverCollectorSpace_S {
  // Immune range, every object inside the immune range is assumed to be marked.
  byte* base_;
  byte* base_end_;
  byte* client_base_;
  byte* client_end_;
} __attribute__((aligned(8))) GCSrverCollectorSpace;


class IPCServerMarkerSweep {
 public:
  static const int KGCSpaceCount = 3;
  static const int KGCSpaceServerImageInd_    = 0;
  static const int KGCSpaceServerZygoteInd_   = 1;
  static const int KGCSpaceServerAllocInd_    = 2;


  static const int KGCSpaceServerMarkStackInd_      = 2;
  static const int KGCSpaceServerMarkBitmapInd_     = 3;
  static const int KGCSpaceServerLiveBitmapInd_     = 4;
  static const int KGCSpaceServerZygoteMarkBMInd_   = 5;
  static const int KGCSpaceServerZygoteLiveBMInd_   = 6;

  static int passed_bitmap_tests_;
  static int pushed_back_to_stack_;
  static int is_reference_class_cnt_;

  gcservice::GCServiceClientRecord* const client_rec_;
  space::GCSrvSharableHeapData* const heap_meta_;
  const int32_t offset_;




  GCSrverCollectorSpace spaces_[KGCSpaceCount];
  int marked_spaces_count_prof_[KGCSpaceCount];
  space::GCSrvSharableCollectorData* curr_collector_ptr_;

  accounting::GCSrvceBitmap mark_bitmap_;
  accounting::SharedServerSpaceBitmap* current_mark_bitmap_;
  accounting::SharedServerSpaceBitmap* current_live_bitmap_;

  std::vector<accounting::SharedServerSpaceBitmap*> all_bitmaps_;
  std::vector<accounting::SharedServerSpaceBitmap*> mark_bitmaps_;
  std::vector<accounting::SharedServerSpaceBitmap*> live_bitmaps_;

  accounting::ServerStructuredObjectStack* mark_stack_;

  // offset of java.lang.ref.Reference.referent
  MemberOffset ref_referent_off_client_;
  // offset of java.lang.ref.Reference.queue
  MemberOffset ref_queue_off_client_;
  // offset of java.lang.ref.Reference.queueNext
  MemberOffset ref_queueNext_off_client_;
  // offset of java.lang.ref.Reference.pendingNext
  MemberOffset ref_pendingNext_off_client_;
  // offset of java.lang.ref.FinalizerReference.zombie
  MemberOffset ref_reference_zombie_off_client_;


  space::GCSrvceCashedReferences cashed_references_client_;
  space::GCSrvceCashedStatsCounters cashed_stats_client_;



  //statistics

  IPCServerMarkerSweep(gcservice::GCServiceClientRecord* client_record);
  void SetCachedReferencesPointers(space::GCSrvceCashedReferences* dest,
      space::GCSrvceCashedReferences* src);
  void UpdateClientCachedReferences(space::GCSrvceCashedReferences* dest,
      space::GCSrvceCashedReferences* src);
  void UpdateStatsRecord(space::GCSrvceCashedStatsCounters* dest,
      space::GCSrvceCashedStatsCounters* src, bool atomicCp);
  void ResetStats(void);

  mirror::Object* MapObjectAddress(mirror::Object* obj);
  bool ClientSpaceContains(mirror::Object* obj, GCSrverCollectorSpace* server_space);

  void MarkRemoteObject(space::GCSrvSharableCollectorData* collector_meta);
  void FindDefaultMarkBitmap(void);



  // the object passed to this function is supposed to be marked by the
  // client markbitmap. return false if the object is not mapped correctly to
  // the bitmap.
  bool TestMappedBitmap(const mirror::Object* mapped_object);
  accounting::SharedServerSpaceBitmap* GetMappedBitmap(
      android::MappedPairProcessFD* pair_memory,
      int entry_ind, accounting::GCSrvceBitmap* bitmap_meta_addr);

  accounting::ServerStructuredObjectStack* GetMappedMarkStack(
      android::MappedPairProcessFD* pair_memory,
      int entr_ind,
      StructuredObjectStackData* stack_meta_address);


  void ProcessMarckStack(void);
  bool ServerScanObjectRemoval(const mirror::Object* obj);
  void ServerScanObject(const mirror::Object* obj, uint32_t calculated_offset);

  bool IsMappedReferentEnqueued(const mirror::Object* mapped_ref) const;
  // Schedules an unmarked object for reference processing.
  void ServerDelayReferenceReferent(const mirror::Class* klass, mirror::Object* obj);
  void ServerEnqPendingReference(mirror::Object* ref,
      mirror::Object** list);

  template <typename MarkVisitor>
  bool ServerScanObjectVisitRemoval(const mirror::Object* obj,
      const MarkVisitor& visitor);

  template <typename MarkVisitor>
  void ServerScanObjectVisit(const mirror::Object* obj, const MarkVisitor& visitor);
  //void ExternalScanObjectVisit(mirror::Object* obj, void* calculated_offset);
  void MarkReachableObjects(space::GCSrvSharableCollectorData* collector_addr);
  void SweepSpaces(space::GCSrvSharableCollectorData* collector_addr);
  size_t ServerFreeSpaceList(Thread* self, size_t num_ptrs,
      mirror::Object** ptrs);
  void ServerSweepCallback(size_t num_ptrs, mirror::Object** ptrs, void* arg);
  template <class referenceKlass>
  uint32_t MapReferenceToValueClient(const referenceKlass* mapped_reference) const;
  template <class referenceKlass>
  const referenceKlass* MapValueToServer(uint32_t raw_address_value) const;
  template <class referenceKlass>
  const referenceKlass* MapReferenceToClient(
                                        const referenceKlass* const ref_parm);

  void SetClientFieldValue(const mirror::Object* mapped_object,
      MemberOffset field_offset, const mirror::Object* mapped_ref_value);
  template <class referenceKlass>
  const referenceKlass* MapReferenceToClientChecks(
                                        const referenceKlass* const ref_parm);

  template <class referenceKlass>
  const referenceKlass* MapReferenceToServerChecks(const referenceKlass* const ref_parm);
  template <class referenceKlass>
  const referenceKlass* MapReferenceToServer(const referenceKlass* const ref_parm);

  const mirror::Class* GetMappedObjectKlass(const mirror::Object* mapped_obj_parm);
//  template <class TypeRef>
//  TypeRef* ServerMapHeapReference(TypeRef* ptr_param);


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



  template <typename Visitor>
  void ServerVisitObjectArrayReferences(
                              const mirror::ObjectArray<mirror::Object>* array,
                                                      const Visitor& visitor);
  template <typename Visitor>
  void ServerVisitClassReferences(
                          const mirror::Class* klass, const mirror::Object* obj,
                                              const Visitor& visitor);

  template <typename Visitor>
  void ServerVisitStaticFieldsReferences(const mirror::Class* klass,
                                                     const Visitor& visitor);

  template <typename Visitor>
  void ServerVisitInstanceFieldsReferences(const mirror::Class* klass,
                                                       const mirror::Object* obj,
                                                       const Visitor& visitor);

  bool IsArtFieldVolatile(const mirror::ArtField* field);

  template <typename Visitor>
  void ServerVisitFieldsReferences(const mirror::Object* obj,
                              uint32_t ref_offsets, bool is_static,
                              const Visitor& visitor);

  // Visits the header and field references of a data object.
  template <typename Visitor>
  void ServerVisitOtherReferences(const mirror::Class* klass, const mirror::Object* obj,
                                   const Visitor& visitor);
  const mirror::ArtField* ServerClassGetStaticField(const mirror::Class* klass,
      uint32_t i);
  const mirror::ArtField* ServerClassGetInstanceField(const mirror::Class* klass,
      uint32_t i);

  const mirror::Class* GetSuperMappedClass(const mirror::Class* mapped_klass);
  size_t GetNumReferenceStaticFields(const mirror::Class* klass_ref) const;
  size_t GetNumReferenceInstanceFields(const mirror::Class* klass_ref) const;
//  template <class TypeRef>
//  bool IsValidObjectForServer(TypeRef* ptr_param);

  template <class referenceKlass>
  bool BelongsToOldHeap(const referenceKlass* const ptr_param) const;

  template <class TypeRef>
  bool IsMappedObjectToAllocationSpace(
                                          const TypeRef* const ptr_param) const;
  template <class TypeRef>
  bool IsMappedObjectToServer(const TypeRef* const ptr_param) const;
  template <class TypeRef>
  inline bool BelongsToServerHeap(const TypeRef* const ptr_param) const;

//  mirror::Class* GetClientClassFromObject(mirror::Object* obj);
  void MarkObject(const mirror::Object* obj);
  void MarkObjectNonNull(const mirror::Object* obj);
  bool IsMappedObjectMarked(const mirror::Object* object);

  byte* GetClientSpaceEnd(int index) const;
  byte* GetClientSpaceBegin(int index) const;
  byte* GetServerSpaceEnd(int index) const;
  byte* GetServerSpaceBegin(int index) const;

  void UpdateCurrentMarkBitmap(void);
  void SetMarkHolders(space::GCSrvSharableCollectorData* collector_addr);
  bool InitMarkingPhase(space::GCSrvSharableCollectorData* collector_addr);
  // Returns true if an object is inside of the immune region (assumed to be marked).
  bool IsMappedObjectImmuned(const mirror::Object* obj) const {
    return obj >= GetImmuneBegin() && obj < GetImmuneEnd();
  }

  mirror::Object* GetImmuneBegin() const{
    return cashed_references_client_.immune_begin_;
  }

  mirror::Object* GetImmuneEnd() const {
    return cashed_references_client_.immune_end_;
  }


  mirror::ArtField* ServerClassGetStaticField(mirror::Class* klass, uint32_t i);
  mirror::ArtField* ServerClassGetInstanceField(mirror::Class* klass, uint32_t i);
  mirror::Class* ServerClassGetSuperClass(mirror::Class* klass);
};//class IPCServerMarkerSweep

}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_ */
