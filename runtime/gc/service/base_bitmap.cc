/*
 * base_bitmap.cc
 *
 *  Created on: Sep 14, 2015
 *      Author: hussein
 */




#include "base/logging.h"
#include "dex_file-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "mirror/art_field-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "object_utils.h"
#include "gc/accounting/space_bitmap-inl.h"
#include "UniquePtr.h"
#include "utils.h"

namespace art {
namespace gc {
namespace accounting {

void BaseBitmap::Clear() {
  if (Begin() != NULL) {
    // This returns the memory to the system.  Successive page faults will return zeroed memory.
    int result = madvise(Begin(), Size(), MADV_DONTNEED);
    if (result == -1) {
      PLOG(FATAL) << "madvise failed";
    }
  }
}


void BaseBitmap::CopyFrom(BaseBitmap* source_bitmap) {
  DCHECK_EQ(Size(), source_bitmap->Size());
  std::copy(source_bitmap->Begin(), source_bitmap->Begin() + source_bitmap->Size() / kWordSize, Begin());
}



// Walk through the bitmaps in increasing address order, and find the
// object pointers that correspond to garbage objects.  Call
// <callback> zero or more times with lists of these object pointers.
//
// The callback is not permitted to increase the max of either bitmap.
void BaseBitmap::SweepWalk(const BaseBitmap& live_bitmap,
                           const BaseBitmap& mark_bitmap,
                           uintptr_t sweep_begin, uintptr_t sweep_end,
                           BaseBitmap::SweepCallback* callback, void* arg) {
  CHECK(live_bitmap.Begin() != NULL);
  CHECK(mark_bitmap.Begin() != NULL);
  CHECK_EQ(live_bitmap.HeapBegin(), mark_bitmap.HeapBegin());
  CHECK_EQ(live_bitmap.Size(), mark_bitmap.Size());
  CHECK(callback != NULL);
  CHECK_LE(sweep_begin, sweep_end);
  CHECK_GE(sweep_begin, live_bitmap.HeapBegin());

  if (sweep_end <= sweep_begin) {
    return;
  }

  // TODO: rewrite the callbacks to accept a std::vector<mirror::Object*> rather than a mirror::Object**?
  const size_t buffer_size = kWordSize * kBitsPerWord;
  mirror::Object* pointer_buf[buffer_size];
  mirror::Object** pb = &pointer_buf[0];
  size_t start = OffsetToIndex(sweep_begin - live_bitmap.HeapBegin());
  size_t end = OffsetToIndex(sweep_end - live_bitmap.HeapBegin() - 1);
  CHECK_LT(end, live_bitmap.Size() / kWordSize);
  word* live = live_bitmap.Begin();
  word* mark = mark_bitmap.Begin();
  for (size_t i = start; i <= end; i++) {
    word garbage = live[i] & ~mark[i];
    if (UNLIKELY(garbage != 0)) {
      uintptr_t ptr_base = IndexToOffset(i) + live_bitmap.HeapBegin();
      do {
        const size_t shift = CLZ(garbage);
        garbage ^= static_cast<size_t>(kWordHighBitMask) >> shift;
        *pb++ = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
      } while (garbage != 0);
      // Make sure that there are always enough slots available for an
      // entire word of one bits.
      if (pb >= &pointer_buf[buffer_size - kBitsPerWord]) {
        (*callback)(pb - &pointer_buf[0], &pointer_buf[0], arg);
        pb = &pointer_buf[0];
      }
    }
  }
  if (pb > &pointer_buf[0]) {
    (*callback)(pb - &pointer_buf[0], &pointer_buf[0], arg);
  }
}

static void WalkFieldsInOrder(BaseBitmap* visited,
            BaseBitmap::Callback* callback, mirror::Object* obj, void* arg);

// Walk instance fields of the given Class. Separate function to allow recursion on the super
// class.
static void WalkInstanceFields(BaseBitmap* visited,
              BaseBitmap::Callback* callback, mirror::Object* obj,
              mirror::Class* klass, void* arg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  // Visit fields of parent classes first.
  mirror::Class* super = klass->GetSuperClass();
  if (super != NULL) {
    WalkInstanceFields(visited, callback, obj, super, arg);
  }
  // Walk instance fields
  mirror::ObjectArray<mirror::ArtField>* fields = klass->GetIFields();
  if (fields != NULL) {
    for (int32_t i = 0; i < fields->GetLength(); i++) {
      mirror::ArtField* field = fields->Get(i);
      FieldHelper fh(field);
      if (!fh.IsPrimitiveType()) {
        mirror::Object* value = field->GetObj(obj);
        if (value != NULL) {
          WalkFieldsInOrder(visited, callback, value,  arg);
        }
      }
    }
  }
}

// For an unvisited object, visit it then all its children found via fields.
static void WalkFieldsInOrder(BaseBitmap* visited,
    BaseBitmap::Callback* callback, mirror::Object* obj,
                      void* arg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  if (visited->Test(obj)) {
    return;
  }
  // visit the object itself
  (*callback)(obj, arg);
  visited->Set(obj);
  // Walk instance fields of all objects
  mirror::Class* klass = obj->GetClass();
  WalkInstanceFields(visited, callback, obj, klass, arg);
  // Walk static fields of a Class
  if (obj->IsClass()) {
    mirror::ObjectArray<mirror::ArtField>* fields = klass->GetSFields();
    if (fields != NULL) {
      for (int32_t i = 0; i < fields->GetLength(); i++) {
        mirror::ArtField* field = fields->Get(i);
        FieldHelper fh(field);
        if (!fh.IsPrimitiveType()) {
          mirror::Object* value = field->GetObj(NULL);
          if (value != NULL) {
            WalkFieldsInOrder(visited, callback, value, arg);
          }
        }
      }
    }
  } else if (obj->IsObjectArray()) {
    // Walk elements of an object array
    mirror::ObjectArray<mirror::Object>* obj_array = obj->AsObjectArray<mirror::Object>();
    int32_t length = obj_array->GetLength();
    for (int32_t i = 0; i < length; i++) {
      mirror::Object* value = obj_array->Get(i);
      if (value != NULL) {
        WalkFieldsInOrder(visited, callback, value, arg);
      }
    }
  }
}

// Visits set bits with an in order traversal.  The callback is not permitted to change the bitmap
// bits or max during the traversal.
void BaseBitmap::InOrderWalk(BaseBitmap::Callback* callback, void* arg) {
  UniquePtr<BaseBitmap> visited(SpaceBitmap::Create("bitmap for in-order walk",
                                       reinterpret_cast<byte*>(HeapBegin()),
                                       IndexToOffset(Size() / kWordSize)));
  CHECK(Begin() != NULL);
  CHECK(callback != NULL);
  uintptr_t end = Size() / kWordSize;
  for (uintptr_t i = 0; i < end; ++i) {
    word w = Begin()[i];
    if (UNLIKELY(w != 0)) {
      uintptr_t ptr_base = IndexToOffset(i) + HeapBegin();
      while (w != 0) {
        const size_t shift = CLZ(w);
        mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
        WalkFieldsInOrder(visited.get(), callback, obj, arg);
        w ^= static_cast<size_t>(kWordHighBitMask) >> shift;
      }
    }
  }
}


// Visits set bits in address order.  The callback is not permitted to
// change the bitmap bits or max during the traversal.
void BaseBitmap::Walk(BaseBitmap::Callback* callback, void* arg) {
  CHECK(Begin() != NULL);
  CHECK(callback != NULL);

  uintptr_t end = OffsetToIndex(HeapLimit() - HeapBegin() - 1);
  word* bitmap_begin = Begin();
  for (uintptr_t i = 0; i <= end; ++i) {
    word w = bitmap_begin[i];
    if (w != 0) {
      uintptr_t ptr_base = IndexToOffset(i) + HeapBegin();
      do {
        const size_t shift = CLZ(w);
        mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
        (*callback)(obj, arg);
        w ^= static_cast<size_t>(kWordHighBitMask) >> shift;
      } while (w != 0);
    }
  }
}


}  // namespace accounting
}  // namespace gc
}  // namespace art
