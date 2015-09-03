/*
 * Copyright (C) 2010 The Android Open Source Project
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
#include <sstream>
#include "card_table.h"

#include "base/logging.h"
#include "card_table-inl.h"
#include "gc/heap.h"
#include "gc/space/space.h"
#include "gc/gcservice/common.h"
#include "heap_bitmap.h"
#include "runtime.h"
#include "utils.h"
#include "thread.h"

namespace art {
namespace gc {
namespace accounting {

/*
 * Maintain a card table from the write barrier. All writes of
 * non-NULL values to heap addresses should go through an entry in
 * WriteBarrier, and from there to here.
 *
 * The heap is divided into "cards" of GC_CARD_SIZE bytes, as
 * determined by GC_CARD_SHIFT. The card table contains one byte of
 * data per card, to be used by the GC. The value of the byte will be
 * one of GC_CARD_CLEAN or GC_CARD_DIRTY.
 *
 * After any store of a non-NULL object pointer into a heap object,
 * code is obliged to mark the card dirty. The setters in
 * object.h [such as SetFieldObject] do this for you. The
 * compiler also contains code to mark cards as dirty.
 *
 * The card table's base [the "biased card table"] gets set to a
 * rather strange value.  In order to keep the JIT from having to
 * fabricate or load GC_DIRTY_CARD to store into the card table,
 * biased base is within the mmap allocation at a point where its low
 * byte is equal to GC_DIRTY_CARD. See CardTable::Create for details.
 */

CardTable* CardTable::Create(const byte* heap_begin, size_t heap_capacity) {
  /* Set up the card table */
  size_t capacity = heap_capacity / kCardSize;
  /* Allocate an extra 256 bytes to allow fixed low-byte of base */
  int _fd = 0;
  GCSERV_ILOG << "--- creating card table ---";
  UniquePtr<BaseMapMem> mem_map(MemMap::MapAnonymous("card table", NULL,
                                                 capacity + 256, PROT_READ | PROT_WRITE));
  mem_map->SetFD(_fd);// = _fd;
  CHECK(mem_map.get() != NULL) << "couldn't allocate card table";
  // All zeros is the correct initial value; all clean. Anonymous mmaps are initialized to zero, we
  // don't clear the card table to avoid unnecessary pages being allocated
  COMPILE_ASSERT(kCardClean == 0, card_clean_must_be_0);

  byte* cardtable_begin = mem_map->Begin();
  CHECK(cardtable_begin != NULL);

  // We allocated up to a bytes worth of extra space to allow biased_begin's byte value to equal
  // GC_CARD_DIRTY, compute a offset value to make this the case
  size_t offset = 0;
  byte* biased_begin = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(cardtable_begin) -
      (reinterpret_cast<uintptr_t>(heap_begin) >> kCardShift));
  if (((uintptr_t)biased_begin & 0xff) != kCardDirty) {
    int delta = kCardDirty - (reinterpret_cast<int>(biased_begin) & 0xff);
    offset = delta + (delta < 0 ? 0x100 : 0);
    biased_begin += offset;
  }
  CHECK_EQ(reinterpret_cast<int>(biased_begin) & 0xff, kCardDirty);

  return new CardTable(mem_map.release(), biased_begin, offset, heap_begin);
}

CardTable::CardTable(BaseMapMem* mem_map, byte* biased_begin, size_t offset,
    const byte* heap_begin)
    : mem_map_(mem_map), biased_begin_(biased_begin), offset_(offset),
      heap_begin_(heap_begin) {
  GCSERV_ILOG << "--- constructor card table ---";
  byte* __attribute__((unused)) begin = mem_map_->Begin() + offset_;
  byte* __attribute__((unused)) end = mem_map_->End();
}

void CardTable::ClearSpaceCards(space::ContinuousSpace* space) {
  // TODO: clear just the range of the table that has been modified
  byte* card_start = CardFromAddr(space->Begin());
  byte* card_end = CardFromAddr(space->End());  // Make sure to round up.
  memset(reinterpret_cast<void*>(card_start), kCardClean, card_end - card_start);
}

void CardTable::ClearCardTable() {
  // TODO: clear just the range of the table that has been modified
  memset(mem_map_->Begin(), kCardClean, mem_map_->Size());
}

bool CardTable::AddrIsInCardTable(const void* addr) const {
  return IsValidCard(biased_begin_ + ((uintptr_t)addr >> kCardShift));
}

void CardTable::CheckAddrIsInCardTable(const byte* addr) const {
  byte* card_addr = biased_begin_ + ((uintptr_t)addr >> kCardShift);
  byte* begin = mem_map_->Begin() + offset_;
  byte* end = mem_map_->End();
  CHECK(AddrIsInCardTable(addr))
      << "Card table " << this
      << " begin: " << reinterpret_cast<void*>(begin)
      << " end: " << reinterpret_cast<void*>(end)
      << " card_addr: " << reinterpret_cast<void*>(card_addr)
      << " heap begin: " << AddrFromCard(begin)
      << " heap end: " << AddrFromCard(end)
      << " addr: " << reinterpret_cast<const void*>(addr);
}

void CardTable::VerifyCardTable() {
  UNIMPLEMENTED(WARNING) << "Card table verification";
}


void CardTable::DumpCardTable(std::ostream& os) {
  Thread* self = Thread::Current();
  os << self->GetTid() << " --- " << mem_map_->getName() <<
      " ----- SharedCardTable: biased_begin_: " <<
      reinterpret_cast<void*>(GetBiasedBegin());

  os <<
      " ----- SharedCardTable: offset_: " << getOffset();
  os <<
      " ----- SharedCardTable: owner_begin_: " <<
      reinterpret_cast<void*>(getBegin());
  os <<
      " ----- SharedCardTable: owner_base_begin_: " <<
      reinterpret_cast<void*>(getBaseBegin());
  os <<
      " ----- SharedCardTable: base_size_: " << getBaseSize();
  os <<
      " ----- SharedCardTable: size_: " << getSize();
  os <<
      " ----- SharedCardTable: fd_: " << getFD();
  os <<
      " ----- SharedCardTable: prot_: " << getProt();

  os <<
      "\n ===========================";
}


bool CardTable::updateProtection(int newProtection) {
  return mem_map_->Protect(newProtection);
}


///// reset the card table to enable sharing with gc service
//void CardTable::ShareCardTable(void) {
//  CardTable* orig_card_table = this;
//  GCSERV_CLIENT_ILOG << "restart cardTable to enable sharing";
//  byte* original_begin = orig_card_table->getBegin();
//  size_t origi_size = orig_card_table->getSize();
//
//  orig_card_table->mem_map_.reset();
//
//  std::ostringstream oss;
//  oss << "shared card-" << getpid();
//  std::string debug_friendly_name(oss.str());
//  UniquePtr<BaseMapMem>
//    shared_mem_map(MemMap::MapSharedMemoryAnonymous(debug_friendly_name.c_str(),
//        original_begin, origi_size, PROT_READ | PROT_WRITE));
//
//
//  GCSERV_CLIENT_ILOG << "~~~~~ Memory mapped ~~~~~ original _fd = "  <<
//      shared_mem_map->GetFD();
//
//  //UniquePtr<MemMap> mem_map(shared_mem_map->GetLocalMemMap());
//
//  orig_card_table->mem_map_.reset(mem_map.release());
//  byte* cardtable_begin = orig_card_table->mem_map_->Begin();
//  size_t offset = 0;
//  byte* biased_begin = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(cardtable_begin) -
//        (reinterpret_cast<uintptr_t>(orig_card_table->heap_begin_) >> kCardShift));
//  if (((uintptr_t)biased_begin & 0xff) != kCardDirty) {
//    int delta = kCardDirty - (reinterpret_cast<int>(biased_begin) & 0xff);
//    offset = delta + (delta < 0 ? 0x100 : 0);
//    biased_begin += offset;
//  }
//
//  orig_card_table->DumpCardTable(LOG(ERROR));
//}


/// reset the card table to enable sharing with gc service
void CardTable::ShareCardTable(gcservice::SharedMemMapMeta* metaMemory) {
  CardTable* orig_card_table = this;
  GCSERV_CLIENT_ILOG << "restart cardTable to enable sharing";
  byte* original_begin = orig_card_table->getBegin();
  size_t origi_size = orig_card_table->getSize();

  orig_card_table->mem_map_.reset();

  std::ostringstream oss;
  oss << "shared card-" << getpid();
  std::string debug_friendly_name(oss.str());
  UniquePtr<BaseMapMem>
    shared_mem_map(MemMap::MapSharedMemoryWithMeta(debug_friendly_name.c_str(),
        original_begin, origi_size, PROT_READ | PROT_WRITE, metaMemory));


  GCSERV_CLIENT_ILOG << "xxx~~~~~ Memory mapped ~~~~~ original _fd = "  <<
      shared_mem_map->GetFD();

  //UniquePtr<MemMap> mem_map(shared_mem_map->GetLocalMemMap());

  orig_card_table->mem_map_.reset(shared_mem_map.release());
  byte* cardtable_begin = orig_card_table->mem_map_->Begin();
  size_t offset = 0;
  byte* biased_begin = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(cardtable_begin) -
        (reinterpret_cast<uintptr_t>(orig_card_table->heap_begin_) >> kCardShift));
  if (((uintptr_t)biased_begin & 0xff) != kCardDirty) {
    int delta = kCardDirty - (reinterpret_cast<int>(biased_begin) & 0xff);
    offset = delta + (delta < 0 ? 0x100 : 0);
    biased_begin += offset;
  }

  orig_card_table->DumpCardTable(LOG(ERROR));
}

//
///* reset the card table to enable sharing with gc service */
//void CardTable::ResetCardTable(CardTable* orig_card_table) {
//  orig_card_table->DumpCardTable(LOG(ERROR));
//  GCSERV_PROC_ILOG << "**** Resetting CardTable Mapping ****";
//  byte* original_begin = orig_card_table->getBegin();
//  size_t origi_size = orig_card_table->getSize();
//  orig_card_table->mem_map_.reset();
//  GCSERV_PROC_ILOG << "~~~~~ Done Reset ~~~~~";
//  int _fd = 0;
//  std::ostringstream oss;
//  oss << "shared card-" << getpid();
//  std::string debug_friendly_name(oss.str());
//  UniquePtr<MemMap> mem_map(MemMap::MapSharedMemoryAnonymous(debug_friendly_name.c_str(),
//      original_begin, origi_size,
//      PROT_READ | PROT_WRITE, &_fd));
//  GCSERV_PROC_ILOG << "~~~~~ Memory mapped ~~~~~ original _fd = "  << _fd;
//  int new_fd = _fd;
//  GCSERV_PROC_ILOG << "~~~~~ Memory mapped ~~~~~ new _fd = "  << new_fd;
//  mem_map->fd_ = new_fd;
//
//  GCServiceDaemon::GCPMapFileDescriptor(mem_map->fd_);
//
//  orig_card_table->mem_map_.reset(mem_map.release());
//  GCSERV_PROC_ILOG << "~~~~~ put new pointer ~~~~~";
//  byte* cardtable_begin = orig_card_table->mem_map_->Begin();
//  GCSERV_PROC_ILOG << "~~~~~ put new pointer: " <<
//      reinterpret_cast<void*>(cardtable_begin);
//  // We allocated up to a bytes worth of extra space to allow biased_begin's byte value to equal
//  // GC_CARD_DIRTY, compute a offset value to make this the case
//  size_t offset = 0;
//  byte* biased_begin = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(cardtable_begin) -
//      (reinterpret_cast<uintptr_t>(orig_card_table->heap_begin_) >> kCardShift));
//  if (((uintptr_t)biased_begin & 0xff) != kCardDirty) {
//    int delta = kCardDirty - (reinterpret_cast<int>(biased_begin) & 0xff);
//    offset = delta + (delta < 0 ? 0x100 : 0);
//    biased_begin += offset;
//  }
//
//  GCSERV_CLIENT_ILOG << "~~~~~ biased begin: " <<
//      reinterpret_cast<void*>(biased_begin);
//
//
////  UniquePtr<MemMap> _newCard(CardTable(mem_map.release(), biased_begin, offset, heap_begin));
//  orig_card_table->DumpCardTable(LOG(ERROR));
//}

}  // namespace accounting
}  // namespace gc
}  // namespace art
