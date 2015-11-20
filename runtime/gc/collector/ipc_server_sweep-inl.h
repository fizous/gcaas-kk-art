/*
 * ipc_server_sweep-inl.h
 *
 *  Created on: Nov 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_
#define ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_

#include "gc/collector/ipc_server_sweep.h"

#include "gc/heap.h"
#include "mirror/art_field.h"
#include "mirror/class.h"
#include "mirror/object_array.h"
#include "mirror/art_field.h"

#include "mirror/art_field-inl.h"

namespace art {
namespace gc {
namespace collector {


byte* IPCServerMarkerSweep::GetClientSpaceEnd(int index) const {
  return spaces_[index].client_end_;
}
byte* IPCServerMarkerSweep::GetClientSpaceBegin(int index) const {
  return spaces_[index].client_base_;
}


template <class TypeRef>
inline bool IPCServerMarkerSweep::IsValidObjectForServer(TypeRef* ptr_param) {
  return (BelongsToOldHeap(ptr_param) || IsMappedObjectToServer(ptr_param));

}


template <class referenceKlass>
const referenceKlass* IPCServerMarkerSweep::MapReferenceToServer(const referenceKlass* ref_parm) {
  if(ref_parm == NULL)
    return ref_parm;
  const byte* casted_param = reinterpret_cast<const byte*>(ref_parm);
  if(casted_param < GetClientSpaceEnd(KGCSpaceServerImageInd_)) {
    return ref_parm;
  }
  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetClientSpaceEnd(i)) &&
        (casted_param >= GetClientSpaceBegin(i))) {
      return reinterpret_cast<const referenceKlass*>(casted_param + offset_);
    }
  }

  LOG(FATAL) << "..... MapClientReference: ERROR0";
  return ref_parm;
}

const mirror::Object* IPCServerMarkerSweep::MapClientReference(const mirror::Object* obj_parm) {
  if(obj_parm == NULL)
    return obj_parm;
  const byte* casted_param = reinterpret_cast<const byte*>(obj_parm);
  if(casted_param < GetClientSpaceEnd(KGCSpaceServerImageInd_)) {
    return obj_parm;
  }
  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetClientSpaceEnd(i)) &&
        (casted_param >= GetClientSpaceBegin(i))) {
      return reinterpret_cast<const mirror::Object*>(casted_param + offset_);
    }
  }

  LOG(ERROR) << "..... MapClientReference: ERROR0";
  return obj_parm;
}


template <class referenceKlass>
bool IPCServerMarkerSweep::BelongsToOldHeap(const referenceKlass* ptr_param) const {
  if(ptr_param == NULL)
    return true;
  if(!IsAligned<kObjectAlignment>(ptr_param))
    return false;
  const byte* casted_param = reinterpret_cast<const byte*>(ptr_param);
  if(casted_param < GetClientSpaceEnd(KGCSpaceServerImageInd_)) {
    return true;
  }
  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < GetClientSpaceEnd(i)) &&
        (casted_param >= GetClientSpaceBegin(i))) {
      return true;
    }
  }
  return false;
}

inline const mirror::Class* IPCServerMarkerSweep::GetMappedObjectKlass(const mirror::Object* mapped_obj_parm) {
  const byte* raw_addr_class = reinterpret_cast<const byte*>(mapped_obj_parm) +
      mirror::Object::ClassOffset().Int32Value();
  const mirror::Class* class_address = *reinterpret_cast<mirror::Class* const *>(raw_addr_class);
  if (UNLIKELY(class_address == NULL)) {
    LOG(FATAL) << "ERROR2....Null class in object: " << mapped_obj_parm;
  } else if (UNLIKELY(!IsAligned<kObjectAlignment>(class_address))) {
    LOG(FATAL) << "ERROR3.....Class isn't aligned: " << class_address <<
        " in object: " << mapped_obj_parm;
  } else if(!BelongsToOldHeap<mirror::Class>(class_address)) {
    LOG(FATAL) << "ERROR4.....Class isn't aligned: " << class_address <<
        " in object: " << mapped_obj_parm;
  }

  const mirror::Class* mapped_class_address =
      MapReferenceToServer<mirror::Class>(class_address);
  return mapped_class_address;
}


int IPCServerMarkerSweep::GetMappedClassType(const mirror::Class* klass) const {
  return 0;
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


template <class TypeRef>
inline bool IPCServerMarkerSweep::WithinServerHeapAddresses(TypeRef* ptr_param) {
  if(ptr_param == NULL)
    return true;
  byte* casted_param = reinterpret_cast<byte*>(ptr_param);
  if(casted_param < spaces_[KGCSpaceServerImageInd_].client_end_) {
    return true;
  }
  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < spaces_[i].base_end_) &&
        (casted_param >= spaces_[i].base_)) {
      return true;
    }
  }
  return false;
}

template <class TypeRef>
inline bool IPCServerMarkerSweep::IsMappedObjectToServer(TypeRef* ptr_param) {
  if(ptr_param == NULL)
    return true;
  if(!IsAligned<kObjectAlignment>(ptr_param))
    return false;
  byte* casted_param = reinterpret_cast<byte*>(ptr_param);
  if(casted_param < spaces_[KGCSpaceServerImageInd_].client_end_) {
    return true;
  }
  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < spaces_[i].base_end_) &&
        (casted_param >= spaces_[i].base_)) {
      return true;
    }
  }
  return false;
}




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

inline void IPCServerMarkerSweep::MarkObjectNonNull(mirror::Object* obj) {
  DCHECK(obj != NULL);

  if (IsImmune(obj)) {
//    DCHECK(IsMarked(obj));
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

// Used to mark objects when recursing.  Recursion is done by moving
// the finger across the bitmaps in address order and marking child
// objects.  Any newly-marked objects whose addresses are lower than
// the finger won't be visited by the bitmap scan, so those objects
// need to be added to the mark stack.
inline void IPCServerMarkerSweep::MarkObject(mirror::Object* obj) {
  if (obj != NULL) {
//    if(BelongsToOldHeap(obj)) {
//      LOG(ERROR) << "XXX ERROR - BelongsToOldHeap";//  << static_cast<void*>(obj);
//    }

    MarkObjectNonNull(obj);
  }
}

template <typename MarkVisitor>
inline void IPCServerMarkerSweep::ServerScanObjectVisit(const mirror::Object* obj,
    const MarkVisitor& visitor) {
  if(!BelongsToOldHeap<mirror::Object>(obj)) {
    LOG(FATAL) << "MAPPINGERROR: XXXXXXX does not belong to Heap XXXXXXXXX";
  }
  const mirror::Object* mapped_object = MapReferenceToServer<mirror::Object>(obj);
  if(mapped_object == reinterpret_cast<const mirror::Object*>(GetClientSpaceEnd(KGCSpaceServerImageInd_))) {
      LOG(FATAL) << "..... ServerScanObjectVisit: ERROR1";
  }

  const mirror::Class* original_klass = GetMappedObjectKlass(mapped_object);
  if(false) {
    if(!BelongsToOldHeap<mirror::Class>(original_klass)) {
      LOG(FATAL) << "..... ServerScanObjectVisit: ERROR5";
    }
  }
}


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


//template <typename Visitor>
//inline void IPCServerMarkerSweep::ServerVisitClassReferences(
//                        mirror::Class* klass, mirror::Object* obj,
//                                            const Visitor& visitor)  {
//  ServerVisitInstanceFieldsReferences(klass, obj, visitor);
////  VisitStaticFieldsReferences(obj->AsClass(), visitor);
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
//inline mirror::ArtField* IPCServerMarkerSweep::ServerClassGetStaticField(mirror::Class* klass,
//    uint32_t i) {
//  mirror::ArtField* mapped_field = klass->GetStaticFieldNoLock(i);
//  mapped_field = ServerMapHeapReference<mirror::ArtField>(mapped_field);
//
//  return mapped_field;
//}
//
//inline mirror::ArtField* IPCServerMarkerSweep::ServerClassGetInstanceField(mirror::Class* klass, uint32_t i) {
//  mirror::ArtField* mapped_field = klass->GetInstanceFieldNoLock(i);
//  mapped_field = ServerMapHeapReference<mirror::ArtField>(mapped_field);
//
//  return mapped_field;
//}
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
