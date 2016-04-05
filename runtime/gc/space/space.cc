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

#include "base/logging.h"

namespace art {
namespace gc {
namespace space {


#if (ART_GC_SERVICE)
Space::Space(const std::string& name, GcRetentionPolicy gc_retention_policy,
    GCSrvceSpace* memory_alloc) : space_data_(memory_alloc) {
  if(0)
    LOG(ERROR) << "string length = " << name.length() << "size is: " << name.size();

  if(0)
    LOG(ERROR) << "Space::Space --> memory_alloc ? " << (memory_alloc != NULL ? "true" : "False");

  COPY_NAME_TO_STRUCT(space_data_->name_, name);


  if(0)
    LOG(ERROR) << "Done Copying..." << space_data_->name_;
  space_data_->gc_retention_policy_ = gc_retention_policy;
  if(0)
    LOG(ERROR) << "Leaving Space Constructor";
}

ContinuousSpace::ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
                byte* begin, byte* end,
                GCSrvceContinuousSpace* cont_space_data) :
    Space(name, gc_retention_policy, &(cont_space_data->space_header_)),
                                          cont_space_data_(cont_space_data) {
  if(cont_space_data_ == NULL) {
    if(0)
      LOG(ERROR) << "XXXX Continuous space was null XXXXX" ;
    cont_space_data_ =
        reinterpret_cast<GCSrvceContinuousSpace*>(calloc(1,
            SERVICE_ALLOC_ALIGN_BYTE(GCSrvceContinuousSpace)));
  } else {
    if(0)
      LOG(ERROR) << "XXXX Continuous space was not null XXXXX" ;
  }
//  LOG(ERROR) << "XXXX Continuous space was not null XXXXX " << name <<
//      ", begin=" << begin << ", end = " << end;
  cont_space_data_->begin_ = begin;
  cont_space_data_->end_ = end;
}

MemMapSpace::MemMapSpace(const std::string& name, MEM_MAP* mem_map, size_t initial_size,
            GcRetentionPolicy gc_retention_policy, GCSrvceContinuousSpace* cont_space_data)
    : ContinuousSpace(name, gc_retention_policy,
                      mem_map->Begin(), mem_map->Begin() + initial_size,
                      cont_space_data == NULL ?
                          ContinuousSpace::AllocateContSpaceMemory() : cont_space_data),
      mem_map_(mem_map) {
  if(0)
    LOG(ERROR) << "MemMapSpace::MemMapSpace-->done";
}


DiscontinuousSpace::DiscontinuousSpace(const std::string& name,
                                       GcRetentionPolicy gc_retention_policy) :
    Space(name, gc_retention_policy, Space::AllocateSpaceData()),
    live_objects_(new accounting::SpaceSetMap("large live objects")),
    mark_objects_(new accounting::SpaceSetMap("large marked objects")) {
}
#else
Space::Space(const std::string& name, GcRetentionPolicy gc_retention_policy)
    : name_(name), gc_retention_policy_(gc_retention_policy) { }

DiscontinuousSpace::DiscontinuousSpace(const std::string& name,
                                       GcRetentionPolicy gc_retention_policy) :
    Space(name, gc_retention_policy),
    live_objects_(new accounting::SpaceSetMap("large live objects")),
    mark_objects_(new accounting::SpaceSetMap("large marked objects")) {
}
#endif





void Space::Dump(std::ostream& os) const {
  os << GetName() << ":" << GetGcRetentionPolicy();
}

std::ostream& operator<<(std::ostream& os, const Space& space) {
  space.Dump(os);
  return os;
}



}  // namespace space
}  // namespace gc
}  // namespace art
