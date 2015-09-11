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
#include "base/mutex.h"

namespace art {

namespace gcservice {

typedef enum {
  GCSERVICE_STATUS_NONE = 0,
  GCSERVICE_STATUS_WAITINGSERVER = 1,
  GCSERVICE_STATUS_SERVER_INITIALIZED = 2,
  GCSERVICE_STATUS_STARTING = 4,
  GCSERVICE_STATUS_RUNNING  = 8,
  GCSERVICE_STATUS_SHUTTING_DOWN  = 16,
  GCSERVICE_STATUS_STOPPED  = 32
} GC_SERVICE_STATUS;
}//namespace gcservice





typedef struct MemMapMetaData_S {
  byte* const begin_;  // Start of data.
  size_t size_;  // Length of data.
  void* const base_begin_;  // Page-aligned base address.
  const size_t base_size_;  // Length of mapping.
  int prot_;  // Protection of the map.
  int fd_;
} __attribute__((aligned(8))) MemMapMetaData;




typedef struct SharedMemMapMeta_S {
  MemMapMetaData owner_meta_;
  /* mapped data into outer process */
  byte* mapped_begin_;
  byte* mapped_base_begin_;
  int fd_;
} __attribute__((aligned(8))) SharedMemMapMeta;





//class BaseMapMem {
// public:
//  const std::string name_;
//
//  virtual byte* Begin() const = 0;
//
//  virtual byte* BaseBegin() const = 0;
//
//  virtual size_t Size() const = 0;
//
//  virtual void SetSize(size_t) = 0;
//
//  virtual size_t BaseSize() const = 0;
//
//  virtual byte* End() const = 0;
//
//  virtual bool HasAddress(const void* addr) const = 0;
//
//  virtual int GetProtect() const = 0;
//
//  virtual int GetFD() {
//    return -1;
//  }
//
//  virtual void SetFD(int){}
//  virtual bool Protect(int){return false;}
//  virtual bool ProtectModifiedMMAP(int) {return false;}
//
//  BaseMapMem(const std::string& name);
//
//
//  // Trim by unmapping pages at the end of the map.
//  virtual void UnMapAtEnd(byte*);
//
//  virtual ~BaseMapMem(){}
//
//  virtual void initMemMap(byte* begin, size_t size,
//      void* base_begin, size_t base_size, int prot) = 0;
//
//
//
//};





//class SharedMemMap;

// Used to keep track of mmap segments.
class MemMap { //: public BaseMapMem {
 public:
  // Request an anonymous region of length 'byte_count' and a requested base address.
  // Use NULL as the requested base address if you don't care.
  //
  // The word "anonymous" in this context means "not backed by a file". The supplied
  // 'ashmem_name' will be used -- on systems that support it -- to give the mapping
  // a name.
  //
  // On success, returns returns a MemMap instance.  On failure, returns a NULL;
  static MemMap* MapAnonymous(const char* ashmem_name, byte* addr, size_t byte_count, int prot);
  static MemMap* MapSharedMemoryAnonymous(const char* name, byte* addr,
  		size_t byte_count, int prot);

  static MemMap* MapSharedMemoryWithMeta(const char* name, byte* addr,
      size_t byte_count, int prot, SharedMemMapMeta* metadata,
      int flagsParam = MAP_SHARED);

  static MemMap* MapSharedMemWithMetaAtAddr(const char* name, byte* addr,
      size_t byte_count, int prot, SharedMemMapMeta* metadata,
      int flagsParam = MAP_SHARED) {
    return MapSharedMemoryWithMeta(name, addr, byte_count, prot, metadata,
        flagsParam | MAP_FIXED);
  }
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
      byte* addr, size_t byte_count, int prot, int flags, int fd, off_t start, bool reuse);

  static MemMap* MapSharedProcessFile(byte* addr, size_t byte_count, int prot,
      int fd);

  static void FillSharedMemMapMetaData(SharedMemMapMeta* addr, byte* begin,
      size_t size, void* base_begin, size_t base_size, int prot, int fd) {
    MemMapMetaData _data = {begin, size, base_begin, base_size, prot, fd};
    memcpy(&addr->owner_meta_, &_data, sizeof(MemMapMetaData));
  }

  static void FillMemMapMetaData(MemMapMetaData* addr, byte* begin, size_t size,
      void* base_begin, size_t base_size, int prot, int fd) {
    MemMapMetaData _data = {begin, size, base_begin, base_size, prot, fd};
    memcpy(addr, &_data, sizeof(MemMapMetaData));
  }

  // Releases the memory mapping
  ~MemMap();

  byte* Begin() const {
    return meta_data_addr_->begin_;
  }

  byte* BaseBegin() const {
    return (byte*) meta_data_addr_->base_begin_;
  }

  size_t Size() const {
    return meta_data_addr_->size_;
  }

  void SetSize(size_t new_size) {
    meta_data_addr_->size_ = new_size;
  }

  size_t BaseSize() const {
    return meta_data_addr_->base_size_;
  }

  byte* End() const {
    return Begin() + Size();
  }

  bool HasAddress(const void* addr) const {
    return Begin() <= addr && addr < End();
  }

  int GetProtect() {
    return meta_data_addr_->prot_;
  }

  void SetProtect(int newProtect) {
    meta_data_addr_->prot_ = newProtect;
  }

  int GetFD() {
    return meta_data_addr_->fd_;
  }

  void SetFD(int fd){
    meta_data_addr_->fd_ = fd;
  }

  const char* getName() {
    return name_.c_str();
  }

//  void initMemMap(byte* begin, size_t size, void* base_begin, size_t base_size,
//      int prot);

  bool Protect(int prot);
  bool ProtectModifiedMMAP(int prot);
  void UnMapAtEnd(byte*);
 private:
  MemMap(const std::string& name, byte* begin, size_t size, void* base_begin,
      size_t base_size, int prot, int fd = -1, MemMapMetaData* addr = NULL);

  void InitMetadata(byte* begin, size_t size, void* base_begin,
      size_t base_size, int prot, int fd);

  const std::string name_;
  MemMapMetaData* meta_data_addr_;
//  friend class SharedMemMap;
//
//  byte* const begin_;  // Start of data.
//  size_t size_;  // Length of data.
//
//  void* const base_begin_;  // Page-aligned base address.
//  const size_t base_size_;  // Length of mapping.
//  int prot_;  // Protection of the map.

};


typedef struct CardTableMemberMetaData_S {
  // Backing storage for bitmap.
  MemMap* mem_map_;

  // Value used to compute card table addresses from object addresses, see GetBiasedBegin
  byte* const biased_begin_;

  // Card table doesn't begin at the beginning of the mem_map_, instead it is displaced by offset
  // to allow the byte value of biased_begin_ to equal GC_CARD_DIRTY
  const size_t offset_;

  const byte* heap_begin_;
} __attribute__((aligned(8)))  CardTableMemberMetaData;



typedef struct BitMapMemberMetaData_S {
  // Backing storage for bitmap.
  MemMap* mem_map_;

  // This bitmap itself, word sized for efficiency in scanning.
  word* const bitmap_begin_;

  // Size of this bitmap.
  size_t bitmap_size_;

  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  const uintptr_t heap_begin_;
} __attribute__((aligned(8)))  BitMapMemberMetaData;


typedef struct ContinuousSpaceMemberMetaData_S {
  // The beginning of the storage for fast access.
  byte* const begin_;

  // Current end of the space.
  byte* end_;

  int gc_retention_policy_;

  SharedMemMapMeta mem_meta_;
} __attribute__((aligned(8)))  ContinuousSpaceMemberMetaData;


typedef struct SharedSpaceBitmapMeta_S {
  /* memory pointer to the bitmap data*/
  SharedMemMapMeta data_;
  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  BitMapMemberMetaData bitmap_fields_;
} __attribute__((aligned(8)))  SharedSpaceBitmapMeta;

typedef struct SharedSpaceMeta_S {
  ContinuousSpaceMemberMetaData space_data_meta_;
  /* data related to space bitmap */
  SharedSpaceBitmapMeta bitmap_meta_[2];
  byte* biased_begin_;
  byte* begin_;
  size_t offset_;
} __attribute__((aligned(8))) SharedSpaceMeta;

typedef struct SharedCardTableMeta_S {
  SharedMemMapMeta mem_meta_;
  CardTableMemberMetaData card_table_fields_;
} __attribute__((aligned(8)))  SharedCardTableMeta;


typedef struct SharedHeapMetada_S {
  SynchronizedLockHead lock_header_;
  /* data related to continuous space */
  SharedCardTableMeta card_table_meta_;

  SharedSpaceMeta alloc_space_meta_;
  /* used to synchronize on conc requests*/
  SynchronizedLockHead gc_conc_requests;

  gcservice::GC_SERVICE_STATUS vm_status_;

  pid_t pid_;
} __attribute__((aligned(8))) SharedHeapMetada;

///////////////////////////
/////////////////////////////Shared Memory Map

//// Used to keep track of mmap segments.
//class SharedMemMap : public BaseMapMem {
//  SharedMemMap(const std::string& name, byte* begin, size_t size,
//      void* base_begin, size_t base_size, int prot, int fd);
//
//  SharedMemMap(const std::string& name, byte* begin, size_t size,
//      void* base_begin, size_t base_size, int prot, int fd,
//      SharedMemMapMeta*);
//
//  void initSharedMemMap(byte* begin, size_t size,
//      void* base_begin, size_t base_size, int prot, int fd,
//      SharedMemMapMeta* metaMem);
//
// public:
//
//  void initMemMap(byte* begin, size_t size,
//      void* base_begin, size_t base_size, int prot);
//  SharedMemMapMeta* metadata_;
//
//  int GetProtect() const {
//    return metadata_->prot_;
//  }
//
//  byte* Begin() const {
//    return metadata_->owner_begin_;
//  }
//
//  byte* BaseBegin() const {
//    return (byte*) metadata_->owner_base_begin_;
//  }
//
//  size_t Size() const {
//    return metadata_->size_;
//  }
//
//  void SetSize(size_t new_size) {
//    metadata_->size_ = new_size;
//  }
//
//  size_t BaseSize() const {
//    return metadata_->base_size_;
//  }
//
//  byte* End() const {
//    return Begin() + Size();
//  }
//
//  int GetFD() {
//    return metadata_->fd_;
//  }
//
//  void SetFD(int fd){
//    metadata_->fd_ = fd;
//  }
//
//  bool HasAddress(const void* addr) const {
//    return Begin() <= addr && addr < End();
//  }
//
//  friend class MemMap;
//  MemMap* GetLocalMemMap();
//  // Releases the memory mapping
//  ~SharedMemMap();
//};//SharedMemMap

}  // namespace art

#endif  // ART_RUNTIME_MEM_MAP_H_
