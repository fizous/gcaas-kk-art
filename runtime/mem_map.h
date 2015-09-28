/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef ART_RUNTIME_MEM_MAP_H_
#define ART_RUNTIME_MEM_MAP_H_

#include <string>

#include <stddef.h>
#include <sys/mman.h>  // For the PROT_* and MAP_* constants.
#include <sys/types.h>

#include "globals.h"
#include "utils.h"

#if (true || ART_GC_SERVICE)
#define MEM_MAP MemBaseMap
#else
#define MEM_MAP MemMap
#endif



namespace art {

typedef struct AShmemMap_S {
  char name_[64];
  byte* /*const*/ begin_;  // Start of data.
  size_t size_;  // Length of data.
  void* /*const*/ base_begin_;  // Page-aligned base address.
  /*const*/ size_t base_size_;  // Length of mapping.
  int prot_;  // Protection of the map.
  /* int to hold the flags by which the memory was mapped */
  int flags_;
  /*integer to hold the file descriptor of the memory mapped region */
  int fd_;
  AShmemMap_S(const std::string& name, byte* begin,
      size_t size, void* base_begin, size_t base_size, int prot,
      int flags, int fd) :
        begin_(begin), size_(size),
        base_begin_(base_begin), base_size_(base_size),
        prot_(prot), flags_(flags), fd_(fd) {
    memcpy(name_, name.c_str(), name.size());
    name_[name.size()] = '\0';
  }
} __attribute__((aligned(8))) AShmemMap;


typedef struct CardBaseTableFields_S {
  AShmemMap mem_map_;
  // Value used to compute card table addresses from object addresses, see
  //GetBiasedBegin
  byte* const biased_begin_;
  // Card table doesn't begin at the beginning of the mem_map_, instead it is
  //displaced by offset
  // to allow the byte value of biased_begin_ to equal GC_CARD_DIRTY
  const size_t offset_;
} __attribute__((aligned(8))) CardBaseTableFields;

template <class T>
struct AtomicStackData {
  char name_[64];

  // Memory mapping of the atomic stack.
  AShmemMap memory_;

  // Back index (index after the last element pushed).
  volatile int back_index_;

  // Front index, used for implementing PopFront.
  volatile int front_index_;

  // Base of the atomic stack.
  T* begin_;

  // Maximum number of elements.
  size_t capacity_;

  // Whether or not the stack is sorted, only updated in debug mode to avoid performance overhead.
  bool debug_is_sorted_;

  bool is_shared_;
}__attribute__((aligned(8)));
typedef AtomicStackData<mirror::Object*> StructuredObjectStackData;


#if (true || ART_GC_SERVICE)

// Used to keep track of mmap segments.
class MemBaseMap {
 public:
  virtual int GetProtect() const = 0;

  virtual byte* Begin() const = 0;

  virtual void* BaseBegin()  const = 0;

  virtual size_t Size()  const = 0;

  virtual void SetSize(size_t) = 0;

  virtual void SetProt(int) = 0;

  virtual size_t BaseSize()  const = 0;

  byte* End() const {
    return Begin() + Size();
  }

  virtual bool HasAddress(const void* addr) const {
    return Begin() <= addr && addr < End();
  }


  bool Protect(int);

  // Trim by unmapping pages at the end of the map.
  void UnMapAtEnd(byte* new_end);

  static AShmemMap* CreateAShmemMap(AShmemMap* ashmem_mem_map,
      const char* ashmem_name, byte* addr, size_t byte_count, int prot,
      bool shareMem = false);

  static void AShmemFillData(AShmemMap* addr, const std::string& name, byte* begin,
      size_t size, void* base_begin, size_t base_size, int prot, int flags, int fd) {
    AShmemMap _data = {"g\0", begin, size, base_begin, base_size, prot, flags, fd};
    memcpy(_data.name_, name.c_str(), name.size());
    _data.name_[name.size()] = '\0';
    memcpy(addr, &_data, SERVICE_ALLOC_ALIGN_BYTE(AShmemMap));
  }

  // Releases the memory mapping
  virtual ~MemBaseMap();

  MemBaseMap(){}

  // Map part of a file, taking care of non-page aligned offsets.  The
  // "start" offset is absolute, not relative. This version allows
  // requesting a specific address for the base of the mapping.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemBaseMap* MapFileAtAddress(
      byte* addr, size_t byte_count, int prot, int flags, int fd, off_t start,
      bool reuse);

  // Request an anonymous region of length 'byte_count' and a requested base address.
  // Use NULL as the requested base address if you don't care.
  //
  // The word "anonymous" in this context means "not backed by a file". The supplied
  // 'ashmem_name' will be used -- on systems that support it -- to give the mapping
  // a name.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemBaseMap* MapAnonymous(const char* ashmem_name, byte* addr,
      size_t byte_count, int prot, bool shareMem = false);

  static MemBaseMap* CreateStructedMemMap(const char* ashmem_name, byte* addr,
      size_t byte_count, int prot, bool shareMem = false,
      AShmemMap* ashmem_mem_map = NULL);

  // Map part of a file, taking care of non-page aligned offsets.  The
  // "start" offset is absolute, not relative.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemBaseMap* MapFile(size_t byte_count, int prot, int flags, int fd, off_t start) {
    return MapFileAtAddress(NULL, byte_count, prot, flags, fd, start, false);
  }

  static bool AshmemHasAddress(AShmemMap* record, const void* addr)  {
    return AshmemBegin(record) <= addr && addr < AshmemEnd(record);
  }

  static byte* AshmemBegin(AShmemMap* addr)  {
    return /*const_cast<const byte*>*/(addr->begin_);
  }



  static size_t AshmemSize(AShmemMap* addr)  {
    return addr->size_;
  }

  static byte* AshmemEnd(AShmemMap* addr)  {
    return AshmemBegin(addr) + AshmemSize(addr);
  }

  // Trim by unmapping pages at the end of the map.
  static void AshmemUnMapAtEnd(AShmemMap* addr, byte* new_end) {
    size_t unmap_size = AshmemEnd(addr) - new_end;
    munmap(new_end, unmap_size);
    addr->size_ -= unmap_size;
  }

  static AShmemMap* ShareAShmemMap(AShmemMap* source_ashmem_mem_map,
      AShmemMap* dest_ashmem_mem_map = NULL);

  static void AshmemDestructData(AShmemMap* addr, bool release_pointer);

  static bool AshmemProtect(AShmemMap* addr, int prot) {
    if (addr->base_begin_ == NULL && addr->base_size_ == 0) {
      addr->prot_ = prot;
      return true;
    }

    if (mprotect(addr->base_begin_, addr->base_size_, prot) == 0) {
      addr->prot_ = prot;
      return true;
    }

    PLOG(ERROR) << "mprotect(" << reinterpret_cast<void*>(addr->base_begin_) <<
        ", " << addr->base_size_ << ", "
                << prot << ") failed";
    return false;
  }
};//class MemBaseMap


// Used to keep track of mmap segments.
class MemMap : public MemBaseMap {
 public:
  int GetProtect() const {
    return prot_;
  }

  byte* Begin() const {
    return begin_;
  }


  void* BaseBegin() const {
    return base_begin_;
  }

  size_t Size() const {
    return size_;
  }

  size_t BaseSize() const {
    return base_size_;
  }

  void SetSize(size_t new_size);

  void SetProt(int newProt) {
    prot_ = newProt;
  }

  ~MemMap();
  MemMap(const std::string& name, byte* begin, size_t size, void* base_begin,
        size_t base_size, int prot);
 private:


  std::string name_;
  byte* const begin_;  // Start of data.
  size_t size_;  // Length of data.

  void* const base_begin_;  // Page-aligned base address.
  const size_t base_size_;  // Length of mapping.
  int prot_;  // Protection of the map.
};//class MemMap


class StructuredMemMap: public MemBaseMap {
 public:
  ~StructuredMemMap();
  StructuredMemMap(AShmemMap* ashmem) : ashmem_(ashmem) {};
  StructuredMemMap(AShmemMap* ashmem, const std::string& name, byte* begin,
      size_t size, void* base_begin, size_t base_size, int prot);

//  StructuredMemMap(AShmemMap* ashmem);

  static StructuredMemMap* CreateStructuredMemMap(AShmemMap* ashmem_mem_map,
      const char* ashmem_name, byte* addr, size_t byte_count, int prot,
      bool shareMem = false);

  bool Protect(int prot) {
    return MEM_MAP::AshmemProtect(ashmem_, prot);
  }

  int GetProtect() const {
    return ashmem_->prot_;
  }

  byte* Begin() const {
    return ashmem_->begin_;
  }

  size_t Size() const {
    return ashmem_->size_;
  }

  void* BaseBegin() const {
    return ashmem_->base_begin_;
  }

  size_t BaseSize() const {
    return ashmem_->base_size_;
  }

  byte* End() const {
    return Begin() + Size();
  }

  void UnMapAtEnd(byte* new_end) {
    MEM_MAP::AshmemUnMapAtEnd(ashmem_, new_end);
  }

  void SetProt(int newProt) {
    ashmem_->prot_ = newProt;
  }

  void SetSize(size_t new_size);
  AShmemMap* ashmem_;
};




#else







// Used to keep track of mmap segments.
class MemMap {
 public:
  // Request an anonymous region of length 'byte_count' and a requested base address.
  // Use NULL as the requested base address if you don't care.
  //
  // The word "anonymous" in this context means "not backed by a file". The supplied
  // 'ashmem_name' will be used -- on systems that support it -- to give the mapping
  // a name.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemMap* MapAnonymous(const char* ashmem_name, byte* addr,
      size_t byte_count, int prot, bool shareMem = false);

  // Map part of a file, taking care of non-page aligned offsets.  The
  // "start" offset is absolute, not relative.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemMap* MapFile(size_t byte_count, int prot, int flags, int fd, off_t start) {
    return MapFileAtAddress(NULL, byte_count, prot, flags, fd, start, false);
  }

  // Map part of a file, taking care of non-page aligned offsets.  The
  // "start" offset is absolute, not relative. This version allows
  // requesting a specific address for the base of the mapping.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemMap* MapFileAtAddress(
      byte* addr, size_t byte_count, int prot, int flags, int fd, off_t start,
      bool reuse);

  // Releases the memory mapping
  ~MemMap();

  bool Protect(int prot);

  int GetProtect() const {
    return prot_;
  }

  byte* Begin() const {
    return begin_;
  }


  void* BaseBegin() const {
    return base_begin_;
  }

  size_t Size() const {
    return size_;
  }

  size_t BaseSize() const {
    return base_size_;
  }

  byte* End() const {
    return Begin() + Size();
  }

  bool HasAddress(const void* addr) const {
    return Begin() <= addr && addr < End();
  }

  // Trim by unmapping pages at the end of the map.
  void UnMapAtEnd(byte* new_end);




 //protected:
  MemMap(const std::string& name, byte* begin, size_t size, void* base_begin,
      size_t base_size, int prot);
 private:
  std::string name_;
  byte* const begin_;  // Start of data.
  size_t size_;  // Length of data.

  void* const base_begin_;  // Page-aligned base address.
  const size_t base_size_;  // Length of mapping.
  int prot_;  // Protection of the map.
};


#endif




}  // namespace art

#endif  // ART_RUNTIME_MEM_MAP_H_
