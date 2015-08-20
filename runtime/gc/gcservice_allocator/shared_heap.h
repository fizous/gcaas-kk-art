/*
 * shared_heap.h
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_HEAP_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_HEAP_H_

#include "os.h"
#include "globals.h"
#include "base/mutex.h"
#include "gc/gcservice_allocator/service_allocator.h"
#include "gc/gcservice_allocator/shared_card_table.h"

namespace art {
namespace gc {

class SharedHeap {
public:

  static SharedHeap* CreateSharedHeap(ServiceAllocator* service_alloc);
  SharedSpaceBitmapMeta* getSharedSpaceBitmap(void);

private:
  SharedHeapMetada* shared_metadata_;

  /* class members */
  accounting::SharedCardTable* card_table_;
  SharedAtomicStack* live_stack_;
  SharedAtomicStack* mark_stack_;
  SharedAtomicStack* allocation_stack;


  SharedHeap(int _pid, SharedHeapMetada* metadata);

};//SharedHeap

}//namespace gc
}//namespace art

#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_HEAP_H_ */
