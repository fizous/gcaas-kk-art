/*
 * space_common.h
 *
 *  Created on: Sep 12, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SPACE_SPACE_COMMON_H_
#define ART_RUNTIME_GC_SPACE_SPACE_COMMON_H_

#include "globals.h"
#include "mem_map.h"

namespace art {
namespace gc {
namespace space {

// See Space::GetGcRetentionPolicy.
enum GcRetentionPolicy {
  // Objects are retained forever with this policy for a space.
  kGcRetentionPolicyNeverCollect,
  // Every GC cycle will attempt to collect objects in this space.
  kGcRetentionPolicyAlwaysCollect,
  // Objects will be considered for collection only in "full" GC cycles, ie faster partial
  // collections won't scan these areas such as the Zygote.
  kGcRetentionPolicyFullCollect,
};
std::ostream& operator<<(std::ostream& os, const GcRetentionPolicy& policy);

enum SpaceType {
  kSpaceTypeImageSpace,
  kSpaceTypeAllocSpace,
  kSpaceTypeZygoteSpace,
  kSpaceTypeLargeObjectSpace,
};
std::ostream& operator<<(std::ostream& os, const SpaceType& space_type);


}//namespace space
}//namespace gc
}//namespace art

#endif /* ART_RUNTIME_GC_SPACE_SPACE_COMMON_H_ */
