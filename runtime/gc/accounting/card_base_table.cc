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
  return IsValidCard(BiasedBegin() + ((uintptr_t)addr >> kCardShift));
}


void CardBaseTable::ClearSpaceCards(space::ContinuousSpace* space) {
  // TODO: clear just the range of the table that has been modified
  byte* card_start = CardFromAddr(space->Begin());
  byte* card_end = CardFromAddr(space->End());  // Make sure to round up.
  memset(reinterpret_cast<void*>(card_start), kCardClean, card_end - card_start);
}


void CardBaseTable::CheckAddrIsInCardTable(const byte* addr) const {
  byte* card_addr = BiasedBegin() + ((uintptr_t)addr >> kCardShift);
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

  /* Set up the card table */
  size_t capacity = heap_capacity / kCardSize;

  /* Allocate an extra 256 bytes to allow fixed low-byte of base */
  AShmemMap* _mem_map_structure =
      MemMap::CreateAShmemMap(&fields_memory->mem_map_, "card table", NULL,
          capacity + 256, PROT_READ | PROT_WRITE);


  CHECK(_mem_map_structure != &fields_memory->mem_map_) <<
      "couldn't allocate card table";
  // All zeros is the correct initial value; all clean. Anonymous mmaps are initialized to zero, we
  // don't clear the card table to avoid unnecessary pages being allocated
  COMPILE_ASSERT(kCardClean == 0, card_clean_must_be_0);

  byte* cardtable_begin = MemMap::AshmemBegin(_mem_map_structure);
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

  return new CardBaseTable(biased_begin, offset, fields_memory);

}

CardBaseTable::CardBaseTable(byte* biased_begin, size_t offset,
    CardBaseTableFields* fields_memory): fields_(fields_memory){
  memcpy((void*)&fields_->biased_begin_, &biased_begin, sizeof(byte*));
  memcpy((void*)&fields_->offset_, &offset, sizeof(size_t));
}


}  // namespace accounting
}  // namespace gc
}  // namespace art