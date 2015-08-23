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
#include "gc/gcservice_allocator/shared_atomic_stack.h"

namespace art {
namespace gc {

class SharedHeap {
public:

  static SharedHeap* CreateSharedHeap(ServiceAllocator* service_alloc);
  SharedSpaceBitmapMeta* getSharedSpaceBitmap(void);
  static SharedHeap* ConstructHeapServer(int vm_index);
private:
  SharedHeapMetada* shared_metadata_;
  accounting::SharedCardTable* card_table_;

  /* synchronization on the global locks */
  InterProcessMutex* ipc_global_mu_;
  InterProcessConditionVariable* ipc_global_cond_;

  /* synchronization on the conc requests */
  InterProcessMutex* conc_req_mu_;
  InterProcessConditionVariable* conc_req_cond_;

  /* class members */
  accounting::SharedCardTable* card_table_;
  SharedAtomicStack* live_stack_;
  SharedAtomicStack* mark_stack_;
  SharedAtomicStack* allocation_stack;


  SharedHeap(int _pid, SharedHeapMetada* metadata);
  SharedHeap(SharedHeapMetada* metadata);

};//SharedHeap

}//namespace gc
}//namespace art

#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_HEAP_H_ */
