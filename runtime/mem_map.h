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

namespace art {

namespace gcservice {
typedef struct SharedMemMapMeta_S {
  byte* owner_begin_;
  byte* owner_base_begin_;
  size_t size_;
  size_t base_size_;
  volatile int fd_;
  volatile int prot_;
} SharedMemMapMeta;
}//gcservice

class MemMapBase {
 public:
  const std::string name_;

  virtual byte* Begin() const = 0;

  virtual byte* BaseBegin() const = 0;

  virtual size_t Size() const = 0;

  virtual void SetSize(size_t) = 0;

  virtual size_t BaseSize() const = 0;

  virtual byte* End() const = 0;

  virtual bool HasAddress(const void* addr) const = 0;

  virtual int GetProtect() const = 0;

  virtual int GetFD() {
    return -1;
  }

  virtual void SetFD(int){}
  virtual bool Protect(int){return false;}
  virtual bool ProtectModifiedMMAP(int) {return false;}

  MemMapBase(const std::string& name);


  // Trim by unmapping pages at the end of the map.
  virtual void UnMapAtEnd(byte*);

  virtual ~MemMapBase();

  const char* getName() {
    return name_.c_str();
  }
};

class SharedMemMap;

// Used to keep track of mmap segments.
class MemMap : public MemMapBase {
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
  static SharedMemMap* MapSharedMemoryAnonymous(const char* name, byte* addr,
  		size_t byte_count, int prot);

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

  // Releases the memory mapping
  ~MemMap();

  int GetProtect() const {
    return prot_;
  }

  byte* Begin() const {
    return begin_;
  }

  byte* BaseBegin() const {
    return (byte*) base_begin_;
  }

  size_t Size() const {
    return size_;
  }

  void SetSize(size_t new_size) {
    size_ = new_size;
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

  int GetProtect() {
    return prot_;
  }



  bool Protect(int prot);
  bool ProtectModifiedMMAP(int prot);

 private:
  MemMap(const std::string& name, byte* begin, size_t size, void* base_begin, size_t base_size,
         int prot);
  friend class SharedMemMap;

  byte* const begin_;  // Start of data.
  size_t size_;  // Length of data.

  void* const base_begin_;  // Page-aligned base address.
  const size_t base_size_;  // Length of mapping.
  int prot_;  // Protection of the map.

};

///////////////////////////
/////////////////////////////Shared Memory Map

// Used to keep track of mmap segments.
class SharedMemMap : public MemMapBase {
  SharedMemMap(const std::string& name, byte* begin, size_t size,
      void* base_begin, size_t base_size, int prot, int fd);

  SharedMemMap(const std::string& name, byte* begin, size_t size,
      void* base_begin, size_t base_size, int prot, int fd,
      gcservice::SharedMemMapMeta*);

  void initSharedMemMap(byte* begin, size_t size,
      void* base_begin, size_t base_size, int prot, int fd,
      gcservice::SharedMemMapMeta* metaMem);

 public:
  static SharedMemMap* MapSharedMemoryWithMeta(const char* name, byte* addr,
      size_t byte_count, int prot, gcservice::SharedMemMapMeta* metadata);

  gcservice::SharedMemMapMeta* metadata_;

  int GetProtect() const {
    return metadata_->prot_;
  }

  byte* Begin() const {
    return metadata_->owner_begin_;
  }

  byte* BaseBegin() const {
    return (byte*) metadata_->owner_base_begin_;
  }

  size_t Size() const {
    return metadata_->size_;
  }

  void SetSize(size_t new_size) {
    metadata_->size_ = new_size;
  }

  size_t BaseSize() const {
    return metadata_->base_size_;
  }

  byte* End() const {
    return Begin() + Size();
  }

  int GetFD() {
    return metadata_->fd_;
  }

  void SetFD(int fd){
    metadata_->fd_ = fd;
  }

  bool HasAddress(const void* addr) const {
    return Begin() <= addr && addr < End();
  }

  friend class MemMap;
  MemMap* GetLocalMemMap();
  // Releases the memory mapping

};//SharedMemMap

}  // namespace art

#endif  // ART_RUNTIME_MEM_MAP_H_
