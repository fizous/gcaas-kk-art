/*
 * compactor.cc
 *
 *  Created on: Apr 7, 2016
 *      Author: hussein
 */

#include "thread.h"
#include "thread_list.h"
#include "thread-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc/accounting/space_bitmap-inl.h"
#include "gc/collector/compactor.h"
#include "gc/space/dlmalloc_space.h"
#include "gc/space/space.h"
#include "gc/space/space-inl.h"
#include "mirror/object.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/art_field-inl.h"
#include "mirror/art_method-inl.h"
#include "mirror/array-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class.h"

namespace art {
namespace gc {
namespace collector {



inline void SpaceCompactor::allocateSpaceObject(const mirror::Object* obj,
                                         size_t obj_size) {
  size_t actual_space = 0;
  mirror::Object* result = compact_space_->publicAllocWithoutGrowthLocked(obj_size,
                                                            &actual_space);
//  const byte* src = reinterpret_cast<const byte*>(obj);
//  byte* dst = reinterpret_cast<byte*>(result);
//  memcpy(dst, src, obj_size);
  forwarded_objects_.Put(obj, result);


//
//
}


template <class referenceKlass>
const referenceKlass* SpaceCompactor::MapValueToServer(
    const referenceKlass* original_obj, bool* ismoved) const {
  const byte* _raw_address = reinterpret_cast<const byte*>(original_obj);
  if((_raw_address < byte_end_) &&
          (_raw_address >= byte_start_)) {
    *ismoved = true;
    FwdedOBJs::const_iterator found = forwarded_objects_.find(original_obj);
    return found->second;
  }

  return original_obj;


}

SpaceCompactor::SpaceCompactor(Heap* vmHeap) : local_heap_(vmHeap),
    objects_cnt_(0),
    compacted_cnt_(0),
    original_space_(local_heap_->GetAllocSpace()->AsDlMallocSpace()),
    compact_space_(NULL),
    immune_begin_ (NULL),
    immune_end_ (NULL),
    byte_start_(NULL),
    byte_end_(NULL) {




}

class CompactVisitor {
 public:
  SpaceCompactor* compactor_;
  CompactVisitor(SpaceCompactor* space_compactor) :
    compactor_(space_compactor) {

  }

  void operator()(const mirror::Object* o) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if(o != NULL) {
      size_t n = o->SizeOf();
      compactor_->allocateSpaceObject(o, n);
      compactor_->compacted_cnt_++;
    }
  }
  DISALLOW_COPY_AND_ASSIGN(CompactVisitor);
};



class CompactFixableVisitor {
 public:
  SpaceCompactor* compactor_;
  CompactFixableVisitor(SpaceCompactor* space_compactor) :
    compactor_(space_compactor) {

  }

  void operator()(const mirror::Object* o) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if(o != NULL) {
      size_t n = o->SizeOf();
      compactor_->allocateSpaceObject(o, n);
      compactor_->compacted_cnt_++;
    }
  }
  DISALLOW_COPY_AND_ASSIGN(CompactFixableVisitor);
};

void SpaceCompactor::FillNewObjects(void) {
//  for(const auto& ref : forwarded_objects_) {
//    const byte* src = reinterpret_cast<const byte*>(ref.first);
//    byte* dst = reinterpret_cast<byte*>(ref.second);
////    size_t n = ref.first->SizeOf();
////    memcpy(dst, src, n);
//  }
}


typedef struct FragmentationInfo_S {
  uint64_t sum_;
  uint64_t max_;
}FragmentationInfo;

static void MSpaceSumFragChunkCallback(void* start, void* end, size_t used_bytes, void* arg) {
  size_t chunk_size = reinterpret_cast<uint8_t*>(end) - reinterpret_cast<uint8_t*>(start);
  if (used_bytes < chunk_size) {
    uint64_t chunk_free_bytes = chunk_size - used_bytes;
    if (chunk_free_bytes >= 8) {
      FragmentationInfo* _info = reinterpret_cast<FragmentationInfo*>(arg);
      _info->max_ = std::max(_info->max_, chunk_free_bytes);
      _info->sum_ = _info->sum_ +  chunk_free_bytes;
//      uint64_t& max_contiguous_allocation = *reinterpret_cast<uint64_t*>(arg);
//      max_contiguous_allocation = max_contiguous_allocation + chunk_free_bytes;
    }
  }
}


void SpaceCompactor::FixupFields(const mirror::Object* orig,
                                 mirror::Object* copy,
                              uint32_t ref_offsets,
                              bool is_static) {
  if (ref_offsets != CLASS_WALK_SUPER) {
    // Found a reference offset bitmap.  Fixup the specified offsets.
    while (ref_offsets != 0) {
      size_t right_shift = CLZ(ref_offsets);
      MemberOffset byte_offset = CLASS_OFFSET_FROM_CLZ(right_shift);
      const mirror::Object* ref = orig->GetFieldObject<const mirror::Object*>(byte_offset, false);
      // Use SetFieldPtr to avoid card marking since we are writing to the image.
      bool isMapped = false;
      const mirror::Object* _new_ref = MapValueToServer<mirror::Object>(ref, &isMapped);
      copy->publicSetFieldPtr(byte_offset, _new_ref, false);
      ref_offsets &= ~(CLASS_HIGH_BIT >> right_shift);
    }
  } else {
    // There is no reference offset bitmap.  In the non-static case,
    // walk up the class inheritance hierarchy and find reference
    // offsets the hard way. In the static case, just consider this
    // class.
    for (const mirror::Class *klass = is_static ? orig->AsClass() : orig->GetClass();
         klass != NULL;
         klass = is_static ? NULL : klass->GetSuperClass()) {
      size_t num_reference_fields = (is_static
                                     ? klass->NumReferenceStaticFields()
                                     : klass->NumReferenceInstanceFields());
      for (size_t i = 0; i < num_reference_fields; ++i) {
        mirror::ArtField* field = (is_static
                           ? klass->GetStaticField(i)
                           : klass->GetInstanceField(i));
        MemberOffset field_offset = field->GetOffset();
        const mirror::Object* ref =
            orig->GetFieldObject<const mirror::Object*>(field_offset, false);
        bool isMapped = false;
        const mirror::Object* _new_ref = MapValueToServer<mirror::Object>(ref, &isMapped);
        // Use SetFieldPtr to avoid card marking since we are writing to the image.
        copy->publicSetFieldPtr(field_offset, _new_ref, false);
      }
    }
  }
  if (!is_static && orig->IsReferenceInstance()) {
    // Fix-up referent, that isn't marked as an object field, for References.
    mirror::ArtField* field = orig->GetClass()->FindInstanceField("referent", "Ljava/lang/Object;");
    MemberOffset field_offset = field->GetOffset();
    const mirror::Object* ref = orig->GetFieldObject<const mirror::Object*>(field_offset, false);
    // Use SetFieldPtr to avoid card marking since we are writing to the image.
    bool isMapped = false;
    const mirror::Object* _new_ref = MapValueToServer<mirror::Object>(ref, &isMapped);
    copy->publicSetFieldPtr(field_offset, _new_ref, false);
  }
}

void SpaceCompactor::FixupInstanceFields(const mirror::Object* orig, mirror::Object* copy) {
  DCHECK(orig != NULL);
  DCHECK(copy != NULL);
  mirror::Class* klass = orig->GetClass();
  DCHECK(klass != NULL);
  FixupFields(orig,
              copy,
              klass->GetReferenceInstanceOffsets(),
              false);
}

void SpaceCompactor::FixupStaticFields(const mirror::Class* orig, mirror::Class* copy) {
  DCHECK(orig != NULL);
  DCHECK(copy != NULL);
  FixupFields(orig,
              copy,
              orig->GetReferenceStaticOffsets(),
              true);
}

void SpaceCompactor::FixupObjectArray(const mirror::ObjectArray<mirror::Object>* orig, mirror::ObjectArray<mirror::Object>* copy) {
  for (int32_t i = 0; i < orig->GetLength(); ++i) {
    const mirror::Object* element = orig->Get(i);
    bool isMapped = false;
    const mirror::Object* _new_ref = MapValueToServer<mirror::Object>(element, &isMapped);
    copy->SetPtrWithoutChecks(i, const_cast<mirror::Object*>(_new_ref));
  }
}


void SpaceCompactor::FixupClass(const mirror::Class* orig, mirror::Class* copy) {
  FixupInstanceFields(orig, copy);
  FixupStaticFields(orig, copy);
}


void SpaceCompactor::FixupObject(const mirror::Object* orig,
                                 mirror::Object* copy) {
  const mirror::Object* _origin_class =
      reinterpret_cast<mirror::Object*>(orig->GetClass());
  bool ismapped = false;
  const mirror::Object* _new_class =
      MapValueToServer<mirror::Object>(_origin_class, &ismapped);
  if(ismapped) {
    copy->SetClass(down_cast<mirror::Class*>(const_cast<mirror::Object*>(_new_class)));
  }

  if (orig->IsClass()) {
    FixupClass(orig->AsClass(), down_cast<mirror::Class*>(copy));
  } else if (orig->IsObjectArray()) {
    FixupObjectArray(orig->AsObjectArray<mirror::Object>(), down_cast<mirror::ObjectArray<mirror::Object>*>(copy));
  } else if (orig->IsArtMethod()) {
    FixupMethod(orig->AsArtMethod(), down_cast<mirror::ArtMethod*>(copy));
  } else {
    FixupInstanceFields(orig, copy);
  }

}

void SpaceCompactor::FixupMethod(const mirror::ArtMethod* orig, mirror::ArtMethod* copy) {
  FixupInstanceFields(orig, copy);

}
void SpaceCompactor::startCompaction(void) {
  LOG(ERROR) << "Inside SpaceCompactor::startCompaction()";

  Runtime* runtime = Runtime::Current();
  if(true) {
  // We only want reachable instances, so do a GC. This also ensures that the alloc stack
  // is empty, so the live bitmap is the only place we need to look.
  Thread* self = Thread::Current();
  {
    //ReaderMutexLock mu(self, *Locks::mutator_lock_);
    //self->TransitionFromRunnableToSuspended(kNative);
    local_heap_->CollectGarbage(true);
    //self->TransitionFromSuspendedToRunnable();
  }
  LOG(ERROR) << "Done Non-Concurrent Collection: " << self->GetTid();
  FragmentationInfo _frag_info;
  _frag_info.max_ = 0;
  _frag_info.sum_ = 0;

  for (const auto& space : local_heap_->GetContinuousSpaces()) {
    if (space->IsZygoteSpace()) {
      space->AsDlMallocSpace()->Walk(MSpaceSumFragChunkCallback, &_frag_info);
      LOG(ERROR) << "XXXX Fragmentation zygote space = " << _frag_info.max_ << ", " << _frag_info.sum_;
      break;
    }
  }

  _frag_info.max_ = 0;
  _frag_info.sum_ = 0;
  original_space_->Walk(MSpaceSumFragChunkCallback, &_frag_info);

  LOG(ERROR) << "XXXX Fragmentation before Compaction = " << _frag_info.max_ << ", " << _frag_info.sum_;
  ThreadList* thread_list = runtime->GetThreadList();
  thread_list->SuspendAll();
  {

    ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
    local_heap_->DisableObjectValidation();

    size_t capacity = original_space_->Capacity();
    original_space_->SetEnd(reinterpret_cast<byte*>(RoundUp(reinterpret_cast<uintptr_t>(original_space_->End()), kPageSize)));
    original_space_->Trim();
    original_space_->GetMemMap()->UnMapAtEnd(original_space_->End());

    _frag_info.max_ = 0;
    _frag_info.sum_ = 0;
        original_space_->Walk(MSpaceSumFragChunkCallback, &_frag_info);

    LOG(ERROR) << "XXXX Fragmentation after trimming = " << _frag_info.max_ << ", " << _frag_info.sum_;

    LOG(ERROR) << "original space size is: being:"
        << reinterpret_cast<void*>(original_space_->Begin()) << ", end: " << reinterpret_cast<void*>(original_space_->End()) <<
        ", capacity is:" << capacity << ", size is : " << original_space_->Size();

    original_space_->GetLiveBitmap()->SetHeapLimit(reinterpret_cast<uintptr_t>(original_space_->End()));
    original_space_->GetMarkBitmap()->SetHeapLimit(reinterpret_cast<uintptr_t>(original_space_->End()));
      compact_space_ = gc::space::DlMallocSpace::Create("compacted_space",
                                                          original_space_->Size(),
                                                          capacity, capacity,
                                                          original_space_->End(), false);
      LOG(ERROR) << "new dlmalloc space size is: being:"
          << reinterpret_cast<void*>(compact_space_->Begin()) << ", end: " << reinterpret_cast<void*>(compact_space_->End()) <<
          ", capacity is:" << compact_space_->Capacity();
      byte_start_ = original_space_->Begin();
      byte_end_ = original_space_->End();
      immune_begin_ = reinterpret_cast<mirror::Object*>(original_space_->Begin());
      immune_end_ = reinterpret_cast<mirror::Object*>(original_space_->End());
      accounting::SPACE_BITMAP* _live_bitmap =
          original_space_->GetLiveBitmap();
      CompactVisitor compact_visitor(this);
      _live_bitmap->VisitMarkedRange(_live_bitmap->HeapBegin(),
                                     _live_bitmap->HeapLimit(),
                                     compact_visitor);


      LOG(ERROR) << "Start Check Fragmentation";
      _frag_info.max_ = 0;
      _frag_info.sum_ = 0;
      mspace_inspect_all(compact_space_->GetMspace(),
                         MSpaceSumFragChunkCallback,  &_frag_info);
      MSpaceSumFragChunkCallback(NULL, NULL, 0, &_frag_info);
     // MSpaceSumFragChunkCallback(NULL, NULL, 0, &postFragmentation);  // Indicate end of a space.
//      compact_space_->Walk(MSpaceSumFragChunkCallback, &postFragmentation);

      LOG(ERROR) << "Fragmentation post Compaction = "  << _frag_info.max_ << ", " << _frag_info.sum_;

      LOG(ERROR) << "Start copying and fixing Objects";

      int _count = 0;
      for(const auto& ref : forwarded_objects_) {
        const byte* src = reinterpret_cast<const byte*>(ref.first);
        byte* dst = reinterpret_cast<byte*>(ref.second);
        size_t n = ref.first->SizeOf();

        memcpy(dst, src, n);

        LOG(ERROR) << "++ fixing: "<<  _count << ", "
            << reinterpret_cast<const void*>(ref.first)
            << ", to " << reinterpret_cast<const void*>(ref.second)
            << ", size: " << n;
        _count++;
        FixupObject(ref.first,ref.second);
//        if (ref.first->IsClass()) {
//
//        }
//        const mirror::Object* _origin_class = reinterpret_cast<mirror::Object*>(ref.first->GetClass());
//        bool ismapped = false;
//        const mirror::Object* new_addr =
//            MapValueToServer<mirror::Object>(_origin_class, &ismapped);
//        if(ismapped) {
////          const byte* _raw_address = reinterpret_cast<const byte*>(_origin_class);
//          (const_cast<mirror::Object*>(ref.second))->SetClass(down_cast<mirror::Class*>(const_cast<mirror::Object*>(new_addr)));
//          LOG(ERROR) << "correcting class of object.." <<
//              reinterpret_cast<const void*>(ref.first) <<
//              ", old_class=" << reinterpret_cast<const void*>(_origin_class) <<
//              ", new_class=" << reinterpret_cast<const void*>(new_addr);
//        }
//
//
//        LOG(ERROR) << "fwd.. Obj:" << ref.first << ", fwded:" << ref.second <<
//            ", size=" << n;


      }
      LOG(ERROR) << "Done copying and fixing Objects";
    //here we should copy and fix all broken references;

  }
  thread_list->ResumeAll();
  }
}

void SpaceCompactor::FinalizeCompaction(void) {
  LOG(ERROR) << "compacted count = " << compacted_cnt_;
  for(const auto& ref : forwarded_objects_) {
//    const byte* src = reinterpret_cast<const byte*>(ref.first);
//    byte* dst = reinterpret_cast<byte*>(ref.second);
//    memcpy(dst, src, 8);
    LOG(ERROR) << "fwd.. Obj:" << ref.first << ", fwded:" << ref.second;
  }
}





}//collector
}//gc
}//art


