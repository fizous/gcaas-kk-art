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

namespace art {

namespace gc {
namespace accounting {

SharedCardTable::SharedCardTable(SharedCardTableMeta* metaData) :
    meta_(metaData) {

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

  Thread* self = Thread::Current();
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: biased_begin_: " << meta_p->biased_begin_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: offset_: " << meta_p->offset_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: owner_begin_: " << shMemMeta->owner_begin_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: owner_base_begin_: " << shMemMeta->owner_base_begin_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: base_size_: " << shMemMeta->base_size_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: size_: " << shMemMeta->size_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: fd_: " << shMemMeta->fd_;
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
      " ----- SharedCardTable: prot_: " << shMemMeta->prot_;

  SharedCardTable* sharedCardTable = new SharedCardTable(meta_p);
  return sharedCardTable;
}


}//namespace accounting
}//namespace gc
}//namespace art
