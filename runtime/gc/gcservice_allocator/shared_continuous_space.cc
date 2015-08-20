/*
 * shared_continuous_space.cc
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
#include "gc/gcservice_allocator/shared_continuous_space.h"


namespace art {

namespace gc {

SharedContinuousSpace* SharedContinuousSpace::CreateSharedContinuousSpace() {
  return NULL;
}

SharedContinuousSpace::SharedContinuousSpace(void) {

}

}//namespace gc
}//namespace art
