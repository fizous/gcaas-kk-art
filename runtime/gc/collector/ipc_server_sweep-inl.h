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

namespace art {
namespace gc {
namespace collector {


inline mirror::Object* IPCServerMarkerSweep::MapClientReference(mirror::Object* obj) {
  if(obj == NULL)
    return obj;
  byte* casted_object = reinterpret_cast<byte*>(obj);
  if(casted_object > spaces_[KGCSpaceServerImageInd_].client_end_) {
    bool _found = false;
    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
      if(casted_object < spaces_[i].client_end_) {
        casted_object = casted_object + offset_;
        _found = true;
        break;
      }
    }
    if(!_found) {
      LOG(FATAL) << "--------Could not map Object: " <<
          reinterpret_cast<void*>(casted_object);
      return NULL;
    }
  } else {
    return obj;
  }
  return reinterpret_cast<mirror::Object*>(casted_object);
}

/* it assumes that the class is already mapped */
inline mirror::Class* IPCServerMarkerSweep::GetClientClassFromObject(mirror::Object* obj) {
  mirror::Class* klass = obj->GetClass();
  byte* casted_klass = reinterpret_cast<byte*>(klass);
  if(casted_klass > spaces_[KGCSpaceServerImageInd_].client_end_) {
    bool _found = false;
    for(int i = KGCSpaceServerZygoteInd_; i <= KGCSpaceServerAllocInd_; i++) {
      if(casted_klass < spaces_[i].client_end_) {
        casted_klass = casted_klass + offset_;
        _found = true;
        break;
      }
    }
    if(!_found) {
      LOG(FATAL) << "--------Could not Get Class from Object: " <<
          reinterpret_cast<void*>(obj) << ", klass:" << reinterpret_cast<void*>(klass) <<
          ", mapped_class: " << reinterpret_cast<void*>(casted_klass);
      return NULL;
    }
  } else {
    return klass;
  }
  return reinterpret_cast<mirror::Class*>(casted_klass);
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
    LOG(FATAL) << StringPrintf("XXXX Class is Null....objAddr: %p XXXXXXXXX:",
        obj);
    return;
  }

  if (UNLIKELY(klass->IsArrayClass())) {
    if (klass->IsObjectArrayClass()) {
      ServerVisitObjectArrayReferences(obj->AsObjectArray<mirror::Object>(), visitor);
    }
  } else if (UNLIKELY(klass == java_lang_Class_client_)) {
    //VisitClassReferences(klass, obj, visitor);
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
  for (size_t i = 0; i < length; ++i) {
    const mirror::Object* element = array->GetWithoutChecks(static_cast<int32_t>(i));
    const size_t width = sizeof(mirror::Object*);
    MemberOffset offset(i * width + mirror::Array::DataOffset(width).Int32Value());
    visitor(array, element, offset, false);
  }
}


template <typename Visitor>
inline void IPCServerMarkerSweep::ServerVisitClassReferences(
                        const mirror::Class* klass, const mirror::Object* obj,
                                            const Visitor& visitor)  {
//  VisitInstanceFieldsReferences(klass, obj, visitor);
//  VisitStaticFieldsReferences(obj->AsClass(), visitor);
}

}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_INL_H_ */
