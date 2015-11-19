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


template <typename TypeRef>
inline bool IPCServerMarkerSweep::BelongsToOldHeap(TypeRef* ptr_param) {
  byte* casted_param = reinterpret_cast<byte*>(ptr_param);
  if(casted_param < spaces_[KGCSpaceServerImageInd_].client_end_) {
    return false;
  }
  for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param < spaces_[i].client_end_) &&
        (casted_param >= spaces_[i].client_base_)) {
      return true;
    }
  }
  return false;
}

template <typename TypeRef>
inline bool IPCServerMarkerSweep::IsMappedObjectToServer(TypeRef* ptr_param) {
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


template <typename TypeRef>
inline TypeRef* IPCServerMarkerSweep::ServerMapHeapReference(TypeRef* ptr_param) {
  if(ptr_param == NULL)
    return ptr_param;
  if(IsMappedObjectToServer(ptr_param)) {
    return ptr_param;
  }

  byte* casted_param = reinterpret_cast<byte*>(ptr_param);
  if(casted_param > spaces_[KGCSpaceServerImageInd_].client_end_) {
    bool _found = false;
    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
      if(casted_param < spaces_[i].client_end_ &&
          casted_param >= spaces_[i].client_base_) {
        _found = true;
        casted_param = casted_param + offset_;
        break;
      }
    }
    if(!_found) {
//      LOG(ERROR) << "--------Could not map Object: " <<
//          reinterpret_cast<void*>(casted_param);
      return ptr_param;
    }
  } else {
    return ptr_param;
  }
  return reinterpret_cast<TypeRef*>(casted_param);

}

inline mirror::Object* IPCServerMarkerSweep::MapClientReference(mirror::Object* obj) {
  return ServerMapHeapReference(obj);

//  if(obj == NULL)
//    return obj;
//  byte* casted_object = reinterpret_cast<byte*>(obj);
//  if(casted_object > spaces_[KGCSpaceServerImageInd_].client_end_) {
//    bool _found = false;
//    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
//      if(casted_object < spaces_[i].client_end_) {
//        casted_object = casted_object + offset_;
//        _found = true;
//        break;
//      }
//    }
//    if(!_found) {
//      LOG(FATAL) << "--------Could not map Object: " <<
//          reinterpret_cast<void*>(casted_object);
//      return NULL;
//    }
//  } else {
//    return obj;
//  }
//  return reinterpret_cast<mirror::Object*>(casted_object);
}

/* it assumes that the class is already mapped */
inline mirror::Class* IPCServerMarkerSweep::GetClientClassFromObject(mirror::Object* obj) {
  mirror::Class* klass = obj->GetClass();
  return ServerMapHeapReference(klass);
//  byte* casted_klass = reinterpret_cast<byte*>(klass);
//  if(casted_klass > spaces_[KGCSpaceServerImageInd_].client_end_) {
//    bool _found = false;
//    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
//      if(casted_klass < spaces_[i].client_end_) {
//        casted_klass = casted_klass + offset_;
//        _found = true;
//        break;
//      }
//    }
//    if(!_found) {
//      LOG(FATAL) << "--------Could not Get Class from Object: " <<
//          reinterpret_cast<void*>(obj) << ", klass:" << reinterpret_cast<void*>(klass) <<
//          ", mapped_class: " << reinterpret_cast<void*>(casted_klass);
//      return NULL;
//    }
//  } else {
//    return klass;
//  }
//  return reinterpret_cast<mirror::Class*>(casted_klass);
}


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
    if(BelongsToOldHeap(obj)) {
      LOG(ERROR) << "XXX ERROR - BelongsToOldHeap";//  << static_cast<void*>(obj);
    }

    MarkObjectNonNull(obj);
  }
}

template <typename MarkVisitor>
inline void IPCServerMarkerSweep::ServerScanObjectVisit(mirror::Object* obj,
    const MarkVisitor& visitor) {
  mirror::Object* mapped_obj = MapClientReference(obj);

  mirror::Class* klass =
      GetClientClassFromObject(mapped_obj);

  if(klass == NULL) {
    LOG(ERROR) << "XXXXXXX Klass NULL XXXXXXXXX";
    return;
  }

  if (UNLIKELY(klass->IsArrayClass())) {
    if (klass->IsObjectArrayClass()) {
      ServerVisitObjectArrayReferences(obj->AsObjectArray<mirror::Object>(), visitor);
    }
  } else if (UNLIKELY(klass == java_lang_Class_client_)) {
    //ServerVisitClassReferences(klass, obj, visitor);
  } else {
    //VisitOtherReferences(klass, obj, visitor);
    if (UNLIKELY(klass->IsReferenceClass())) {
    //  DelayReferenceReferent(klass, const_cast<mirror::Object*>(obj));
    }
  }
}


template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitObjectArrayReferences(
                                    mirror::ObjectArray<mirror::Object>* array,
                                                  const Visitor& visitor) {
  const size_t length = static_cast<size_t>(array->GetLength());
  for (size_t i = 0; i < length; ++i) {//we do not need to map the element from an array
    mirror::Object* element =
       // MapClientReference(
            const_cast<mirror::Object*>(array->GetWithoutChecksNoLocks(static_cast<int32_t>(i)));
            //);

    size_t width = sizeof(mirror::Object*);
    MemberOffset offset(i * width + mirror::Array::DataOffset(width).Int32Value());


    if(offset.Uint32Value() == 0) {

    }
//    element = ServerMapHeapReference(element);
//    if(BelongsToOldHeap(element)) {
//      LOG(ERROR) << "XXX ERROR - ServerVisitObjectArrayReferences";//  << static_cast<void*>(obj);
//    }
    //visitor(array, element, offset, false);
  }
}


template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitClassReferences(
                        mirror::Class* klass, mirror::Object* obj,
                                            const Visitor& visitor)  {
  ServerVisitInstanceFieldsReferences(klass, obj, visitor);
//  VisitStaticFieldsReferences(obj->AsClass(), visitor);
}

template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitInstanceFieldsReferences(mirror::Class* klass,
                                                     mirror::Object* obj,
                                                     const Visitor& visitor) {
  DCHECK(obj != NULL);
  DCHECK(klass != NULL);
  ServerVisitFieldsReferences(obj, klass->GetReferenceInstanceOffsets(), false, visitor);
}

inline mirror::Class* IPCServerMarkerSweep::ServerClassGetSuperClass(mirror::Class* klass) {
  mirror::Class* super_klass = klass->GetSuperClassNoLock();
  super_klass = ServerMapHeapReference(super_klass);
  return super_klass;
}

inline mirror::ArtField* IPCServerMarkerSweep::ServerClassGetStaticField(mirror::Class* klass,
    uint32_t i) {
  mirror::ArtField* mapped_field = klass->GetStaticFieldNoLock(i);
  mapped_field = ServerMapHeapReference(mapped_field);

  return mapped_field;
}

inline mirror::ArtField* IPCServerMarkerSweep::ServerClassGetInstanceField(mirror::Class* klass, uint32_t i) {
  mirror::ArtField* mapped_field = klass->GetInstanceFieldNoLock(i);
  mapped_field = ServerMapHeapReference(mapped_field);

  return mapped_field;
}

template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitFieldsReferences(
                                        mirror::Object* obj, uint32_t ref_offsets,
                                             bool is_static, const Visitor& visitor) {
  if (LIKELY(ref_offsets != CLASS_WALK_SUPER)) {
#ifndef MOVING_COLLECTOR
    // Clear the class bit since we mark the class as part of marking the classlinker roots.
    DCHECK_EQ(mirror::Object::ClassOffset().Uint32Value(), 0U);
    ref_offsets &= (1U << (sizeof(ref_offsets) * 8 - 1)) - 1;
#endif
    while (ref_offsets != 0) {
      size_t right_shift = CLZ(ref_offsets);
      MemberOffset field_offset = CLASS_OFFSET_FROM_CLZ(right_shift);
      const mirror::Object* ref =
          obj->GetFieldObject<const mirror::Object*>(field_offset, false);
      visitor(obj, const_cast<mirror::Object*>(ref), field_offset, is_static);
      ref_offsets &= ~(CLASS_HIGH_BIT >> right_shift);
    }
  } else {
    // There is no reference offset bitmap.  In the non-static case,
    // walk up the class inheritance hierarchy and find reference
    // offsets the hard way. In the static case, just consider this
    // class.
//    for (mirror::Class* klass = is_static ? obj->AsClass() : GetClientClassFromObject(obj);
//         klass != NULL;
//         klass = is_static ? NULL : ServerClassGetSuperClass(klass)) {
//      size_t num_reference_fields = (is_static
//                                     ? klass->NumReferenceStaticFields()
//                                     : klass->NumReferenceInstanceFields());
//      if(false) {
//        LOG(ERROR) << "static fields " << num_reference_fields;
//      }
////      for (size_t i = 0; i < num_reference_fields; ++i) {
////        mirror::ArtField* field = (is_static ? ServerClassGetStaticField(klass, i)
////                                   : ServerClassGetInstanceField(klass,i));
////        MemberOffset field_offset = field->GetOffset();
////        const mirror::Object* ref = obj->GetFieldObject<const mirror::Object*>(field_offset, false);
////        visitor(obj, const_cast<mirror::Object*>(ref), field_offset, is_static);
////      }
//    }
  }
}
}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_ */
