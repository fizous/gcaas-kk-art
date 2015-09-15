/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_
#define ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_

#include "base/logging.h"
#include "gc_allocator.h"
#include "locks.h"
#include "space_bitmap.h"

namespace art {
namespace gc {

class Heap;

namespace accounting {

#if ART_GC_SERVICE


typedef struct GCSrvceSharedHeapBitmap_S {
  // pointer to the heap
  const Heap* const heap_;
  // The index of the bitmap array
  volatile int index_;
  //bitmaps array
  BaseBitmap* bitmaps_[8];
}  __attribute__((aligned(8))) GCSrvceSharedHeapBitmap;



class BaseHeapBitmap {
 public:
  virtual bool Test(const mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    BaseBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      return bitmap->Test(obj);
    } else {
      LOG(FATAL) << "Test: object does not belong to any bitmap";
    }
    return false;
  }


  virtual void Clear(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    BaseBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Clear(obj);
    } else {
      LOG(FATAL) << "The object could not be cleared as it does not belong to "
          "any bitmap";
    }
  }


  virtual void Set(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    BaseBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Set(obj);
    } else {
      LOG(FATAL) << "The object could not be set the object as it does not belong to "
          "any bitmap";
    }
  }


  virtual BaseBitmap* GetContinuousSpaceBitmap(const mirror::Object* obj) = 0;

  virtual void Walk(BaseBitmap::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) = 0;


  template <typename Visitor>
  void Visit(const Visitor& visitor)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);


  // Find and replace a bitmap pointer, this is used by for the bitmap swapping in the GC.
  virtual void ReplaceBitmap(BaseBitmap* old_bitmap, BaseBitmap* new_bitmap)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) = 0;

  // Find and replace a object set pointer, this is used by for the bitmap swapping in the GC.
  virtual void ReplaceObjectSet(SpaceSetMap*, SpaceSetMap*)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_){}

  explicit BaseHeapBitmap(Heap*) {}
  virtual ~BaseHeapBitmap(){}

  virtual void AddContinuousSpaceBitmap(BaseBitmap*) = 0;
  virtual void AddDiscontinuousObjectSet(SpaceSetMap*){}


  virtual int GetContinuousSize() = 0;
  virtual BaseBitmap* GetContBitmapFromIndex(int index) = 0;
  virtual int GetDiscContinuousSize() {
    return 0;
  }
  virtual SpaceSetMap* GetDiscContBitmapFromIndex(int index){
    return NULL;
  }
};//class BaseHeapBitmap


class SharedHeapBitmap : public BaseHeapBitmap {
 public:
  SharedHeapBitmap(Heap* heap, GCSrvceSharedHeapBitmap* header_addr = NULL);
  ~SharedHeapBitmap(){}
  void AddContinuousSpaceBitmap(BaseBitmap* bitmap);
  void Walk(BaseBitmap::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  void ReplaceBitmap(BaseBitmap* old_bitmap, BaseBitmap* new_bitmap)
        EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  BaseBitmap* GetContinuousSpaceBitmap(const mirror::Object* obj) {
    BaseBitmap* _bitmap = NULL;
    for(int i = 0; i < header_->index_; i ++) {
      _bitmap = header_->bitmaps_[i];
      if (_bitmap->HasAddress(obj)) {
        return _bitmap;
      }
    }
    return NULL;
  }

  int GetContinuousSize() {
    return header_->index_;
  }

  BaseBitmap* GetContBitmapFromIndex(int index) {
    return header_->bitmaps_[index];
  }

 private:
  GCSrvceSharedHeapBitmap* header_;
};





////////////////////////////////////////////////////////////////
class HeapBitmap : public BaseHeapBitmap {
 public:
  typedef std::vector<BaseBitmap*, GCAllocator<BaseBitmap*> > SpaceBitmapVector;
  typedef std::vector<SpaceSetMap*, GCAllocator<SpaceSetMap*> > SpaceSetMapVector;


  bool Test(const mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    BaseBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      return bitmap->Test(obj);
    } else {
      return GetDiscontinuousSpaceObjectSet(obj) != NULL;
    }
  }

  void Clear(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    BaseBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Clear(obj);
    } else {
      SpaceSetMap* set = GetDiscontinuousSpaceObjectSet(obj);
      DCHECK(set != NULL);
      set->Clear(obj);
    }
  }

  void Set(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    BaseBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Set(obj);
    } else {
      SpaceSetMap* set = GetDiscontinuousSpaceObjectSet(obj);
      DCHECK(set != NULL);
      set->Set(obj);
    }
  }

  BaseBitmap* GetContinuousSpaceBitmap(const mirror::Object* obj) {
    for (const auto& bitmap : continuous_space_bitmaps_) {
      if (bitmap->HasAddress(obj)) {
        return bitmap;
      }
    }
    return NULL;
  }

  SpaceSetMap* GetDiscontinuousSpaceObjectSet(const mirror::Object* obj) {
    for (const auto& space_set : discontinuous_space_sets_) {
      if (space_set->Test(obj)) {
        return space_set;
      }
    }
    return NULL;
  }

  void Walk(BaseBitmap::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Find and replace a bitmap pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceBitmap(BaseBitmap* old_bitmap, BaseBitmap* new_bitmap)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Find and replace a object set pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  explicit HeapBitmap(Heap* heap) : BaseHeapBitmap(heap), heap_(heap) {}

  void AddContinuousSpaceBitmap(BaseBitmap* bitmap);
  void AddDiscontinuousObjectSet(SpaceSetMap* set);


  int GetContinuousSize() {
    return continuous_space_bitmaps_.size();
  }


  BaseBitmap* GetContBitmapFromIndex(int index) {
    return continuous_space_bitmaps_[index];
  }


  int GetDiscContinuousSize() {
    return discontinuous_space_sets_.size();
  }

  SpaceSetMap* GetDiscContBitmapFromIndex(int index) {
    return discontinuous_space_sets_[index];
  }
 private:
  const Heap* const heap_;

  // Bitmaps covering continuous spaces.
  SpaceBitmapVector continuous_space_bitmaps_;

  // Sets covering discontinuous spaces.
  SpaceSetMapVector discontinuous_space_sets_;

  //friend class art::gc::Heap;
};



#else

class HeapBitmap {
 public:
  typedef std::vector<SpaceBitmap*, GCAllocator<SpaceBitmap*> > SpaceBitmapVector;
  typedef std::vector<SpaceSetMap*, GCAllocator<SpaceSetMap*> > SpaceSetMapVector;

  bool Test(const mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    SpaceBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      return bitmap->Test(obj);
    } else {
      return GetDiscontinuousSpaceObjectSet(obj) != NULL;
    }
  }

  void Clear(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    SpaceBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Clear(obj);
    } else {
      SpaceSetMap* set = GetDiscontinuousSpaceObjectSet(obj);
      DCHECK(set != NULL);
      set->Clear(obj);
    }
  }

  void Set(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    SpaceBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Set(obj);
    } else {
      SpaceSetMap* set = GetDiscontinuousSpaceObjectSet(obj);
      DCHECK(set != NULL);
      set->Set(obj);
    }
  }

  SpaceBitmap* GetContinuousSpaceBitmap(const mirror::Object* obj) {
    for (const auto& bitmap : continuous_space_bitmaps_) {
      if (bitmap->HasAddress(obj)) {
        return bitmap;
      }
    }
    return NULL;
  }

  SpaceSetMap* GetDiscontinuousSpaceObjectSet(const mirror::Object* obj) {
    for (const auto& space_set : discontinuous_space_sets_) {
      if (space_set->Test(obj)) {
        return space_set;
      }
    }
    return NULL;
  }

  void Walk(SpaceBitmap::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  template <typename Visitor>
  void Visit(const Visitor& visitor)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Find and replace a bitmap pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceBitmap(SpaceBitmap* old_bitmap, SpaceBitmap* new_bitmap)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Find and replace a object set pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  explicit HeapBitmap(Heap* heap) : heap_(heap) {}

 private:
  const Heap* const heap_;

  void AddContinuousSpaceBitmap(SpaceBitmap* bitmap);
  void AddDiscontinuousObjectSet(SpaceSetMap* set);

  // Bitmaps covering continuous spaces.
  SpaceBitmapVector continuous_space_bitmaps_;

  // Sets covering discontinuous spaces.
  SpaceSetMapVector discontinuous_space_sets_;

  friend class art::gc::Heap;
};


#endif

}  // namespace accounting
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_
