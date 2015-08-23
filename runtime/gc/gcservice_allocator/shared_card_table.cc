/*
 * shared_card_table.cc
 *
 *  Created on: Aug 19, 2015
 *      Author: hussein
 */



#include "utils.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"
#include "gc/accounting/space_bitmap.h"
#include "gc/gcservice_allocator/service_allocator.h"
#include "gc/gcservice_allocator/shared_space_bitmap.h"
#include "gc/gcservice_allocator/shared_card_table.h"
#include "gc_profiler/MProfiler.h"
#include "gc_profiler/GCService.h"

namespace art {

namespace gc {
namespace accounting {



void SharedCardTable::DumpSharedCardTable(std::ostream& os) {
  Thread* self = Thread::Current();
  SharedMemMapMeta* shMemMeta = &meta_->mem_meta_;

  os << self->GetTid() <<
      " ----- SharedCardTable: biased_begin_: " <<
      reinterpret_cast<void*>(meta_->biased_begin_);

  os <<
      " ----- SharedCardTable: offset_: " << meta_->offset_;
  os <<
      " ----- SharedCardTable: owner_begin_: " <<
      reinterpret_cast<void*>(shMemMeta->owner_begin_);
  os <<
      " ----- SharedCardTable: owner_base_begin_: " <<
      reinterpret_cast<void*>(shMemMeta->owner_base_begin_);
  os <<
      " ----- SharedCardTable: base_size_: " << shMemMeta->base_size_;
  os <<
      " ----- SharedCardTable: size_: " << shMemMeta->size_;
  os <<
      " ----- SharedCardTable: fd_: " << shMemMeta->fd_;
  os <<
      " ----- SharedCardTable: prot_: " << shMemMeta->prot_;


  os <<
      "\n ------------------------------";
}

SharedCardTable::SharedCardTable(SharedCardTableMeta* metaData) :
    meta_(metaData), mmap_(NULL) {

}

SharedCardTable* SharedCardTable::ConstructSharedCardTable(SharedCardTableMeta* shared_meta) {
  SharedCardTable* card_table = new SharedCardTable(shared_meta);
  card_table->DumpSharedCardTable(LOG(ERROR));


  int mappedFD = shared_meta->mem_meta_.fd_;//dup(shared_meta->mem_meta_.fd_);

  GCSERV_DAEM_VLOG(INFO) <<
      "SharedCardTable: server Side-------  the mapped file descriptor is: " <<  mappedFD;

 // if(false) {
  card_table->mmap_ = MemMap::MapSharedProcessFile(NULL,
      shared_meta->mem_meta_.size_, shared_meta->mem_meta_.prot_,
      mappedFD);
 // }
  if(card_table->mmap_ == NULL) {
    LOG(ERROR) << "SharedCardTable: server Side------- Could not map FD: " <<
        shared_meta->mem_meta_.fd_;
    return NULL;
  }

  GCSERV_DAEM_VLOG(INFO) <<
      "SharedCardTable: server Side-------  succeeded to map the fd: " <<
      shared_meta->mem_meta_.fd_;
  card_table->mmap_->fd_ = shared_meta->mem_meta_.fd_;

  return card_table;
}


SharedCardTable* SharedCardTable::CreateSharedCardTable(SharedCardTableMeta* meta_p,
    accounting::CardTable* cardTable) {

  SharedMemMapMeta* shMemMeta = &meta_p->mem_meta_;
  meta_p->biased_begin_   = cardTable->GetBiasedBegin();
  meta_p->offset_         = cardTable->getOffset();
  shMemMeta->owner_begin_ = cardTable->getBegin();
  shMemMeta->owner_base_begin_ = cardTable->getBaseBegin();
  shMemMeta->base_size_ = cardTable->getBaseSize();
  shMemMeta->size_      = cardTable->getSize();
  shMemMeta->fd_        = cardTable->getFD();
  shMemMeta->prot_      = cardTable->getProt();


  SharedCardTable* sharedCardTable = new SharedCardTable(meta_p);

  sharedCardTable->DumpSharedCardTable(LOG(ERROR));

  return sharedCardTable;
}


}//namespace accounting
}//namespace gc
}//namespace art
