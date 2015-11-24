/*
 * ipc_server_sweep-inl.h
 *
 *  Created on: Nov 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_
#define ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_

#include "gc/heap.h"
#include "mirror/class.h"
#include "mirror/object_array.h"
#include "mirror/art_field.h"
#include "mirror/art_field-inl.h"
#include "gc/collector/ipc_server_sweep.h"


namespace art {
namespace mirror {
  class Class;
  class Object;
  template<class T> class ObjectArray;
}  // namespace mirror

namespace gc {
namespace collector {

byte* IPCServerMarkerSweep::GetServerSpaceEnd(int index) const {
  return spaces_[index].base_end_;
}
byte* IPCServerMarkerSweep::GetServerSpaceBegin(int index) const {
  return spaces_[index].base_;
}


byte* IPCServerMarkerSweep::GetClientSpaceEnd(int index) const {
  return spaces_[index].client_end_;
}
byte* IPCServerMarkerSweep::GetClientSpaceBegin(int index) const {
  return spaces_[index].client_base_;
}


//template <class TypeRef>
//inline bool IPCServerMarkerSweep::IsValidObjectForServer(TypeRef* ptr_param) {
//  return (BelongsToOldHeap(ptr_param) || IsMappedObjectToServer(ptr_param));
//
//}

template <class referenceKlass>
const referenceKlass* IPCServerMarkerSweep::MapValueToServer(const uint32_t raw_address_value) const {
  if(raw_address_value == (uint32_t)0)
    return NULL;
  const byte* _raw_address = reinterpret_cast<const byte*>(raw_address_value);
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((_raw_address < GetClientSpaceEnd(i)) &&
        (_raw_address >= GetClientSpaceBegin(i))) {
      if(i == KGCSpaceServerImageInd_)
        return reinterpret_cast<const referenceKlass*>(_raw_address);
      return reinterpret_cast<const referenceKlass*>(_raw_address + offset_);
    }
  }

  LOG(FATAL) << "IPCServerMarkerSweep::MapValueToServer....0000--raw_Address_value:"
      << raw_address_value;
  return NULL;
}

template <class referenceKlass>
const referenceKlass* IPCServerMarkerSweep::MapReferenceToServerChecks(const referenceKlass* const ref_parm) {
  if(!BelongsToOldHeap<referenceKlass>(ref_parm)) {
    LOG(FATAL) << "..... MapReferenceToServer: ERROR00.." << ref_parm;
  }
  if(ref_parm == NULL)
    return ref_parm;
  const byte* casted_param = reinterpret_cast<const byte*>(ref_parm);
//  if(casted_param < GetClientSpaceEnd(KGCSpaceServerImageInd_)) {
//    return ref_parm;
//  }
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetClientSpaceEnd(i)) &&
        (casted_param >= GetClientSpaceBegin(i))) {
      if(i == KGCSpaceServerImageInd_)
        return ref_parm;
      return reinterpret_cast<const referenceKlass*>(casted_param + offset_);
    }
  }

  LOG(FATAL) << "..... MapReferenceToServerChecks: ERROR001.." << ref_parm <<
      ", casted_param  = " << casted_param;

  return NULL;
}



template <class referenceKlass>
const referenceKlass* IPCServerMarkerSweep::MapReferenceToServer(const referenceKlass* const ref_parm) {

  if(ref_parm == NULL)
    return ref_parm;
  const byte* casted_param = reinterpret_cast<const byte*>(ref_parm);

  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetClientSpaceEnd(i)) &&
        (casted_param >= GetClientSpaceBegin(i))) {
      if(i == KGCSpaceServerImageInd_)
        return ref_parm;
      return reinterpret_cast<const referenceKlass*>(casted_param + offset_);
    }
  }

  LOG(ERROR) << "..... MapReferenceToServer: ERROR001.." << ref_parm;
  return ref_parm;
}

template <class TypeRef>
bool IPCServerMarkerSweep::BelongsToServerHeap(const TypeRef* const ptr_param) const {
  if(ptr_param == NULL)
    return true;
  const byte* casted_param = reinterpret_cast<const byte*>(ptr_param);
//  if(casted_param < GetServerSpaceEnd(KGCSpaceServerImageInd_)) {
//    return true;
//  }
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetServerSpaceEnd(i)) &&
        (casted_param >= GetServerSpaceBegin(i))) {
      return true;
    }
  }
  return false;
}


template <class TypeRef>
bool IPCServerMarkerSweep::IsMappedObjectToServer(const TypeRef* const ptr_param) const {
  if(ptr_param == NULL)
    return true;
  if(!IsAligned<kObjectAlignment>(ptr_param))
    return false;
  const byte* casted_param = reinterpret_cast<const byte*>(ptr_param);
//  if(casted_param < GetServerSpaceEnd(KGCSpaceServerImageInd_)) {
//    return true;
//  }
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetServerSpaceEnd(i)) &&
        (casted_param >= GetServerSpaceBegin(i))) {
      return true;
    }
  }
  return false;
}

template <class referenceKlass>
bool IPCServerMarkerSweep::BelongsToOldHeap(const referenceKlass* const ptr_param) const {
  if(ptr_param == NULL)
    return true;
  if(!IsAligned<kObjectAlignment>(ptr_param))
    return false;
  const byte* casted_param = reinterpret_cast<const byte*>(ptr_param);
//  if(casted_param < GetClientSpaceEnd(KGCSpaceServerImageInd_)) {
//    return true;
//  }
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetClientSpaceEnd(i)) &&
        (casted_param >= GetClientSpaceBegin(i))) {
      return true;
    }
  }
  return false;
}

const mirror::Class* IPCServerMarkerSweep::GetMappedObjectKlass(const mirror::Object* mapped_obj_parm) {
  const byte* raw_addr_class = reinterpret_cast<const byte*>(mapped_obj_parm) +
      mirror::Object::ClassOffset().Int32Value();
  const mirror::Class* class_address =
      *reinterpret_cast<mirror::Class* const *>(raw_addr_class);
  if (UNLIKELY(class_address == NULL)) {
    LOG(FATAL) << "ERROR2....Null class in object: " << mapped_obj_parm;
  } else if (UNLIKELY(!IsAligned<kObjectAlignment>(class_address))) {
    LOG(FATAL) << "ERROR3.....Class isn't aligned: " << class_address <<
        " in object: " << mapped_obj_parm;
  } else if(!BelongsToOldHeap<mirror::Class>(class_address)) {
    LOG(FATAL) << "ERROR4.....Class isn't aligned: " << class_address <<
        " in object: " << mapped_obj_parm;
  }

//  if(class_address == reinterpret_cast<const mirror::Object*>(GetClientSpaceEnd(KGCSpaceServerImageInd_))) {
//      LOG(FATAL) << "..... IPCServerMarkerSweep::GetMappedObjectKlass: ERROR00000";
//  }
  const mirror::Class* mapped_class_address =
      MapReferenceToServerChecks<mirror::Class>(class_address);
//  if(mapped_class_address == reinterpret_cast<const mirror::Object*>(GetClientSpaceEnd(KGCSpaceServerImageInd_))) {
//      LOG(FATAL) << "..... IPCServerMarkerSweep::GetMappedObjectKlass: ERROR00001";
//  }


  if(!BelongsToServerHeap<mirror::Class>(mapped_class_address)) {
    LOG(FATAL) << "IPCServerMarkerSweep::GetMappedObjectKlass..5.....Class isn't aligned: " << class_address <<
            " in object: " << mapped_obj_parm << "..mapped_class = " << mapped_class_address;
  }
  return mapped_class_address;
}


inline uint32_t IPCServerMarkerSweep::GetClassAccessFlags(const mirror::Class* klass) const {
  // Check class is loaded or this is java.lang.String that has a
  // circularity issue during loading the names of its members
  uint32_t raw_access_flag_value =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
      mirror::Class::AcessFlagsOffset());
  return raw_access_flag_value;
}

// Returns true if the class is an interface.
bool IPCServerMarkerSweep::IsInterfaceMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccInterface) != 0;
}

// Returns true if the class is declared final.
bool IPCServerMarkerSweep::IsFinalMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccFinal) != 0;
}

bool IPCServerMarkerSweep::IsFinalizableMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsFinalizable) != 0;
}

// Returns true if the class is abstract.
bool IPCServerMarkerSweep::IsAbstractMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccAbstract) != 0;
}

// Returns true if the class is an annotation.
bool IPCServerMarkerSweep::IsAnnotationMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccAnnotation) != 0;
}

// Returns true if the class is synthetic.
bool IPCServerMarkerSweep::IsSyntheticMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccSynthetic) != 0;
}

bool IPCServerMarkerSweep::IsReferenceMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsReference) != 0;
}


bool IPCServerMarkerSweep::IsWeakReferenceMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsWeakReference) != 0;
}


bool IPCServerMarkerSweep::IsSoftReferenceMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccReferenceFlagsMask) == kAccClassIsReference;
}

bool IPCServerMarkerSweep::IsFinalizerReferenceMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsFinalizerReference) != 0;
}

bool IPCServerMarkerSweep::IsPhantomReferenceMappedClass(const mirror::Class* klass) const {
  return (GetClassAccessFlags(klass) & kAccClassIsPhantomReference) != 0;
}


const mirror::Class* IPCServerMarkerSweep::GetComponentTypeMappedClass(const mirror::Class* mapped_klass) const {
  uint32_t component_raw_value =
        mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_klass),
            mirror::Class::ComponentTypeOffset());
  const mirror::Class* c = MapValueToServer<mirror::Class>(component_raw_value);
  return c;
}

bool IPCServerMarkerSweep::IsObjectArrayMappedKlass(const mirror::Class* klass) const {
  const mirror::Class* component_type = GetComponentTypeMappedClass(klass);
  if(component_type != NULL) {
    return(!IsPrimitiveMappedKlass(component_type));
  }
  return false;
}


bool IPCServerMarkerSweep::IsPrimitiveMappedKlass(const mirror::Class* klass) const {
  int32_t type_raw_value =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
          mirror::Class::PrimitiveTypeOffset());
  Primitive::Type primitive_type =
      static_cast<Primitive::Type>(type_raw_value);
  return (primitive_type != Primitive::kPrimNot);
}

int IPCServerMarkerSweep::GetMappedClassType(const mirror::Class* klass) const {

  if(!IsMappedObjectToServer<mirror::Class>(klass)) {
    LOG(FATAL) << "ERROR005...GetMappedClassType";
  }

  if(UNLIKELY(klass == cashed_references_client_.java_lang_Class_))
    return 2;

  if(GetComponentTypeMappedClass(klass) != NULL) {
    if(IsObjectArrayMappedKlass(klass))
      return 0;
    return 1;
  }
//  const byte* raw_addr = reinterpret_cast<const byte*>(klass) +
//      mirror::Class::ComponentTypeOffset().Int32Value();
//  const mirror::Class* component_type_address =
//      *reinterpret_cast<mirror::Class* const *>(raw_addr);
//  if(UNLIKELY(component_type_address != NULL)) { //this is an array
//    raw_addr = reinterpret_cast<const byte*>(klass) +
//          mirror::Class::PrimitiveTypeOffset().Int32Value();
//    const int32_t* word_addr = reinterpret_cast<const int32_t*>(raw_addr);
//    uint32_t value_read = *word_addr;
//    Primitive::Type primitive_type = static_cast<Primitive::Type>(value_read);
//    if(primitive_type != Primitive::kPrimNot)
//      return 0;
//    return -1;
//  }
  return 3;
}

inline void IPCServerMarkerSweep::MarkObject(const mirror::Object* obj) {
  if (obj != NULL) {
//    if(BelongsToOldHeap(obj)) {
//      LOG(ERROR) << "XXX ERROR - BelongsToOldHeap";//  << static_cast<void*>(obj);
//    }

    MarkObjectNonNull(obj);
  }
}

template <typename MarkVisitor>
void IPCServerMarkerSweep::ServerScanObjectVisit(const mirror::Object* obj,
    const MarkVisitor& visitor) {
  if(!BelongsToOldHeap<mirror::Object>(obj)) {
    LOG(FATAL) << "MAPPINGERROR: XXXXXXX does not belong to Heap XXXXXXXXX";
  }
  const mirror::Object* mapped_object = MapReferenceToServerChecks<mirror::Object>(obj);

  if(!IsMappedObjectToServer<mirror::Object>(mapped_object)) {
    LOG(FATAL) << "..... ServerScanObjectVisit: ERROR01";
  }
  if(mapped_object == reinterpret_cast<const mirror::Object*>(GetClientSpaceEnd(KGCSpaceServerImageInd_))) {
      LOG(FATAL) << "..... ServerScanObjectVisit: ERROR02";
  }

  const mirror::Class* mapped_klass = GetMappedObjectKlass(mapped_object);

  if(!IsMappedObjectToServer<mirror::Class>(mapped_klass)) {
    LOG(FATAL) << "..... ServerScanObjectVisit: ERROR03";
  }

  int mapped_class_type = GetMappedClassType(mapped_klass);
  if (UNLIKELY(mapped_class_type < 2)) {
    cashed_stats_client_.array_count_ += 1;
    //android_atomic_add(1, &(array_count_));
    if(mapped_class_type == 0) {
      ServerVisitObjectArrayReferences(
        down_cast<const mirror::ObjectArray<mirror::Object>*>(mapped_object), visitor);
    }
  } else if (UNLIKELY(mapped_class_type == 2)) {
    cashed_stats_client_.class_count_ += 1;
    ServerVisitClassReferences(mapped_klass, mapped_object, visitor);
  } else if (UNLIKELY(mapped_class_type == 3)) {
    cashed_stats_client_.other_count_ += 1;
    ServerVisitOtherReferences(mapped_klass, mapped_object, visitor);
    if(UNLIKELY(IsReferenceMappedClass(mapped_klass))) {

    }
  }
}


template <typename Visitor>
void IPCServerMarkerSweep::ServerVisitObjectArrayReferences(
                          const mirror::ObjectArray<mirror::Object>* mapped_arr,
                                                  const Visitor& visitor) {
  if(!(IsMappedObjectToServer<mirror::ObjectArray<mirror::Object>>(mapped_arr))) {
    LOG(FATAL) << "ServerVisitObjectArrayReferences:: 0000";
  }
  const byte* raw_object_addr = reinterpret_cast<const byte*>(mapped_arr);
  const byte* raw_addr_length_address = raw_object_addr +
               mirror::Array::LengthOffset().Int32Value();
  const size_t length =
        static_cast<size_t>(*reinterpret_cast<const int32_t*>(raw_addr_length_address));

  if(length == 0)
    return;

  const size_t width = sizeof(mirror::Object*);
  int32_t _data_offset = mirror::Array::DataOffset(width).Int32Value();
  //const byte* _raw_data_element = NULL;

  for (size_t i = 0; i < length; ++i) {//we do not need to map the element from an array
    MemberOffset offset(_data_offset + i * width);
    uint32_t _data_read =
        mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_arr),
            offset);
    const mirror::Object* element_content =
        MapValueToServer<mirror::Object>(_data_read);
    if(!(IsMappedObjectToServer<mirror::Object>(element_content))) {
      LOG(FATAL) << "ServerVisitObjectArrayReferences:: 0002";
    }

//    _raw_data_element = raw_object_addr + offset.Int32Value();
//
//    if(!(BelongsToServerHeap<byte>(_raw_data_element))) {
//      LOG(FATAL) << "ServerVisitObjectArrayReferences:: 0001";
//    }
//    const int32_t* word_addr = reinterpret_cast<const int32_t*>(_raw_data_element);
//    uint32_t _data_read = *word_addr;
//    const mirror::Object* element_content =
//        MapValueToServer<mirror::Object>(_data_read);
//    if(!(IsMappedObjectToServer<mirror::Object>(element_content))) {
//      LOG(FATAL) << "ServerVisitObjectArrayReferences:: 0002";
//    }
    visitor(mapped_arr, element_content, offset, false);
  }

}


template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitInstanceFieldsReferences(const mirror::Class* klass,
                                                     const mirror::Object* obj,
                                                     const Visitor& visitor) {
  const int32_t reference_offsets =
      mirror::Class::GetReferenceInstanceOffsetsOffset().Int32Value();
  const byte* raw_addr = reinterpret_cast<const byte*>(klass) + reference_offsets;
  const int32_t* word_addr = reinterpret_cast<const int32_t*>(raw_addr);
  uint32_t reference_instance_offsets_val = *word_addr;
  ServerVisitFieldsReferences(obj, reference_instance_offsets_val, false, visitor);
}

template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitClassReferences(
                        const mirror::Class* klass, const mirror::Object* obj,
                                            const Visitor& visitor)  {
  ServerVisitInstanceFieldsReferences(klass, obj, visitor);
  ServerVisitStaticFieldsReferences(down_cast<const mirror::Class*>(obj), visitor);

}
template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitStaticFieldsReferences(const mirror::Class* klass,
                                                   const Visitor& visitor) {
  MemberOffset reference_static_offset = mirror::Class::ReferenceStaticOffset();
  uint32_t _raw_value_offsets =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
          reference_static_offset);
  ServerVisitFieldsReferences(klass, _raw_value_offsets, true, visitor);
}


size_t IPCServerMarkerSweep::GetNumReferenceStaticFields(const mirror::Class* klass_ref) const {
  uint32_t raw_static_fields_number =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass_ref),
      mirror::Class::ReferenceStaticFieldsOffset());
  size_t mapped_value = static_cast<size_t>(raw_static_fields_number);
  return mapped_value;
}

size_t IPCServerMarkerSweep::GetNumReferenceInstanceFields(const mirror::Class* klass_ref) const {
  uint32_t raw_instance_fields_number =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass_ref),
      mirror::Class::ReferenceInstanceFieldsOffset());
  size_t mapped_value = static_cast<size_t>(raw_instance_fields_number);
  return mapped_value;
}

const mirror::Class* IPCServerMarkerSweep::GetSuperMappedClass(const mirror::Class* mapped_klass) {
  int32_t raw_super_klass = mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(mapped_klass),
      mirror::Class::SuperClassOffset());
  const mirror::Class* c = MapValueToServer<mirror::Class>(raw_super_klass);
  return c;
}


const mirror::ArtField* IPCServerMarkerSweep::ServerClassGetInstanceField(const mirror::Class* klass, uint32_t i) {
  uint32_t instance_fields_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
      mirror::Class::GetInstanceFieldsOffset());
  const mirror::ObjectArray<mirror::ArtField>* instance_fields =
      MapValueToServer<mirror::ObjectArray<mirror::ArtField>>(instance_fields_raw);
  MemberOffset data_offset(mirror::Array::DataOffset(sizeof(mirror::Object*)).Int32Value()
      + i * sizeof(mirror::Object*));
  uint32_t instance_field_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(instance_fields),
          data_offset);
  const mirror::ArtField* mapped_art_field =
      MapValueToServer<mirror::ArtField>(instance_field_raw);

  return mapped_art_field;
}


inline const mirror::ArtField* IPCServerMarkerSweep::ServerClassGetStaticField(
    const mirror::Class* klass, uint32_t i) {
  uint32_t static_fields_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(klass),
      mirror::Class::GetStaticFieldsOffset());
  const mirror::ObjectArray<mirror::ArtField>* static_fields =
      MapValueToServer<mirror::ObjectArray<mirror::ArtField>>(static_fields_raw);

  MemberOffset data_offset(mirror::Array::DataOffset(sizeof(mirror::Object*)).Int32Value()
      + i * sizeof(mirror::Object*));


  uint32_t static_field_raw =
      mirror::Object::GetRawValueFromObject(reinterpret_cast<const mirror::Object*>(static_fields),
          data_offset);
  const mirror::ArtField* mapped_art_field =
      MapValueToServer<mirror::ArtField>(static_field_raw);


  return mapped_art_field;
}

template <typename Visitor>
void IPCServerMarkerSweep::ServerVisitOtherReferences(const mirror::Class* klass, const mirror::Object* obj,
                                 const Visitor& visitor) {
  ServerVisitInstanceFieldsReferences(klass, obj, visitor);
}


template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitFieldsReferences(const mirror::Object* obj,
            uint32_t ref_offsets, bool is_static, const Visitor& visitor) {
  if (LIKELY(ref_offsets != CLASS_WALK_SUPER)) {
    // Found a reference offset bitmap.  Mark the specified offsets.
#ifndef MOVING_COLLECTOR
    // Clear the class bit since we mark the class as part of marking the classlinker roots.
    ref_offsets &= (1U << (sizeof(ref_offsets) * 8 - 1)) - 1;
#endif
    while (ref_offsets != 0) {
      size_t right_shift = CLZ(ref_offsets);
      MemberOffset field_offset = CLASS_OFFSET_FROM_CLZ(right_shift);
      uint32_t raw_fiel_value =
          mirror::Object::GetRawValueFromObject(obj, field_offset);
      const mirror::Object* mapped_field_object  =
          MapValueToServer<mirror::Object>(raw_fiel_value);
      visitor(obj, mapped_field_object, field_offset, is_static);
      ref_offsets &= ~(CLASS_HIGH_BIT >> right_shift);
    }
  } else {
    // There is no reference offset bitmap.  In the non-static case,
    // walk up the class inheritance hierarchy and find reference
    // offsets the hard way. In the static case, just consider this
    // class.
    for (const mirror::Class* klass = is_static ? down_cast<const mirror::Class*>(obj) : GetMappedObjectKlass(obj);
         klass != NULL;
         klass = is_static ? NULL : GetSuperMappedClass(klass)) {
      size_t num_reference_fields = (is_static
                                     ? GetNumReferenceStaticFields(klass)
                                     : GetNumReferenceInstanceFields(klass));
      for (size_t i = 0; i < num_reference_fields; ++i) {
        const mirror::ArtField* field = (is_static ? ServerClassGetStaticField(klass, i)
                                   : ServerClassGetInstanceField(klass, i));
        uint32_t field_word_value =
            mirror::Object::GetRawValueFromObject(field, mirror::ArtField::OffsetOffset());
        MemberOffset field_offset(field_word_value);
        uint32_t raw_field_value =
            mirror::Object::GetRawValueFromObject(obj, field_offset);
        const mirror::Object* mapped_field_object =
            MapValueToServer<mirror::Object>(raw_field_value);
        visitor(obj, mapped_field_object, field_offset, is_static);
      }
    }
  }
}

inline void IPCServerMarkerSweep::MarkObjectNonNull(const mirror::Object* obj) {
  DCHECK(obj != NULL);

  if(!IsMappedObjectToServer<mirror::Object>(obj)) {
    LOG(FATAL) << "IPCServerMarkerSweep::MarkObjectNonNull.." << obj;
  }
  if (IsImmune(obj)) {
    return;
  }

//  // Try to take advantage of locality of references within a space, failing this find the space
//  // the hard way.
//  accounting::BaseBitmap* object_bitmap = current_mark_bitmap_;
//  if (UNLIKELY(!object_bitmap->HasAddress(obj))) {
//    accounting::BaseBitmap* new_bitmap =
//        heap_->GetMarkBitmap()->GetContinuousSpaceBitmap(obj);
//    if (LIKELY(new_bitmap != NULL)) {
//      object_bitmap = new_bitmap;
//    } else {
//      MarkLargeObject(obj, true);
//      return;
//    }
//  }
//
//  // This object was not previously marked.
//  if (!object_bitmap->Test(obj)) {
//    object_bitmap->Set(obj);
//    if (UNLIKELY(mark_stack_->Size() >= mark_stack_->Capacity())) {
//      // Lock is not needed but is here anyways to please annotalysis.
//      MutexLock mu(Thread::Current(), mark_stack_lock_);
//      ExpandMarkStack();
//    }
//    // The object must be pushed on to the mark stack.
//    mark_stack_->PushBack(const_cast<mirror::Object*>(obj));
//  }
}
//
//template <class TypeRef>
//inline TypeRef* IPCServerMarkerSweep::ServerMapHeapReference(TypeRef* ptr_param) {
//  if(ptr_param == NULL)
//    return ptr_param;
//
//  if(!BelongsToOldHeap<TypeRef>(ptr_param)) {
//    LOG(ERROR) << "0--------Checking inside the mapper return inconsistent things: " <<
//        static_cast<void*>(ptr_param) << ", " << StringPrintf("string %p", ptr_param);
//
//  }
//
//  TypeRef* copiedValue = ptr_param;
//  byte* casted_param = reinterpret_cast<byte*>(copiedValue);
//  byte* calculated_param = casted_param;
//
//  bool xored_value  = 0;
//  xored_value = (BelongsToOldHeap<byte>(casted_param)) ^
//      (BelongsToOldHeap<mirror::Object>(ptr_param));
//
//  if(xored_value == 1) {
//    LOG(ERROR) << "1--------Checking inside the mapper return inconsistent things: " <<
//        reinterpret_cast<void*>(casted_param) << ", original parametter: " <<
//        static_cast<void*>(ptr_param) << ", belong_orig? " <<
//        BelongsToOldHeap<TypeRef>(ptr_param) << ", belong_char? " <<
//        BelongsToOldHeap<byte>(casted_param) << ", belong_copied? " <<
//        BelongsToOldHeap<TypeRef>(copiedValue);
//    //LOG(FATAL) << "XXXX Terminate execution on service side";
//  }
//  bool _found = false;
//  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
//    if((casted_param < spaces_[i].client_end_)
//        && (casted_param >= spaces_[i].client_base_)) {
//      calculated_param = (casted_param + offset_);
//      _found = true;
//      break;
//    }
//  }
//
//  if(!_found) {
//    xored_value = (BelongsToOldHeap<byte>(casted_param)) ^
//        (BelongsToOldHeap<mirror::Object>(ptr_param));
//
//    if(xored_value == 1) {
//      LOG(ERROR) << "2--------Checking inside was not found:" <<
//          reinterpret_cast<void*>(casted_param) << ", original parametter: " <<
//          static_cast<void*>(ptr_param) << ", belong_orig? " <<
//          BelongsToOldHeap<mirror::Object>(ptr_param) << ", belong_char? " <<
//          BelongsToOldHeap<byte>(casted_param) << ", belong_copied? " <<
//          BelongsToOldHeap<mirror::Object>(copiedValue) << ", calculated_param: " <<
//          BelongsToOldHeap<byte>(calculated_param);
//    }
//
//    if(!BelongsToOldHeap<byte>(casted_param)) {
//      LOG(ERROR) << "3--------Checking inside was not found:" <<
//          reinterpret_cast<void*>(casted_param) << ", original parametter: " <<
//          static_cast<void*>(ptr_param) << ", belong_orig? " <<
//          BelongsToOldHeap<mirror::Object>(ptr_param) << ", belong_char? " <<
//          BelongsToOldHeap<byte>(casted_param) << ", belong_copied? " <<
//          BelongsToOldHeap<mirror::Object>(copiedValue) << ", calculated_param: " <<
//          BelongsToOldHeap<byte>(calculated_param);
//    }
//
//  }
//
//
//
//  return ptr_param;
//
////  if((BelongsToOldHeap<byte>(casted_param)) != )
////
////
////  if(BelongsToOldHeap<byte>(casted_param)) {
////
////    if(casted_param < spaces_[KGCSpaceServerImageInd_].client_end_)
////      return ptr_param;
////    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
////      if(casted_param < spaces_[i].client_end_
////          && casted_param >= spaces_[i].client_base_) {
////        return reinterpret_cast<TypeRef*>(casted_param + offset_);
////        break;
////      }
////    }
////
////    //at this point there should be an error
////    LOG(ERROR) << "--------Could not map Object: " <<
////        reinterpret_cast<void*>(casted_param) << ", original parametter: " <<
////        static_cast<void*>(ptr_param);
////
////    LOG(FATAL) << "Terminate execution on service side";
////  }
////  LOG(ERROR) << "--------Checking inside the mapper return nothing: " <<
////      reinterpret_cast<void*>(casted_param) << ", original parametter: " <<
////      static_cast<void*>(ptr_param) << ", belong? " <<
////      BelongsToOldHeap<mirror::Object>(ptr_param);
////  LOG(FATAL) << "XXXX Terminate execution on service side";
////  return NULL;
//}









//inline mirror::Object* IPCServerMarkerSweep::MapClientReference(mirror::Object* obj) {
//  return ServerMapHeapReference<mirror::Object>(obj);
//
////  if(obj == NULL)
////    return obj;
////  byte* casted_object = reinterpret_cast<byte*>(obj);
////  if(casted_object > spaces_[KGCSpaceServerImageInd_].client_end_) {
////    bool _found = false;
////    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
////      if(casted_object < spaces_[i].client_end_) {
////        casted_object = casted_object + offset_;
////        _found = true;
////        break;
////      }
////    }
////    if(!_found) {
////      LOG(FATAL) << "--------Could not map Object: " <<
////          reinterpret_cast<void*>(casted_object);
////      return NULL;
////    }
////  } else {
////    return obj;
////  }
////  return reinterpret_cast<mirror::Object*>(casted_object);
//}

///* it assumes that the class is already mapped */
//inline mirror::Class* IPCServerMarkerSweep::GetClientClassFromObject(mirror::Object* obj) {
//  byte* raw_addr = reinterpret_cast<byte*>(obj) +
//        mirror::Object::ClassOffset().Int32Value();
//  mirror::Class* klass = *reinterpret_cast<mirror::Class**>(raw_addr);
//  if(!BelongsToOldHeap(klass)) {
//    LOG(ERROR) << "MAPPINGERROR: XXXXXXX Original KLASS does not belong to Original Heap NULL XXXXXXXXX";
//  }
//
////  mirror::Class* klass = obj->GetClass();
//
////  mirror::Class* mapped_klass = ServerMapHeapReference<mirror::Class>(klass);
////  if(!IsMappedObjectToServer(mapped_klass)) {
////    LOG(ERROR) << "MAPPINGERROR: XXXXXXX KLASS does not belong to new heap XXXXXXXXX";
////  }
//
//  return klass;
//}
//



// Used to mark objects when recursing.  Recursion is done by moving
// the finger across the bitmaps in address order and marking child
// objects.  Any newly-marked objects whose addresses are lower than
// the finger won't be visited by the bitmap scan, so those objects
// need to be added to the mark stack.



//template <typename Visitor>
//inline void IPCServerMarkerSweep::ServerVisitObjectArrayReferences(
//                                    mirror::ObjectArray<mirror::Object>* array,
//                                                  const Visitor& visitor) {
//  byte* raw_object_addr = reinterpret_cast<byte*>(array);
//
//  if(!(IsMappedObjectToServer(raw_object_addr))) {
//    LOG(ERROR) << "XXXXX Invalid MAPPING Of array Object XXXXXX " <<
//        static_cast<void*>(raw_object_addr);
//  }
//
//  byte* raw_addr_length_address = raw_object_addr +
//             mirror::Array::LengthOffset().Int32Value();
//  const size_t length =
//      static_cast<size_t>(*reinterpret_cast<int32_t*>(raw_addr_length_address));
//
//  if(length == 0)
//    return;
//
////  byte* raw_addr = reinterpret_cast<byte*>(obj) +
////        mirror::Object::ClassOffset().Int32Value();
////  mirror::Class* klass = *reinterpret_cast<mirror::Class**>(raw_addr);
//
//  if(false) {
//
//  const size_t width = sizeof(mirror::Object*);
//  int32_t _data_offset = mirror::Array::DataOffset(width).Int32Value();
//  byte* _raw_data_element = NULL;
//
//
//  //int32_t _data_offset = mirror::Array::DataOffset(width).Int32Value();
//  for (size_t i = 0; i < length; ++i) {//we do not need to map the element from an array
//    if(false) {
//      MemberOffset offset(_data_offset + i * width);
//      _raw_data_element = raw_object_addr + offset.Int32Value();
//      int32_t* word_addr = reinterpret_cast<int32_t*>(_raw_data_element);
// //   uint32_t _data_read = *word_addr;
//
////    mirror::Object* object = reinterpret_cast<mirror::Object*>(_data_read);
//
//
//      if(!(WithinServerHeapAddresses<int32_t>(word_addr))) {
//        LOG(ERROR) << "XXXXX Invalid MAPPING for element array int 32 XXXXXX " <<
//            static_cast<void*>(word_addr) << ", array length = " << length <<
//            " array_address = " << static_cast<void*>(raw_object_addr);
//      }
//    }
////    mirror::Object* element = *reinterpret_cast<mirror::Object**>(element_32);
////            const_cast<mirror::Object*>(array->GetWithoutChecksNoLocks(static_cast<int32_t>(i)));
// //   mirror::Object* mapped_element = MapClientReference(element);
////    if(!(IsMappedObjectToServer(mapped_element))) {
////      LOG(ERROR) << "XXXXX Invalid MAPPING for element array XXXXXX ";
////    }
//
////    if(false)
////      visitor(array, mapped_element, offset, false);
//
//  }
//}
//}



//
//template <typename Visitor>
//inline void IPCServerMarkerSweep::ServerVisitInstanceFieldsReferences(mirror::Class* klass,
//                                                     mirror::Object* obj,
//                                                     const Visitor& visitor) {
//  DCHECK(obj != NULL);
//  DCHECK(klass != NULL);
//  ServerVisitFieldsReferences(obj, klass->GetReferenceInstanceOffsets(), false, visitor);
//}
//
//inline mirror::Class* IPCServerMarkerSweep::ServerClassGetSuperClass(mirror::Class* klass) {
//  mirror::Class* super_klass = klass->GetSuperClassNoLock();
//  super_klass = ServerMapHeapReference<mirror::Class>(super_klass);
//  return super_klass;
//}
//

//

//
//template <typename Visitor>
//inline void IPCServerMarkerSweep::ServerVisitFieldsReferences(
//                                        mirror::Object* obj, uint32_t ref_offsets,
//                                             bool is_static, const Visitor& visitor) {
//  if (LIKELY(ref_offsets != CLASS_WALK_SUPER)) {
//#ifndef MOVING_COLLECTOR
//    // Clear the class bit since we mark the class as part of marking the classlinker roots.
//    DCHECK_EQ(mirror::Object::ClassOffset().Uint32Value(), 0U);
//    ref_offsets &= (1U << (sizeof(ref_offsets) * 8 - 1)) - 1;
//#endif
//    while (ref_offsets != 0) {
//      size_t right_shift = CLZ(ref_offsets);
//      MemberOffset field_offset = CLASS_OFFSET_FROM_CLZ(right_shift);
//      const mirror::Object* ref =
//          obj->GetFieldObject<const mirror::Object*>(field_offset, false);
//      visitor(obj, const_cast<mirror::Object*>(ref), field_offset, is_static);
//      ref_offsets &= ~(CLASS_HIGH_BIT >> right_shift);
//    }
//  } else {
//    // There is no reference offset bitmap.  In the non-static case,
//    // walk up the class inheritance hierarchy and find reference
//    // offsets the hard way. In the static case, just consider this
//    // class.
////    for (mirror::Class* klass = is_static ? obj->AsClass() : GetClientClassFromObject(obj);
////         klass != NULL;
////         klass = is_static ? NULL : ServerClassGetSuperClass(klass)) {
////      size_t num_reference_fields = (is_static
////                                     ? klass->NumReferenceStaticFields()
////                                     : klass->NumReferenceInstanceFields());
////      if(false) {
////        LOG(ERROR) << "static fields " << num_reference_fields;
////      }
//////      for (size_t i = 0; i < num_reference_fields; ++i) {
//////        mirror::ArtField* field = (is_static ? ServerClassGetStaticField(klass, i)
//////                                   : ServerClassGetInstanceField(klass,i));
//////        MemberOffset field_offset = field->GetOffset();
//////        const mirror::Object* ref = obj->GetFieldObject<const mirror::Object*>(field_offset, false);
//////        visitor(obj, const_cast<mirror::Object*>(ref), field_offset, is_static);
//////      }
////    }
//  }
//}
}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_ */
