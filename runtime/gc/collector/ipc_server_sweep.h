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


  static const int KGCSpaceServerMarkStackInd_    = 2;
  static const int KGCSpaceServerMarkBitmapInd_   = 3;

  gcservice::GCServiceClientRecord* const client_rec_;
  space::GCSrvSharableHeapData* const heap_meta_;
  const int32_t offset_;

  GCSrverCollectorSpace spaces_[KGCSpaceCount];
  space::GCSrvSharableCollectorData* curr_collector_ptr_;

  accounting::GCSrvceBitmap mark_bitmap_;
  accounting::SharedServerSpaceBitmap* current_mark_bitmap_;
  accounting::ATOMIC_OBJ_STACK_T* mark_stack_;


  // Cache java.lang.Class for optimization.
  mirror::Class* java_lang_Class_client_;

  mirror::Object* current_immune_begin_;
  mirror::Object* current_immune_end_;


  //statistics

  volatile int32_t array_count_;
  volatile int32_t class_count_;
  volatile int32_t other_count_;

  IPCServerMarkerSweep(gcservice::GCServiceClientRecord* client_record);


  void ResetStats(void);

  mirror::Object* MapObjectAddress(mirror::Object* obj);
  bool ClientSpaceContains(mirror::Object* obj, GCSrverCollectorSpace* server_space);

  void MarkRemoteObject(space::GCSrvSharableCollectorData* collector_meta);
  void FindDefaultMarkBitmap(void);


  accounting::SharedServerSpaceBitmap* GetMappedBitmap(
      android::MappedPairProcessFD* pair_memory,
      int entry_ind, accounting::GCSrvceBitmap* bitmap_meta_addr);

  accounting::ATOMIC_OBJ_STACK_T* GetMappedMarkStack(
      android::MappedPairProcessFD* pair_memory,
      int entr_ind,
      StructuredObjectStackData* stack_meta_address);


  void ProcessMarckStack(void);
  void ServerScanObject(const mirror::Object* obj, uint32_t calculated_offset);
  template <typename MarkVisitor>
  void ServerScanObjectVisit(const mirror::Object* obj, const MarkVisitor& visitor);
  //void ExternalScanObjectVisit(mirror::Object* obj, void* calculated_offset);
  void MarkReachableObjects(space::GCSrvSharableCollectorData* collector_addr);

  template <class referenceKlass>
  const referenceKlass* MapValueToServer(uint32_t raw_address_value);
  template <class referenceKlass>
  const referenceKlass* MapReferenceToServerChecks(const referenceKlass* const ref_parm);
  template <class referenceKlass>
  const referenceKlass* MapReferenceToServer(const referenceKlass* const ref_parm);

  const mirror::Class* GetMappedObjectKlass(const mirror::Object* mapped_obj_parm);
//  template <class TypeRef>
//  TypeRef* ServerMapHeapReference(TypeRef* ptr_param);


  uint32_t GetClassAccessFlags(const mirror::Class* klass) const;
  int GetMappedClassType(const mirror::Class* klass) const;
  bool IsObjectArrayMappedKlass(const mirror::Class* klass) const;
  const mirror::Class* GetComponentTypeMappedClass(const mirror::Class* mapped_klass);
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
  bool IsFinalizerReferenceMappedClass(const mirror::Class* klass);
  bool IsSoftReferenceMappedClass(const mirror::Class* klass) const;
  bool IsFinalizerReferenceMappedClass(const mirror::Class* klass)const;
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
  bool IsMappedObjectToServer(const TypeRef* const ptr_param) const;
  template <class TypeRef>
  inline bool BelongsToServerHeap(const TypeRef* const ptr_param) const;



  void ServerDelayReferenceReferent(mirror::Class* klass, mirror::Object* obj);

//  mirror::Class* GetClientClassFromObject(mirror::Object* obj);
  void MarkObject(const mirror::Object* obj);
  void MarkObjectNonNull(const mirror::Object* obj);


  byte* GetClientSpaceEnd(int index) const;
  byte* GetClientSpaceBegin(int index) const;
  byte* GetServerSpaceEnd(int index) const;
  byte* GetServerSpaceBegin(int index) const;

//
//  template <typename Visitor>
//  void ServerVisitClassReferences(mirror::Class* klass, mirror::Object* obj,
//                                              const Visitor& visitor);
//
//  template <typename Visitor>
//  void ServerVisitInstanceFieldsReferences(mirror::Class* klass, mirror::Object* obj,
//                                            const Visitor& visitor);
//
//  template <typename Visitor>
//  void ServerVisitFieldsReferences(mirror::Object* obj, uint32_t ref_offsets, bool is_static,
//                                    const Visitor& visitor);


  void InitMarkingPhase(space::GCSrvSharableCollectorData* collector_addr);
  // Returns true if an object is inside of the immune region (assumed to be marked).
  bool IsImmune(const mirror::Object* obj) const {
    return obj >= GetImmuneBegin() && obj < GetImmuneEnd();
  }

  mirror::Object* GetImmuneBegin() const{
    return current_immune_begin_;
  }

  mirror::Object* GetImmuneEnd() const {
    return current_immune_end_;
  }


  mirror::ArtField* ServerClassGetStaticField(mirror::Class* klass, uint32_t i);
  mirror::ArtField* ServerClassGetInstanceField(mirror::Class* klass, uint32_t i);
  mirror::Class* ServerClassGetSuperClass(mirror::Class* klass);
};//class IPCServerMarkerSweep

}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_ */
