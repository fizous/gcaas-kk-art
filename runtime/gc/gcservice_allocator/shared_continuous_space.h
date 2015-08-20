/*
 * shared_continuous_space.h
 *
 *  Created on: Aug 19, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_CONTINUOUS_SPACE_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_CONTINUOUS_SPACE_H_


namespace art {

namespace gc {

class SharedContinuousSpace {
public:
  static SharedContinuousSpace* CreateSharedContinuousSpace();
  SharedContinuousSpace();
private:
  SharedContinuousSpaceMeta* meta_;
  accounting::SharedSpaceBitmap* live_bitmap_;
  accounting::SharedSpaceBitmap* mark_bitmap_;
};//SharedContinuousSpace


}//namespace gc
}//namespace art

#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SHARED_CONTINUOUS_SPACE_H_ */
