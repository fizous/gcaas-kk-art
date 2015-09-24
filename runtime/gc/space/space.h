/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_SPACE_SPACE_H_
#define ART_RUNTIME_GC_SPACE_SPACE_H_

#include <string>

#include "UniquePtr.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "gc/accounting/space_bitmap.h"

#include "globals.h"
#include "image.h"
#include "mem_map.h"


#if (true || ART_GC_SERVICE)
#define DL_MALLOC_SPACE  DlMallocSpace
#define DLMALLOC_SPACE_T DlMallocSpace
#define CONTINUOUS_SPACE_T ContinuousSpace
#else
#define DL_MALLOC_SPACE  DlMallocSpace
#define CONTINUOUS_SPACE_T ContinuousSpace
#endif

namespace art {
namespace mirror {
  class Object;
}  // namespace mirror

namespace gc {

namespace accounting {
  class BaseBitmap;
  class SpaceBitmap;
  class SpaceBitmap;
}  // namespace accounting

class Heap;

namespace space {

class DL_MALLOC_SPACE;
class ImageSpace;
class LargeObjectSpace;

static constexpr bool kDebugSpaces = kIsDebugBuild;

// See Space::GetGcRetentionPolicy.
enum GcRetentionPolicy {
  // Objects are retained forever with this policy for a space.
  kGcRetentionPolicyNeverCollect,
  // Every GC cycle will attempt to collect objects in this space.
  kGcRetentionPolicyAlwaysCollect,
  // Objects will be considered for collection only in "full" GC cycles, ie faster partial
  // collections won't scan these areas such as the Zygote.
  kGcRetentionPolicyFullCollect,
};
std::ostream& operator<<(std::ostream& os, const GcRetentionPolicy& policy);

enum SpaceType {
  kSpaceTypeImageSpace,
  kSpaceTypeAllocSpace,
  kSpaceTypeZygoteSpace,
  kSpaceTypeLargeObjectSpace,
};
std::ostream& operator<<(std::ostream& os, const SpaceType& space_type);

#if (true || ART_GC_SERVICE)

// A space contains memory allocated for managed objects.
class InterfaceSpace {
 public:
  // Dump space. Also key method for C++ vtables.
  virtual void Dump(std::ostream& os) const = 0;


  virtual GcRetentionPolicy GetGcRetentionPolicy() const = 0;

  // Does the space support allocation?
  virtual bool CanAllocateInto() const {
    return true;
  }


  // Is the given object contained within this space?
  virtual bool Contains(const mirror::Object* obj) const = 0;


  // The kind of space this: image, alloc, zygote, large object.
  virtual SpaceType GetType() const = 0;


  // Is this an image space, ie one backed by a memory mapped image file.
  virtual bool IsImageSpace() const {
    return GetType() == kSpaceTypeImageSpace;
  }

  virtual ImageSpace* AsImageSpace() = 0;

  // Is this a dlmalloc backed allocation space?
  virtual bool IsDlMallocSpace() const {
    SpaceType type = GetType();
    return type == kSpaceTypeAllocSpace || type == kSpaceTypeZygoteSpace;
  }


  virtual DL_MALLOC_SPACE* AsDlMallocSpace() = 0;

  // Is this the space allocated into by the Zygote and no-longer in use?
  virtual bool IsZygoteSpace() const {
    return GetType() == kSpaceTypeZygoteSpace;
  }

  // Does this space hold large objects and implement the large object space abstraction?
  virtual bool IsLargeObjectSpace() const {
    return GetType() == kSpaceTypeLargeObjectSpace;
  }
  virtual LargeObjectSpace* AsLargeObjectSpace() = 0;


  // Return the storage space required by obj.
  virtual size_t GCPGetAllocationSize(const mirror::Object*){return 0;}


 protected:
  InterfaceSpace() {}
  virtual ~InterfaceSpace() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InterfaceSpace);
};


// A space contains memory allocated for managed objects.
class Space : public InterfaceSpace {
 public:
  // Dump space. Also key method for C++ vtables.
  virtual void Dump(std::ostream& os) const;

  // Name of the space. May vary, for example before/after the Zygote fork.
  const char* GetName() const {
    return name_.c_str();
  }

  // The policy of when objects are collected associated with this space.
  virtual GcRetentionPolicy GetGcRetentionPolicy() const {
    return gc_retention_policy_;
  }


  // Is the given object contained within this space?
  //virtual bool Contains(const mirror::Object* obj) const = 0;

//  // get the allocated memory for that object?
//  virtual size_t GetObjectSize(const mirror::Object* obj) const = 0;

  // The kind of space this: image, alloc, zygote, large object.
  virtual SpaceType GetType() const = 0;

  ImageSpace* AsImageSpace();
  DL_MALLOC_SPACE* AsDlMallocSpace();
  LargeObjectSpace* AsLargeObjectSpace();

  virtual ~Space() {}


  void SetGcRetentionPolicy(GcRetentionPolicy gc_retention_policy) {
    gc_retention_policy_ = gc_retention_policy;
  }

 protected:
  Space(const std::string& name, GcRetentionPolicy gc_retention_policy);



  // Name of the space that may vary due to the Zygote fork.
  std::string name_;

 private:
  // When should objects within this space be reclaimed? Not constant as we vary it in the case
  // of Zygote forking.
  GcRetentionPolicy gc_retention_policy_;

//  friend class art::gc::Heap;

  DISALLOW_COPY_AND_ASSIGN(Space);
};
std::ostream& operator<<(std::ostream& os, const Space& space);


// AllocSpace interface.
class AllocSpace {
 public:
  // Number of bytes currently allocated.
  virtual uint64_t GetBytesAllocated() const = 0;
  // Number of objects currently allocated.
  virtual uint64_t GetObjectsAllocated() const = 0;
  // Number of bytes allocated since the space was created.
  virtual uint64_t GetTotalBytesAllocated() const = 0;
  // Number of objects allocated since the space was created.
  virtual uint64_t GetTotalObjectsAllocated() const = 0;

  virtual void UpdateBytesAllocated(int) = 0;

  virtual void UpdateObjectsAllocated(int) = 0;

  virtual void UpdateTotalBytesAllocated(int) = 0;

  virtual void UpdateTotalObjectsAllocated(int) = 0;
  // Allocate num_bytes without allowing growth. If the allocation
  // succeeds, the output parameter bytes_allocated will be set to the
  // actually allocated bytes which is >= num_bytes.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) = 0;

  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj) = 0;

  // Returns how many bytes were freed.
  virtual size_t Free(Thread* self, mirror::Object* ptr) = 0;

  // Returns how many bytes were freed.
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) = 0;

 protected:
  AllocSpace() {}
  virtual ~AllocSpace() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AllocSpace);
};

// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
class IContinuousSpace {
 public:
  // Address at which the space begins

  virtual byte* Begin() const = 0;
  virtual byte* End() const = 0;
  virtual void SetEnd(byte*)  = 0;
  // Current size of space
  virtual size_t Size() const {
    return End() - Begin();
  }

  virtual accounting::SPACE_BITMAP* GetLiveBitmap() const = 0;
  virtual accounting::SPACE_BITMAP* GetMarkBitmap() const = 0;

  // Is object within this space? We check to see if the pointer is beyond the end first as
  // continuous spaces are iterated over from low to high.
  bool HasAddress(const mirror::Object* obj) const {
    const byte* byte_ptr = reinterpret_cast<const byte*>(obj);
    return byte_ptr < End() && byte_ptr >= Begin();
  }

  virtual ~IContinuousSpace() {}
  IContinuousSpace(){}
};



// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
class ContinuousSpace : public IContinuousSpace, public Space {
 public:

  byte* Begin() const {
    return begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  byte* End() const {
    return end_;
  }

  // Current size of space
  size_t Size() const {
    return End() - Begin();
  }

  void SetEnd(byte* new_end)  {
    end_ = new_end;
  }

  bool Contains(const mirror::Object* obj) const {
    return HasAddress(obj);
  }

  virtual ~ContinuousSpace() {}

 protected:
  ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
                  byte* begin, byte* end) :
      Space(name, gc_retention_policy), begin_(begin), end_(end) {
  }

  // The beginning of the storage for fast access.
  byte* const begin_;

  // Current end of the space.
  byte* end_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ContinuousSpace);
};


// A space where objects may be allocated higgledy-piggledy throughout virtual memory. Currently
// the card table can't cover these objects and so the write barrier shouldn't be triggered. This
// is suitable for use for large primitive arrays.
class DiscontinuousSpace : public Space {
 public:
  accounting::SpaceSetMap* GetLiveObjects() const {
    return live_objects_.get();
  }

  accounting::SpaceSetMap* GetMarkObjects() const {
    return mark_objects_.get();
  }

  virtual ~DiscontinuousSpace() {}

 protected:
  DiscontinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy);

  UniquePtr<accounting::SpaceSetMap> live_objects_;
  UniquePtr<accounting::SpaceSetMap> mark_objects_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscontinuousSpace);
};


class IMemMapSpace /*: public IContinuousSpace*/ {
 public:
  // Maximum which the mapped space can grow to.
  virtual size_t Capacity() const  = 0;

  // Size of the space without a limit on its growth. By default this is just the Capacity, but
  // for the allocation space we support starting with a small heap and then extending it.
  virtual size_t NonGrowthLimitCapacity() const = 0;

  virtual MEM_MAP* GetMemMap() = 0;

  virtual ~IMemMapSpace(){}
  IMemMapSpace(){}
 private:
  DISALLOW_COPY_AND_ASSIGN(IMemMapSpace);
};




class MemMapSpace : public IMemMapSpace, public ContinuousSpace {
 public:
  // Maximum which the mapped space can grow to.
  virtual size_t Capacity() const {
    return mem_map_->Size();
  }

  // Size of the space without a limit on its growth. By default this is just the Capacity, but
  // for the allocation space we support starting with a small heap and then extending it.
  virtual size_t NonGrowthLimitCapacity() const {
    return Capacity();
  }

 protected:
  MemMapSpace(const std::string& name, MEM_MAP* mem_map, size_t initial_size,
              GcRetentionPolicy gc_retention_policy)
      : ContinuousSpace(name, gc_retention_policy,
                        mem_map->Begin(), mem_map->Begin() + initial_size),
        mem_map_(mem_map) {
  }

  MEM_MAP* GetMemMap() {
    return mem_map_.get();
  }

  const MEM_MAP* GetMemMap() const {
    return mem_map_.get();
  }

 private:
  // Underlying storage of the space
  UniquePtr<MEM_MAP> mem_map_;

  DISALLOW_COPY_AND_ASSIGN(MemMapSpace);
};

class AbstractDLmallocSpace: public IMemMapSpace, public IContinuousSpace,
    public  InterfaceSpace {
public:
  // Set the maximum number of bytes that the heap is allowed to obtain from the system via
  // MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
  // allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
  virtual void SetFootprintLimit(size_t limit) = 0;

  virtual void* GetMspace() const = 0;

  virtual void SetGrowthLimit(size_t growth_limit) = 0;

protected:
  virtual ~AbstractDLmallocSpace() {}
  AbstractDLmallocSpace(){}
  //virtual ~AbstractDLmallocSpace() {}
};

class SharableSpace {

};

#else


// A space contains memory allocated for managed objects.
class Space {
 public:
  // Dump space. Also key method for C++ vtables.
  virtual void Dump(std::ostream& os) const;

  // Name of the space. May vary, for example before/after the Zygote fork.
  const char* GetName() const {
    return name_.c_str();
  }

#if (true || ART_GC_SERVICE)
  // The policy of when objects are collected associated with this space.
  GcRetentionPolicy GetGcRetentionPolicy() const {
    return gc_retention_policy_;
  }
#else
  // The policy of when objects are collected associated with this space.
  virtual GcRetentionPolicy GetGcRetentionPolicy() const {
    return gc_retention_policy_;
  }
#endif

  // Does the space support allocation?
  virtual bool CanAllocateInto() const {
    return true;
  }

  // Is the given object contained within this space?
  virtual bool Contains(const mirror::Object* obj) const = 0;

//  // get the allocated memory for that object?
//  virtual size_t GetObjectSize(const mirror::Object* obj) const = 0;

  // The kind of space this: image, alloc, zygote, large object.
  virtual SpaceType GetType() const = 0;

  // Is this an image space, ie one backed by a memory mapped image file.
  bool IsImageSpace() const {
    return GetType() == kSpaceTypeImageSpace;
  }
  ImageSpace* AsImageSpace();

  // Is this a dlmalloc backed allocation space?
  bool IsDlMallocSpace() const {
    SpaceType type = GetType();
    return type == kSpaceTypeAllocSpace || type == kSpaceTypeZygoteSpace;
  }
  DL_MALLOC_SPACE* AsDlMallocSpace();

  // Is this the space allocated into by the Zygote and no-longer in use?
  bool IsZygoteSpace() const {
    return GetType() == kSpaceTypeZygoteSpace;
  }

  // Does this space hold large objects and implement the large object space abstraction?
  bool IsLargeObjectSpace() const {
    return GetType() == kSpaceTypeLargeObjectSpace;
  }
  LargeObjectSpace* AsLargeObjectSpace();

  virtual ~Space() {}


  // Return the storage space required by obj.
  virtual size_t GCPGetAllocationSize(const mirror::Object*){return 0;}
 protected:
  Space(const std::string& name, GcRetentionPolicy gc_retention_policy);

  void SetGcRetentionPolicy(GcRetentionPolicy gc_retention_policy) {
    gc_retention_policy_ = gc_retention_policy;
  }

  // Name of the space that may vary due to the Zygote fork.
  std::string name_;

 private:
  // When should objects within this space be reclaimed? Not constant as we vary it in the case
  // of Zygote forking.
  GcRetentionPolicy gc_retention_policy_;

  friend class art::gc::Heap;

  DISALLOW_COPY_AND_ASSIGN(Space);
};
std::ostream& operator<<(std::ostream& os, const Space& space);

// AllocSpace interface.
class AllocSpace {
 public:
  // Number of bytes currently allocated.
  virtual uint64_t GetBytesAllocated() const = 0;
  // Number of objects currently allocated.
  virtual uint64_t GetObjectsAllocated() const = 0;
  // Number of bytes allocated since the space was created.
  virtual uint64_t GetTotalBytesAllocated() const = 0;
  // Number of objects allocated since the space was created.
  virtual uint64_t GetTotalObjectsAllocated() const = 0;

  virtual void UpdateBytesAllocated(int) = 0;

  virtual void UpdateObjectsAllocated(int) = 0;

  virtual void UpdateTotalBytesAllocated(int) = 0;

  virtual void UpdateTotalObjectsAllocated(int) = 0;
  // Allocate num_bytes without allowing growth. If the allocation
  // succeeds, the output parameter bytes_allocated will be set to the
  // actually allocated bytes which is >= num_bytes.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) = 0;

  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj) = 0;

  // Returns how many bytes were freed.
  virtual size_t Free(Thread* self, mirror::Object* ptr) = 0;

  // Returns how many bytes were freed.
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) = 0;

 protected:
  AllocSpace() {}
  virtual ~AllocSpace() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AllocSpace);
};

// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
class ContinuousSpace : public Space {
 public:
  // Address at which the space begins

#if (true || ART_GC_SERVICE)
  virtual byte* Begin() const {
    return begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  virtual byte* End() const {
    return end_;
  }


  virtual void SetEnd(byte* new_end)  {
    end_ = new_end;
  }

  // Current size of space
  virtual size_t Size() const {
    return End() - Begin();
  }
#else
  byte* Begin() const {
    return begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  byte* End() const {
    return end_;
  }

  // Current size of space
  size_t Size() const {
    return End() - Begin();
  }

  void SetEnd(byte* new_end)  {
    end_ = new_end;
  }
#endif

  virtual accounting::SPACE_BITMAP* GetLiveBitmap() const = 0;
  virtual accounting::SPACE_BITMAP* GetMarkBitmap() const = 0;

  // Is object within this space? We check to see if the pointer is beyond the end first as
  // continuous spaces are iterated over from low to high.
  bool HasAddress(const mirror::Object* obj) const {
    const byte* byte_ptr = reinterpret_cast<const byte*>(obj);
    return byte_ptr < End() && byte_ptr >= Begin();
  }

  bool Contains(const mirror::Object* obj) const {
    return HasAddress(obj);
  }

  virtual ~ContinuousSpace() {}

 protected:
  ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
                  byte* begin, byte* end) :
      Space(name, gc_retention_policy), begin_(begin), end_(end) {
  }


  // The beginning of the storage for fast access.
  byte* const begin_;

  // Current end of the space.
  byte* end_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ContinuousSpace);
};

// A space where objects may be allocated higgledy-piggledy throughout virtual memory. Currently
// the card table can't cover these objects and so the write barrier shouldn't be triggered. This
// is suitable for use for large primitive arrays.
class DiscontinuousSpace : public Space {
 public:
  accounting::SpaceSetMap* GetLiveObjects() const {
    return live_objects_.get();
  }

  accounting::SpaceSetMap* GetMarkObjects() const {
    return mark_objects_.get();
  }

  virtual ~DiscontinuousSpace() {}

 protected:
  DiscontinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy);

  UniquePtr<accounting::SpaceSetMap> live_objects_;
  UniquePtr<accounting::SpaceSetMap> mark_objects_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscontinuousSpace);
};

class MemMapSpace : public ContinuousSpace {
 public:
  // Maximum which the mapped space can grow to.
  virtual size_t Capacity() const {
    return mem_map_->Size();
  }

  // Size of the space without a limit on its growth. By default this is just the Capacity, but
  // for the allocation space we support starting with a small heap and then extending it.
  virtual size_t NonGrowthLimitCapacity() const {
    return Capacity();
  }

 protected:
  MemMapSpace(const std::string& name, MEM_MAP* mem_map, size_t initial_size,
              GcRetentionPolicy gc_retention_policy)
      : ContinuousSpace(name, gc_retention_policy,
                        mem_map->Begin(), mem_map->Begin() + initial_size),
        mem_map_(mem_map) {
  }

  MEM_MAP* GetMemMap() {
    return mem_map_.get();
  }

  const MEM_MAP* GetMemMap() const {
    return mem_map_.get();
  }

 private:
  // Underlying storage of the space
  UniquePtr<MEM_MAP> mem_map_;

  DISALLOW_COPY_AND_ASSIGN(MemMapSpace);
};


#endif

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_SPACE_H_
