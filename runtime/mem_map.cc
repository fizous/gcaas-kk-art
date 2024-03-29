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


#include <string>
#include "mem_map.h"
#include <stdint.h>
#include <corkscrew/map_info.h>
#include <unistd.h>
#include "base/stringprintf.h"
#include "ScopedFd.h"
#include "utils.h"

#define USE_ASHMEM 1

#ifdef USE_ASHMEM
#include <cutils/ashmem.h>
#endif

namespace art {

#if !defined(NDEBUG)

static std::ostream& operator<<(std::ostream& os, map_info_t* rhs) {
  for (map_info_t* m = rhs; m != NULL; m = m->next) {
    os << StringPrintf("0x%08x-0x%08x %c%c %s\n",
                       static_cast<uint32_t>(m->start),
                       static_cast<uint32_t>(m->end),
                       m->is_readable ? 'r' : '-', m->is_executable ? 'x' : '-', m->name);
  }
  return os;
}

static void CheckMapRequest(byte* addr, size_t byte_count) {
  if (addr == NULL) {
    return;
  }

  uint32_t base = reinterpret_cast<size_t>(addr);
  uint32_t limit = base + byte_count;

  map_info_t* map_info_list = load_map_info_list(getpid());
  for (map_info_t* m = map_info_list; m != NULL; m = m->next) {
    CHECK(!(base >= m->start && base < m->end)     // start of new within old
        && !(limit > m->start && limit < m->end)   // end of new within old
        && !(base <= m->start && limit > m->end))  // start/end of new includes all of old
        << StringPrintf("Requested region 0x%08x-0x%08x overlaps with existing map 0x%08x-0x%08x (%s)\n",
                        base, limit,
                        static_cast<uint32_t>(m->start), static_cast<uint32_t>(m->end), m->name)
        << map_info_list;
  }
  free_map_info_list(map_info_list);
}

#else
static void CheckMapRequest(byte*, size_t) { }
#endif



#if (ART_GC_SERVICE)

//byte* MemBaseMap::max_covered_address = NULL;


void StructuredMemMap::SetSize(size_t new_size) {
  ashmem_->size_ = new_size;
}

uintptr_t MemBaseMap::GetHighestMemMap(uintptr_t start_address) {

  uintptr_t _highest_address = ((uintptr_t) 0);


  map_info_t* map_info_list = load_map_info_list(getpid());
  for (map_info_t* m = map_info_list; m != NULL; m = m->next) {
    if((m->start < start_address)
        || (m->start > (uintptr_t)(0xb0000000)))
      continue;
    _highest_address = (uintptr_t)std::max<uintptr_t>(_highest_address, m->end);
  }
  free_map_info_list(map_info_list);
  //_highest_address = RoundUp(_highest_address + 1, kPageSize);
  return _highest_address;
}


StructuredMemMap::StructuredMemMap(AShmemMap* ashmem, const std::string& name,
    byte* begin, size_t size, void* base_begin, size_t base_size, int prot) :
      ashmem_(ashmem) {
  //MemMap::AShmemFillData(ashmem_, name, begin, size, base_begin, base_size, prot);
}

StructuredMemMap::~StructuredMemMap(){
  if (BaseBegin() == NULL && BaseSize() == 0) {
    return;
  }
  int result = munmap(BaseBegin(), BaseSize());
  if (result == -1) {
    PLOG(FATAL) << "munmap failed";
  }
}


MemBaseMap* MemBaseMap::CreateStructedMemMap(const char* ashmem_name, byte* addr,
    size_t byte_count, int prot, bool shareMem, AShmemMap* ashmem_mem_map ) {

  if(ashmem_mem_map == NULL) {
    ashmem_mem_map =
      reinterpret_cast<AShmemMap*>(calloc(1, SERVICE_ALLOC_ALIGN_BYTE(AShmemMap)));
  }
  AShmemMap* _checkP =
      MemBaseMap::CreateAShmemMap(ashmem_mem_map, ashmem_name, addr, byte_count,
      prot, shareMem);

  if(_checkP == NULL) {
//    LOG(ERROR) << "MemBaseMap::CreateStructruedMemMap -- > could not allocate shared memory map";
    return NULL;
  }

  StructuredMemMap* _allocated_structured_map =
      new StructuredMemMap(ashmem_mem_map);

  //LOG(ERROR) << "Leaving MemBaseMap::CreateStructedMemMap-->allocating structured memMap";
  return _allocated_structured_map;
}


MemBaseMap* MemBaseMap::CreateStructedMemMap(AShmemMap* ashmem_mem_map) {
  return new StructuredMemMap(ashmem_mem_map);
}

StructuredMemMap* StructuredMemMap::CreateStructuredMemMap(AShmemMap* ashmem_mem_map,
      const char* ashmem_name, byte* addr, size_t byte_count, int prot,
      bool shareMem) {
  AShmemMap* _addr = MEM_MAP::CreateAShmemMap(ashmem_mem_map, ashmem_name, addr,
      byte_count, prot, shareMem);
  if(_addr != ashmem_mem_map) {
    LOG(FATAL) << "could not create StructuredMemMap::CreateStructuredMemMap";
    return NULL;
  }
  return new StructuredMemMap(ashmem_mem_map, std::string(ashmem_name), addr,
      MEM_MAP::AshmemSize(ashmem_mem_map), ashmem_mem_map->base_begin_,
      ashmem_mem_map->base_size_, prot);

}





AShmemMap* MemBaseMap::CreateAShmemMap(AShmemMap* ashmem_mem_map,
    const char* ashmem_name, byte* addr, size_t byte_count, int prot,
    bool shareFlags) {

  size_t page_aligned_byte_count = RoundUp(byte_count, kPageSize);
  int flags = 0;
  int _fd = -1;
#ifdef USE_ASHMEM
  // android_os_Debug.cpp read_mapinfo assumes all ashmem regions associated with the VM are
  // prefixed "dalvik-".
  std::string debug_friendly_name("dalvik-");
  debug_friendly_name += ashmem_name;
  _fd = ashmem_create_region(debug_friendly_name.c_str(), page_aligned_byte_count);
  flags = MAP_PRIVATE;
  if(shareFlags) {
    flags = MAP_SHARED;
    if(addr != NULL) {
      flags |= MAP_FIXED;
    }
  }
  if (_fd == -1) {
    PLOG(ERROR) << "ashmem_create_region failed (" << ashmem_name << ")";
    return NULL;
  }
#else
  flags = MAP_ANONYMOUS;
  if(shareFlags) {
    flags |= MAP_SHARED;
    if(addr != NULL) {
      flags |= MAP_FIXED;
    }
  } else {
    flags |= MAP_PRIVATE;
  }

#endif

  byte* actual = reinterpret_cast<byte*>(mmap(addr, page_aligned_byte_count,
      prot, flags, _fd, 0));

  if (actual == MAP_FAILED) {
    std::string maps;
    ReadFileToString("/proc/self/maps", &maps);
    PLOG(ERROR) << "mmap(" << reinterpret_cast<void*>(addr) << ", "
                << page_aligned_byte_count
                << ", " << prot << ", " << flags << ", " << _fd
                << ", 0) failed for " << ashmem_name
                << "\n" << maps;
    return NULL;
  }

  //max_covered_address = std::max(max_covered_address, addr + page_aligned_byte_count);

  MemBaseMap::AShmemFillData(ashmem_mem_map, actual,
      byte_count, actual, page_aligned_byte_count, prot, flags, _fd,
      debug_friendly_name, NULL);

  return ashmem_mem_map;
}

void MemBaseMap::AshmemResize(AShmemMap* addr, size_t new_size) {
  new_size = RoundUp(new_size, kPageSize);
  size_t _original_size = std::max(addr->base_size_, (size_t)addr->size_);
  if(new_size <= _original_size)
    return;


  int result = munmap(AshmemBegin(addr), addr->base_size_);
  if (result == -1) {
    PLOG(FATAL) << "munmap failed";
  }

  byte* remapped_address = reinterpret_cast<byte*>(mmap(NULL,
      new_size, addr->prot_, addr->flags_,
      addr->fd_, 0));


  if (remapped_address == MAP_FAILED) {
    PLOG(ERROR) << "remap(" << reinterpret_cast<void*>(addr->base_begin_) <<
        ") of ahmem failed";
    return;
  }
//  LOG(ERROR) << "remap(" << reinterpret_cast<void*>(addr->base_begin_) <<
//      " succeeded and new address is " << reinterpret_cast<void*>(remapped_address) <<
//      ", with new size = " << new_size << ", old_sizes = " << _original_size;
  addr->base_begin_ = remapped_address;
  addr->begin_ = remapped_address;
  addr->base_size_ = new_size;
  addr->size_ = new_size;
}

AShmemMap* MemBaseMap::ShareAShmemMap(AShmemMap* source_ashmem_mem_map,
    AShmemMap* dest_ashmem_mem_map) {
  if((source_ashmem_mem_map->flags_ & MAP_SHARED) != 0) {
//    LOG(ERROR) << "the Memory was already shared before";
    return NULL;
  }
  if(dest_ashmem_mem_map == NULL) {
//    LOG(ERROR) << "the destination sharedAshmemmap is not allocated";
    return NULL;
  }

  int flags = MAP_SHARED | MAP_FIXED;
  int _fd = ashmem_create_region(source_ashmem_mem_map->name_,
      source_ashmem_mem_map->size_);
  if (_fd == -1) {
    PLOG(ERROR) << "ashmem_create_region failed (" << source_ashmem_mem_map->name_ << ")";
    return NULL;
  }

  byte* temp_pointer = reinterpret_cast<byte*>(mmap(NULL,
      source_ashmem_mem_map->size_, source_ashmem_mem_map->prot_, MAP_SHARED,
      source_ashmem_mem_map->fd_, 0));

//  LOG(ERROR) << "temp_pointer: " << reinterpret_cast<void*>(temp_pointer) <<
//      ", size:" << source_ashmem_mem_map->size_;

  memcpy(temp_pointer, source_ashmem_mem_map->begin_,
      source_ashmem_mem_map->size_);

  munmap(source_ashmem_mem_map->begin_, source_ashmem_mem_map->size_);



  byte* actual = reinterpret_cast<byte*>(mmap(source_ashmem_mem_map->begin_,
      source_ashmem_mem_map->size_, source_ashmem_mem_map->prot_, flags,
      _fd, 0));
  if (actual == MAP_FAILED) {
    std::string maps;
    ReadFileToString("/proc/self/maps", &maps);
    PLOG(ERROR) << "mremap(" <<
                reinterpret_cast<void*>(source_ashmem_mem_map->begin_) <<
                ", " << source_ashmem_mem_map->size_ <<
                ", " << source_ashmem_mem_map->prot_ << ", " << flags <<
                ", " << _fd << ", 0) failed for " << source_ashmem_mem_map->name_
                << "\n" << maps;
    return NULL;
  }
//  LOG(ERROR) << "...the actual allocated = " << reinterpret_cast<void*>(actual);
  std::string _name_string(source_ashmem_mem_map->name_);
  MemBaseMap::AShmemFillData(dest_ashmem_mem_map, actual,
      source_ashmem_mem_map->size_, actual,
      source_ashmem_mem_map->size_, source_ashmem_mem_map->prot_, flags, _fd,
      _name_string, NULL);

//  LOG(ERROR) << "source_ashmem_mem_map->size_:" << source_ashmem_mem_map->size_
//      << ", dest_ashmem_mem_map->size=" << dest_ashmem_mem_map->size_ <<
//      ", begin = " << reinterpret_cast<void*>(dest_ashmem_mem_map->begin_);


  memcpy(dest_ashmem_mem_map->begin_, temp_pointer, dest_ashmem_mem_map->size_);

  munmap(temp_pointer, dest_ashmem_mem_map->size_);
//  memcpy(dest_ashmem_mem_map->begin_,source_ashmem_mem_map->begin_
//      SERVICE_ALLOC_ALIGN_BYTE(AShmemMap));


  //make sure we copy to the destination before unmapping the region
//  memcpy(dest_ashmem_mem_map, source_ashmem_mem_map,
//      SERVICE_ALLOC_ALIGN_BYTE(AShmemMap));
  if(source_ashmem_mem_map->fd_ != -1) { //unmap the old memory mapped pages
    close(source_ashmem_mem_map->fd_);
    source_ashmem_mem_map->fd_ = -1;
//    if (!(source_ashmem_mem_map->begin_ == NULL && source_ashmem_mem_map->base_size_ == 0)) {
//      AshmemUnMapAtEnd(source_ashmem_mem_map, source_ashmem_mem_map->begin_);
//    }
  }



  //max_covered_address = std::max(max_covered_address, actual + source_ashmem_mem_map->base_size_);
//  dest_ashmem_mem_map->flags_ = flags;
//  dest_ashmem_mem_map->fd_ = _fd;
  //todo: change the file descriptor here
  return dest_ashmem_mem_map;
}


void MemBaseMap::AshmemDestructData(AShmemMap* addr, bool release_pointer) {
  if (addr == NULL)
    return;
  if (!(addr->base_begin_ == NULL && addr->base_size_ == 0)) {
    if(addr->fd_!=-1) {
//      LOG(ERROR) << "before close";
      close(addr->fd_);
//      LOG(ERROR) << "After close";
    }
//    LOG(ERROR) << "before Calling UnmapAtEnd";
    AshmemUnMapAtEnd(addr, reinterpret_cast<byte*>(addr->base_begin_));
//    LOG(ERROR) << "After Calling UnmapAtEnd";
  }

  if(release_pointer)
    free(addr);
}

MemBaseMap::~MemBaseMap(){
}


MemMap::~MemMap(){
  if (BaseBegin() == NULL && BaseSize() == 0) {
    return;
  }
  int result = munmap(BaseBegin(), BaseSize());
  if (result == -1) {
    PLOG(FATAL) << "munmap failed";
  }
}


MemMap::MemMap(const std::string& name, byte* begin, size_t size, void* base_begin,
               size_t base_size, int prot) :
       name_(name), begin_(begin), size_(size), base_begin_(base_begin), base_size_(base_size),
      prot_(prot) {
  if (size_ == 0) {
    CHECK(begin_ == NULL);
    CHECK(base_begin_ == NULL);
    CHECK_EQ(base_size_, 0U);
  } else {
    CHECK(begin_ != NULL);
    CHECK(base_begin_ != NULL);
    CHECK_NE(base_size_, 0U);
  }
};



MEM_MAP* MEM_MAP::MapAnonymous(const char* name, byte* addr, size_t byte_count,
    int prot, bool shareMem) {
  if (byte_count == 0) {
    return new MemMap(name, NULL, 0, NULL, 0, prot);
  }
  size_t page_aligned_byte_count = RoundUp(byte_count, kPageSize);
  CheckMapRequest(addr, page_aligned_byte_count);

#ifdef USE_ASHMEM
  // android_os_Debug.cpp read_mapinfo assumes all ashmem regions associated with the VM are
  // prefixed "dalvik-".
  std::string debug_friendly_name("dalvik-");
  debug_friendly_name += name;
  ScopedFd fd(ashmem_create_region(debug_friendly_name.c_str(), page_aligned_byte_count));
  int flags = MAP_PRIVATE;
  if(shareMem)
    flags = MAP_SHARED;
  if (fd.get() == -1) {
    PLOG(ERROR) << "ashmem_create_region failed (" << name << ")";
    return NULL;
  }
#else
  ScopedFd fd(-1);
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif

  byte* actual = reinterpret_cast<byte*>(mmap(addr, page_aligned_byte_count, prot, flags, fd.get(), 0));
  if (actual == MAP_FAILED) {
    std::string maps;
    ReadFileToString("/proc/self/maps", &maps);
    PLOG(ERROR) << "mmap(" << reinterpret_cast<void*>(addr) << ", " << page_aligned_byte_count
                << ", " << prot << ", " << flags << ", " << fd.get() << ", 0) failed for " << name
                << "\n" << maps;
    return NULL;
  }
  //max_covered_address = std::max(max_covered_address, actual + page_aligned_byte_count);
  if(shareMem) {
    int result = madvise((void*)actual, page_aligned_byte_count, MADV_DONTFORK);
    if (result == -1) {
      PLOG(WARNING) << "madvise failed";
    }
  }
  return new MemMap(name, actual, byte_count, actual, page_aligned_byte_count, prot);
}

bool MemBaseMap::Protect(int prot) {
  if (BaseBegin() == NULL && BaseSize() == 0) {
    SetProt(prot);
    return true;
  }

  if (mprotect(BaseBegin(), BaseSize(), prot) == 0) {
    SetProt(prot);
    return true;
  }

  PLOG(ERROR) << "mprotect(" << reinterpret_cast<void*>(BaseBegin()) << ", "
      << BaseSize() << ", " << prot << ") failed";
  return false;
}

void MemBaseMap::UnMapAtEnd(byte* new_end) {
  DCHECK_GE(new_end, Begin());
  DCHECK_LE(new_end, End());
  size_t unmap_size = End() - new_end;
  munmap(new_end, unmap_size);
  SetSize(Size()-unmap_size);
}


void MemMap::SetSize(size_t new_size) {
  size_ = new_size;
}



MEM_MAP* MemBaseMap::MapFileAtAddress(byte* addr, size_t byte_count,
                                 int prot, int flags, int fd, off_t start,
                                 bool reuse) {
  CHECK_NE(0, prot);
  CHECK_NE(0, flags & (MAP_SHARED | MAP_PRIVATE));
  if (byte_count == 0) {
    return new MemMap("file", NULL, 0, NULL, 0, prot);
  }
  // Adjust 'offset' to be page-aligned as required by mmap.
  int page_offset = start % kPageSize;
  off_t page_aligned_offset = start - page_offset;
  // Adjust 'byte_count' to be page-aligned as we will map this anyway.
  size_t page_aligned_byte_count = RoundUp(byte_count + page_offset, kPageSize);
  // The 'addr' is modified (if specified, ie non-null) to be page aligned to the file but not
  // necessarily to virtual memory. mmap will page align 'addr' for us.
  byte* page_aligned_addr = (addr == NULL) ? NULL : (addr - page_offset);
  if (!reuse) {
    // reuse means it is okay that it overlaps an existing page mapping.
    // Only use this if you actually made the page reservation yourself.
    CheckMapRequest(page_aligned_addr, page_aligned_byte_count);
  } else {
    CHECK(addr != NULL);
  }
  byte* actual = reinterpret_cast<byte*>(mmap(page_aligned_addr,
                                              page_aligned_byte_count,
                                              prot,
                                              flags,
                                              fd,
                                              page_aligned_offset));
  if (actual == MAP_FAILED) {
    std::string maps;
    ReadFileToString("/proc/self/maps", &maps);
    PLOG(ERROR) << "mmap(" << reinterpret_cast<void*>(page_aligned_addr)
                << ", " << page_aligned_byte_count
                << ", " << prot << ", " << flags << ", " << fd << ", " << page_aligned_offset
                << ") failed\n" << maps;
    return NULL;
  }
  //max_covered_address = std::max(max_covered_address, actual + page_aligned_byte_count);
  return new MemMap("file", actual + page_offset, byte_count, actual, page_aligned_byte_count,
                    prot);
}







#else


MemMap* MemMap::MapAnonymous(const char* name, byte* addr, size_t byte_count, int prot, bool shareMem) {
  if (byte_count == 0) {
    return new MemMap(name, NULL, 0, NULL, 0, prot);
  }
  size_t page_aligned_byte_count = RoundUp(byte_count, kPageSize);
  CheckMapRequest(addr, page_aligned_byte_count);

#ifdef USE_ASHMEM
  // android_os_Debug.cpp read_mapinfo assumes all ashmem regions associated with the VM are
  // prefixed "dalvik-".
  std::string debug_friendly_name("dalvik-");
  debug_friendly_name += name;
  ScopedFd fd(ashmem_create_region(debug_friendly_name.c_str(), page_aligned_byte_count));
  int flags = MAP_PRIVATE;
  if(shareMem)
    flags = MAP_SHARED;
  if (fd.get() == -1) {
    PLOG(ERROR) << "ashmem_create_region failed (" << name << ")";
    return NULL;
  }
#else
  ScopedFd fd(-1);
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif

  byte* actual = reinterpret_cast<byte*>(mmap(addr, page_aligned_byte_count, prot, flags, fd.get(), 0));
  if (actual == MAP_FAILED) {
    std::string maps;
    ReadFileToString("/proc/self/maps", &maps);
    PLOG(ERROR) << "mmap(" << reinterpret_cast<void*>(addr) << ", " << page_aligned_byte_count
                << ", " << prot << ", " << flags << ", " << fd.get() << ", 0) failed for " << name
                << "\n" << maps;
    return NULL;
  }
  if(shareMem) {
    int result = madvise((void*)actual, page_aligned_byte_count, MADV_DONTFORK);
    if (result == -1) {
      PLOG(WARNING) << "madvise failed";
    }
  }
  return new MemMap(name, actual, byte_count, actual, page_aligned_byte_count, prot);
}

MemMap* MemMap::MapFileAtAddress(byte* addr, size_t byte_count,
                                 int prot, int flags, int fd, off_t start, bool reuse) {
  CHECK_NE(0, prot);
  CHECK_NE(0, flags & (MAP_SHARED | MAP_PRIVATE));
  if (byte_count == 0) {
    return new MemMap("file", NULL, 0, NULL, 0, prot);
  }
  // Adjust 'offset' to be page-aligned as required by mmap.
  int page_offset = start % kPageSize;
  off_t page_aligned_offset = start - page_offset;
  // Adjust 'byte_count' to be page-aligned as we will map this anyway.
  size_t page_aligned_byte_count = RoundUp(byte_count + page_offset, kPageSize);
  // The 'addr' is modified (if specified, ie non-null) to be page aligned to the file but not
  // necessarily to virtual memory. mmap will page align 'addr' for us.
  byte* page_aligned_addr = (addr == NULL) ? NULL : (addr - page_offset);
  if (!reuse) {
    // reuse means it is okay that it overlaps an existing page mapping.
    // Only use this if you actually made the page reservation yourself.
    CheckMapRequest(page_aligned_addr, page_aligned_byte_count);
  } else {
    CHECK(addr != NULL);
  }
  byte* actual = reinterpret_cast<byte*>(mmap(page_aligned_addr,
                                              page_aligned_byte_count,
                                              prot,
                                              flags,
                                              fd,
                                              page_aligned_offset));
  if (actual == MAP_FAILED) {
    std::string maps;
    ReadFileToString("/proc/self/maps", &maps);
    PLOG(ERROR) << "mmap(" << reinterpret_cast<void*>(page_aligned_addr)
                << ", " << page_aligned_byte_count
                << ", " << prot << ", " << flags << ", " << fd << ", " << page_aligned_offset
                << ") failed\n" << maps;
    return NULL;
  }
  return new MemMap("file", actual + page_offset, byte_count, actual, page_aligned_byte_count,
                    prot);
}

MemMap::~MemMap() {
  if (BaseBegin() == NULL && BaseSize() == 0) {
    return;
  }
  int result = munmap(BaseBegin(), BaseSize());
  if (result == -1) {
    PLOG(FATAL) << "munmap failed";
  }
}

MemMap::MemMap(const std::string& name, byte* begin, size_t size, void* base_begin,
               size_t base_size, int prot)
    : name_(name), begin_(begin), size_(size), base_begin_(base_begin), base_size_(base_size),
      prot_(prot) {
  if (size_ == 0) {
    CHECK(begin_ == NULL);
    CHECK(base_begin_ == NULL);
    CHECK_EQ(base_size_, 0U);
  } else {
    CHECK(begin_ != NULL);
    CHECK(base_begin_ != NULL);
    CHECK_NE(base_size_, 0U);
  }
};

void MemMap::UnMapAtEnd(byte* new_end) {
  DCHECK_GE(new_end, Begin());
  DCHECK_LE(new_end, End());
  size_t unmap_size = End() - new_end;
  munmap(new_end, unmap_size);
  size_ -= unmap_size;
}

bool MemMap::Protect(int prot) {
  if (base_begin_ == NULL && base_size_ == 0) {
    prot_ = prot;
    return true;
  }

  if (mprotect(base_begin_, base_size_, prot) == 0) {
    prot_ = prot;
    return true;
  }

  PLOG(ERROR) << "mprotect(" << reinterpret_cast<void*>(base_begin_) << ", "
      << base_size_ << ", " << prot << ") failed";
  return false;
}

#endif






//StructuredMemMap::StructuredMemMap(AShmemMap* ashmem) :
//    MemMap((std::string(ashmem->name_),
//            MemMap::AshmemBegin(ashmem),
//            MemMap::AshmemSize(ashmem),
//            ashmem->base_begin_,
//            ashmem->base_size_,
//            ashmem->prot_))
//    , ashmem_(ashmem) {
//
//}




//StructuredMemMap* StructuredMemMap::CreateStructuredMemMap(AShmemMap* ashmem_mem_map,
//    const char* ashmem_name, byte* begin, size_t size, void* base_begin,
//    size_t base_size, int prot) {
//  return new StructuredMemMap(ashmem_mem_map, ashmem_name, begin,
//      size, base_begin, base_size, prot);
//}


}  // namespace art
