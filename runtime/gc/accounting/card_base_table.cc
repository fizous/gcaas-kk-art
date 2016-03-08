/*
 * card_base_table.cc
 *
 *  Created on: Sep 21, 2015
 *      Author: hussein
 */


#include "card_table.h"

#include "base/logging.h"
#include "card_table-inl.h"
#include "gc/heap.h"
#include "gc/space/space.h"
#include "heap_bitmap.h"
#include "runtime.h"
#include "utils.h"


namespace art {
namespace gc {
namespace accounting {


CardBaseTable::~CardBaseTable() {
//  MemMap::AshmemDestructData(&fields_->mem_map_, false);
  free(fields_);
  fields_ = NULL;
}

void CardBaseTable::ClearCardTable() {
  // TODO: clear just the range of the table that has been modified
  memset(MemMapBegin(), kCardClean, MemMapSize());
}

bool CardBaseTable::IsValidCard(const byte* card_addr) const {
  byte* begin = MemMapBegin() + Offset();
  byte* end = MemMapEnd();
  return card_addr >= begin && card_addr < end;
}

bool CardBaseTable::AddrIsInCardTable(const void* addr) const {
  return IsValidCard(GetBiasedBegin() + ((uintptr_t)addr >> kCardShift));
}


void CardBaseTable::ClearSpaceCards(space::ContinuousSpace* space) {
  // TODO: clear just the range of the table that has been modified
  byte* card_start = CardFromAddr(space->Begin());
  byte* card_end = CardFromAddr(space->End());  // Make sure to round up.
  memset(reinterpret_cast<void*>(card_start), kCardClean, card_end - card_start);
}


void CardBaseTable::CheckAddrIsInCardTable(const byte* addr) const {
  byte* card_addr = GetBiasedBegin() + ((uintptr_t)addr >> kCardShift);
  byte* begin = MemMapBegin() + Offset();
  byte* end = MemMapEnd();
  CHECK(AddrIsInCardTable(addr))
      << "Card table " << this
      << " begin: " << reinterpret_cast<void*>(begin)
      << " end: " << reinterpret_cast<void*>(end)
      << " card_addr: " << reinterpret_cast<void*>(card_addr)
      << " heap begin: " << AddrFromCard(begin)
      << " heap end: " << AddrFromCard(end)
      << " addr: " << reinterpret_cast<const void*>(addr);
}


CardBaseTable* CardBaseTable::Create(const byte* heap_begin,
    size_t heap_capacity, CardBaseTableFields* fields_memory) {
  if(fields_memory == NULL) {
    fields_memory = reinterpret_cast<CardBaseTableFields*>(calloc(1,
        SERVICE_ALLOC_ALIGN_BYTE(CardBaseTableFields)));
  }

  return new CardBaseTable(heap_begin, heap_capacity, fields_memory);
}

CardBaseTable::CardBaseTable(const byte* heap_begin, size_t heap_capacity,
    CardBaseTableFields* fields_memory): fields_(fields_memory) {

  /* Set up the card table */
  size_t capacity = heap_capacity / kCardSize;

  /* Allocate an extra 256 bytes to allow fixed low-byte of base */
  mem_map_.reset(MEM_MAP::CreateStructedMemMap("card table", NULL,
      capacity + 256, PROT_READ | PROT_WRITE, false,
          &(fields_->mem_map_)));


//  AShmemMap* _mem_map_structure =
//      MEM_MAP::CreateAShmemMap(&(fields_memory->mem_map_), "card table", NULL,
//          capacity + 256, PROT_READ | PROT_WRITE);
  CHECK(mem_map_.get() != NULL) <<
      "couldn't allocate card table";
  // All zeros is the correct initial value; all clean. Anonymous mmaps are initialized to zero, we
  // don't clear the card table to avoid unnecessary pages being allocated
  COMPILE_ASSERT(kCardClean == 0, card_clean_must_be_0);

  byte* cardtable_begin = mem_map_.get()->Begin();
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
  memcpy((void*)&fields_->biased_begin_, &biased_begin, sizeof(byte*));
  memcpy((void*)&fields_->offset_, &offset, sizeof(size_t));

}


//CardBaseTable::CardBaseTable(byte* biased_begin, size_t offset,
//    CardBaseTableFields* fields_memory): fields_(fields_memory){
//  memcpy((void*)&fields_->biased_begin_, &biased_begin, sizeof(byte*));
//  memcpy((void*)&fields_->offset_, &offset, sizeof(size_t));
//}


bool CardBaseTable::shareCardTable(CardBaseTableFields* fields_memory) {
  if(fields_memory != fields_) {//new process
    byte* cardtable_begin = mem_map_.get()->Begin();
    size_t card_byte_count = mem_map_.get()->BaseSize();
    mem_map_.reset(NULL);
    mem_map_.reset(MEM_MAP::CreateStructedMemMap("card table", cardtable_begin,
        card_byte_count, PROT_READ | PROT_WRITE, true,
            &(fields_memory->mem_map_)));

    memcpy((void*)&fields_memory->biased_begin_, &fields_->biased_begin_, sizeof(byte*));
    memcpy((void*)&fields_memory->offset_, &fields_->offset_, sizeof(size_t));
    free(fields_);
    fields_ = fields_memory;
  }


  return true;
}

}  // namespace accounting
}  // namespace gc
}  // namespace art
