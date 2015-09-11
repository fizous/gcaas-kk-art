/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "space.h"
#include "gc/gcservice/service_allocator.h"
#include "base/logging.h"

namespace art {
namespace gc {
namespace space {

ContinuousSpace::ContinuousSpace(const std::string& name,
                GcRetentionPolicy gc_retention_policy,
                byte* begin, byte* end,
                ContinuousSpaceMemberMetaData* meta_addr) :
    Space(name, gc_retention_policy), space_meta_data_(meta_addr),
    allocated_memory_(space_meta_data_ == NULL) {
  bool allocated_memory = (space_meta_data_ == NULL);
  if(allocated_memory) {
    space_meta_data_ =
        reinterpret_cast<ContinuousSpaceMemberMetaData*>(calloc(1,
        SERVICE_ALLOC_ALIGN_BYTE(ContinuousSpaceMemberMetaData)));
  }
  SetContSpaceMemberData(space_meta_data_, gc_retention_policy, begin, end, allocated_memory);
}

void ContinuousSpace::SetContSpaceMemberData(ContinuousSpaceMemberMetaData* address,
    GcRetentionPolicy gc_retention_policy, byte* begin, byte* end,
    bool allocated_memory) {
  if(allocated_memory) {
    ContinuousSpaceMemberMetaData _data = {begin, end, gc_retention_policy, {{0,0,0,0,0,0},0,0,0}};
    memcpy(address, &_data,
        SERVICE_ALLOC_ALIGN_BYTE(ContinuousSpaceMemberMetaData));
  } else {
    ContinuousSpaceMemberMetaData _data = {begin, end, gc_retention_policy, address->mem_meta_};
    memcpy(address, &_data,
        SERVICE_ALLOC_ALIGN_BYTE(ContinuousSpaceMemberMetaData));
  }
}


Space::Space(const std::string& name, GcRetentionPolicy gc_retention_policy)
    : name_(name), gc_retention_policy_(gc_retention_policy) { }

void Space::Dump(std::ostream& os) const {
  os << GetName() << ":" << GetGcRetentionPolicy();
}

std::ostream& operator<<(std::ostream& os, const Space& space) {
  space.Dump(os);
  return os;
}


DiscontinuousSpace::DiscontinuousSpace(const std::string& name,
                                       GcRetentionPolicy gc_retention_policy) :
    Space(name, gc_retention_policy),
    live_objects_(new accounting::SpaceSetMap("large live objects")),
    mark_objects_(new accounting::SpaceSetMap("large marked objects")) {
}

}  // namespace space
}  // namespace gc
}  // namespace art
