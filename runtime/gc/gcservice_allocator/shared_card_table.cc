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

namespace art {

namespace gc {
namespace accounting {

SharedCardTable::SharedCardTable(SharedCardTableMeta* metaData) :
    meta_(metaData) {

}


SharedCardTable* SharedCardTable::CreateSharedCardTable(void) {
  return NULL;
}


}//namespace accounting
}//namespace gc
}//namespace art
