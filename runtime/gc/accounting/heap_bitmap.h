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



#define GC_SERVICE_SHARABLE_HEAP_BITMAP    true




namespace art {
namespace gc {

class Heap;

namespace accounting {

#if true || ART_GC_SERVICE


class SharedHeapBitmap;

class BaseHeapBitmap {
 public:

  static BaseHeapBitmap* CreateHeapBitmap(Heap* heap, bool sharable = false);
  static BaseHeapBitmap* ReShareHeapBitmap(SharedHeapBitmap* originalBMap,
      GCSrvceSharedHeapBitmap* header_addr);

  virtual bool Test(const mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);


  virtual void Clear(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);


  virtual void Set(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);


  virtual SPACE_BITMAP* GetContinuousSpaceBitmap(const mirror::Object* obj) = 0;

  virtual void Walk(SPACE_BITMAP::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) = 0;


  template <typename Visitor>
  void Visit(const Visitor& visitor)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);


  // Find and replace a bitmap pointer, this is used by for the bitmap swapping in the GC.
  virtual void ReplaceBitmap(SPACE_BITMAP* old_bitmap, SPACE_BITMAP* new_bitmap)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) = 0;

  // Find and replace a object set pointer, this is used by for the bitmap swapping in the GC.
  virtual void ReplaceObjectSet(SpaceSetMap*, SpaceSetMap*)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_){}

  explicit BaseHeapBitmap(Heap*) {}
  virtual ~BaseHeapBitmap(){}

  virtual void AddContinuousSpaceBitmap(SPACE_BITMAP*) = 0;
  virtual void AddDiscontinuousObjectSet(SpaceSetMap*){}


  virtual int GetContinuousSize() = 0;
  virtual SPACE_BITMAP* GetContBitmapFromIndex(int index) = 0;
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
  void AddContinuousSpaceBitmap(SPACE_BITMAP* bitmap);
  void Walk(SPACE_BITMAP::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  void ReplaceBitmap(SPACE_BITMAP* old_bitmap, SPACE_BITMAP* new_bitmap)
        EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  SPACE_BITMAP* GetContinuousSpaceBitmap(const mirror::Object* obj) {
    SPACE_BITMAP* _bitmap = NULL;
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

  SPACE_BITMAP* GetContBitmapFromIndex(int index) {
    return header_->bitmaps_[index];
  }

 private:
  GCSrvceSharedHeapBitmap* header_;
};





////////////////////////////////////////////////////////////////
class HeapBitmap : public BaseHeapBitmap {
 public:
  typedef std::vector<SPACE_BITMAP*, GCAllocator<SPACE_BITMAP*> > SpaceBitmapVector;
  typedef std::vector<SpaceSetMap*, GCAllocator<SpaceSetMap*> > SpaceSetMapVector;


  bool Test(const mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void Clear(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void Set(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  SPACE_BITMAP* GetContinuousSpaceBitmap(const mirror::Object* obj) {
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

  void Walk(SPACE_BITMAP::Callback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Find and replace a bitmap pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceBitmap(SPACE_BITMAP* old_bitmap, SPACE_BITMAP* new_bitmap)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Find and replace a object set pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  explicit HeapBitmap(Heap* heap) : BaseHeapBitmap(heap), heap_(heap) {}

  void AddContinuousSpaceBitmap(SPACE_BITMAP* bitmap);
  void AddDiscontinuousObjectSet(SpaceSetMap* set);


  int GetContinuousSize() {
    return continuous_space_bitmaps_.size();
  }


  SPACE_BITMAP* GetContBitmapFromIndex(int index) {
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
