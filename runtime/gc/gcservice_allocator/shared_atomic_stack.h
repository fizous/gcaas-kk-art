/*
 * shared_atomic_stack.h
 *
 *  Created on: Aug 19, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_ATOMIC_STACK_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_ATOMIC_STACK_H_

#include "gc/gcservice_allocator/service_allocator.h"

namespace art {
namespace gc {

class SharedAtomicStack {

  SharedAtomicStack* CreateSharedAtomicStack(void);
private:
  SharedAtomicStack();
  SharedAtomicStackMeta* meta_;
};//SharedAtomicStack

}//namespace gc
}//namespace art


#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_ATOMIC_STACK_H_ */
