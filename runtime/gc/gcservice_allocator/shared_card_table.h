/*
 * shared_card_table.h
 *
 *  Created on: Aug 19, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_CARD_TABLE_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_CARD_TABLE_H_


#include "globals.h"
#include <stdint.h>


#include "base/mutex.h"
#include "gc/accounting/space_bitmap.h"
#include "gc/gcservice_allocator/service_allocator.h"

namespace art {

namespace gc {
namespace accounting {

class SharedCardTable {
public:
  static SharedCardTable* CreateSharedCardTable(SharedCardTableMeta* meta_p,
      accounting::CardTable* cardTable);
private:
  SharedCardTable(SharedCardTableMeta*);
  SharedCardTableMeta* meta_;
};//SharedCardTable


}//namespace accounting
}//namespace gc

}//namespace art

#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_CARD_TABLE_H_ */
