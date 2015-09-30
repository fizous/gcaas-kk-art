/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_ACCOUNTING_ATOMIC_STACK_H_
#define ART_RUNTIME_GC_ACCOUNTING_ATOMIC_STACK_H_

#include <string>

#include "atomic_integer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "UniquePtr.h"
#include "mem_map.h"
#include "utils.h"


#ifndef ATOMIC_STACK_KLASS
  #if (true || ART_GC_SERVICE)
    #define ATOMIC_STACK_KLASS    StructuredAtomicStack
    #define ATOMIC_OBJ_STACK_T    StructuredObjectStack
    #define ATOMIC_MAPPED_STACK_T StructuredMappedPairStack
  #else
    #define ATOMIC_STACK_KLASS    AtomicStack
    #define ATOMIC_OBJ_STACK_T    ObjectStack
  #endif
#endif


namespace art {
namespace gc {
namespace accounting {



#if (true || ART_GC_SERVICE)

//template <class T>
//struct AtomicStackData {
//  char name_[64];
//
//  // Memory mapping of the atomic stack.
//  AShmemMap memory_;
//
//  // Back index (index after the last element pushed).
//  volatile int back_index_;
//
//  // Front index, used for implementing PopFront.
//  volatile int front_index_;
//
//  // Base of the atomic stack.
//  T* begin_;
//
//  // Maximum number of elements.
//  size_t capacity_;
//
//  // Whether or not the stack is sorted, only updated in debug mode to avoid performance overhead.
//  bool debug_is_sorted_;
//
//  bool is_shared_;
//}__attribute__((aligned(8)));
//
//typedef AtomicStackData<mirror::Object*> StructuredObjectStackData;

template <typename T>
class StructuredAtomicStack {
 public:
  // Capacity is how many elements we can store in the stack.
  static StructuredAtomicStack* Create(const std::string& name,
      size_t capacity, bool shareMem) {
    UniquePtr<StructuredAtomicStack> mark_stack(new StructuredAtomicStack(name, capacity, shareMem));
    mark_stack->Init(shareMem);
    return mark_stack.release();
  }

  // Capacity is how many elements we can store in the stack.
  static StructuredAtomicStack* ShareStack(StructuredAtomicStack* original,
      StructuredObjectStackData* memory_data, bool shareMem) {
    UniquePtr<StructuredAtomicStack> mark_stack(
        new StructuredAtomicStack(std::string(original->stack_data_->name_),
            original->stack_data_->capacity_, shareMem));
    mark_stack->Init(shareMem);
    if(!original->stack_data_->is_shared_) {
      free(original->stack_data_);
    }
    return mark_stack.release();
  }

  ~StructuredAtomicStack() {}

  // Returns false if we overflowed the stack.
  bool AtomicPushBack(const T& value) {
    if (kIsDebugBuild) {
      stack_data_->debug_is_sorted_ = false;
    }
    int32_t index;
    do {
      index = stack_data_->back_index_;
      if (UNLIKELY(static_cast<size_t>(index) >= stack_data_->capacity_)) {
        // Stack overflow.
        return false;
      }
    } while (android_atomic_cas(index, index + 1, &stack_data_->back_index_) != 0);

    stack_data_->begin_[index] = value;
    return true;
  }

  void Reset() {
    DCHECK(mem_map_.get() != NULL);
    DCHECK(stack_data_->begin_ != NULL);
    stack_data_->front_index_ = 0;
    stack_data_->back_index_ = 0;
    stack_data_->debug_is_sorted_ = true;
    if(stack_data_->is_shared_) {
      size_t _mem_length =  sizeof(T) * stack_data_->capacity_;
      memset(stack_data_->begin_, 0, _mem_length);
    } else {
      int result = madvise(stack_data_->begin_,
          sizeof(T) * stack_data_->capacity_, MADV_DONTNEED);
      if (result == -1) {
        PLOG(WARNING) << "madvise failed";
      }
    }
  }

  void PushBack(const T& value) {
    if (kIsDebugBuild) {
      stack_data_->debug_is_sorted_ = false;
    }
    int32_t index = stack_data_->back_index_;
    DCHECK_LT(static_cast<size_t>(index), stack_data_->capacity_);
    stack_data_->back_index_ = index + 1;
    stack_data_->begin_[index] = value;
  }

  T PopBack() {
    DCHECK_GT(stack_data_->back_index_, stack_data_->front_index_);
    // Decrement the back index non atomically.
    stack_data_->back_index_ = stack_data_->back_index_ - 1;
    return stack_data_->begin_[stack_data_->back_index_];
  }

  // Take an item from the front of the stack.
  T PopFront() {
    int32_t index = stack_data_->front_index_;
    DCHECK_LT(index, stack_data_->back_index_);
    stack_data_->front_index_ = stack_data_->front_index_ + 1;
    return stack_data_->begin_[index];
  }

  // Pop a number of elements.
  void PopBackCount(int32_t n) {
    DCHECK_GE(Size(), static_cast<size_t>(n));
    android_atomic_add(-n, &(stack_data_->back_index_));
  }

  bool IsEmpty() const {
    return Size() == 0;
  }

  size_t Size() const {
    DCHECK_LE(stack_data_->front_index_, stack_data_->back_index_);
    return stack_data_->back_index_ - stack_data_->front_index_;
  }

  T* Begin() const {
    return const_cast<T*>(stack_data_->begin_ + stack_data_->front_index_);
  }

  T* End() const {
    return const_cast<T*>(stack_data_->begin_ + stack_data_->back_index_);
  }

  size_t Capacity() const {
    return stack_data_->capacity_;
  }

  // Will clear the stack.
  void Resize(size_t new_capacity) {
    stack_data_->capacity_ = new_capacity;
    Init(stack_data_->is_shared_);
  }

  void Sort() {
    int32_t start_back_index = stack_data_->back_index_;
    int32_t start_front_index = stack_data_->front_index_;
    std::sort(Begin(), End());
    CHECK_EQ(start_back_index, stack_data_->back_index_);
    CHECK_EQ(start_front_index, stack_data_->front_index_);
    if (kIsDebugBuild) {
      stack_data_->debug_is_sorted_ = true;
    }
  }

  bool ContainsSorted(const T& value) const {
    DCHECK(stack_data_->debug_is_sorted_);
    return std::binary_search(Begin(), End(), value);
  }

  bool Contains(const T& value) const {
    return std::find(Begin(), End(), value) != End();
  }
  // Memory mapping of the atomic stack.
  UniquePtr<MEM_MAP> mem_map_;

  void ReShareAllocStack(bool shareFlag) {
    if(shareFlag) {
      Init(shareFlag);
    }
  }
 private:
  // Size in number of elements.
  void Init(bool shareMem) {

    if(mem_map_.get() != NULL) { // we should unmap first?
      LOG(ERROR) << "Reinitializing allocation stack to size: " <<
          stack_data_->capacity_;
      mem_map_.reset(NULL);
      LOG(ERROR) << "Unmapping the structuredMemMapfirst " <<
          stack_data_->capacity_;
    } else {
      LOG(ERROR) << "Rinitializing allocation stack to initial size: " <<
          stack_data_->capacity_;
    }
    mem_map_.reset(MEM_MAP::CreateStructedMemMap(stack_data_->name_, NULL,
        stack_data_->capacity_ * sizeof(T), PROT_READ | PROT_WRITE, shareMem,
        &(stack_data_->memory_)));
    CHECK(mem_map_.get() != NULL) << "couldn't allocate mark stack";
    byte* addr = mem_map_->Begin();
    CHECK(addr != NULL);
    stack_data_->debug_is_sorted_ = true;
    stack_data_->begin_ = reinterpret_cast<T*>(addr);
    stack_data_->is_shared_ = shareMem;
    Reset();
  }

  StructuredAtomicStack(const std::string& name, const size_t capacity,
      bool shareMem) {
    stack_data_ =
        reinterpret_cast<StructuredObjectStackData*>(calloc(1,
            SERVICE_ALLOC_ALIGN_BYTE(StructuredObjectStackData)));
    memcpy(stack_data_->name_, name.c_str(), name.size());
    stack_data_->name_[name.size()] = '\0';
    stack_data_->capacity_ = capacity;
    mem_map_.reset(NULL);
  }
  StructuredObjectStackData* stack_data_;

  DISALLOW_COPY_AND_ASSIGN(StructuredAtomicStack);
};

typedef StructuredAtomicStack<mirror::Object*> StructuredObjectStack;
typedef StructuredAtomicStack<android::MappedPairProcessFD*> StructuredMappedPairStack;

#else

template <typename T>
class AtomicStack {
 public:
  // Capacity is how many elements we can store in the stack.
  static AtomicStack* Create(const std::string& name, size_t capacity) {
    UniquePtr<AtomicStack> mark_stack(new AtomicStack(name, capacity));
    mark_stack->Init();
    return mark_stack.release();
  }

  ~AtomicStack() {}

  void Reset() {
    DCHECK(mem_map_.get() != NULL);
    DCHECK(begin_ != NULL);
    front_index_ = 0;
    back_index_ = 0;
    debug_is_sorted_ = true;
    int result = madvise(begin_, sizeof(T) * capacity_, MADV_DONTNEED);
    if (result == -1) {
      PLOG(WARNING) << "madvise failed";
    }
  }

  // Beware: Mixing atomic pushes and atomic pops will cause ABA problem.

  // Returns false if we overflowed the stack.
  bool AtomicPushBack(const T& value) {
    if (kIsDebugBuild) {
      debug_is_sorted_ = false;
    }
    int32_t index;
    do {
      index = back_index_;
      if (UNLIKELY(static_cast<size_t>(index) >= capacity_)) {
        // Stack overflow.
        return false;
      }
    } while (!back_index_.compare_and_swap(index, index + 1));
    begin_[index] = value;
    return true;
  }

  void PushBack(const T& value) {
    if (kIsDebugBuild) {
      debug_is_sorted_ = false;
    }
    int32_t index = back_index_;
    DCHECK_LT(static_cast<size_t>(index), capacity_);
    back_index_ = index + 1;
    begin_[index] = value;
  }

  T PopBack() {
    DCHECK_GT(back_index_, front_index_);
    // Decrement the back index non atomically.
    back_index_ = back_index_ - 1;
    return begin_[back_index_];
  }

  // Take an item from the front of the stack.
  T PopFront() {
    int32_t index = front_index_;
    DCHECK_LT(index, back_index_.load());
    front_index_ = front_index_ + 1;
    return begin_[index];
  }

  // Pop a number of elements.
  void PopBackCount(int32_t n) {
    DCHECK_GE(Size(), static_cast<size_t>(n));
    back_index_.fetch_sub(n);
  }

  bool IsEmpty() const {
    return Size() == 0;
  }

  size_t Size() const {
    DCHECK_LE(front_index_, back_index_);
    return back_index_ - front_index_;
  }

  T* Begin() const {
    return const_cast<T*>(begin_ + front_index_);
  }

  T* End() const {
    return const_cast<T*>(begin_ + back_index_);
  }

  size_t Capacity() const {
    return capacity_;
  }

  // Will clear the stack.
  void Resize(size_t new_capacity) {
    capacity_ = new_capacity;
    Init();
  }

  void Sort() {
    int32_t start_back_index = back_index_.load();
    int32_t start_front_index = front_index_.load();
    std::sort(Begin(), End());
    CHECK_EQ(start_back_index, back_index_.load());
    CHECK_EQ(start_front_index, front_index_.load());
    if (kIsDebugBuild) {
      debug_is_sorted_ = true;
    }
  }

  bool ContainsSorted(const T& value) const {
    DCHECK(debug_is_sorted_);
    return std::binary_search(Begin(), End(), value);
  }

  bool Contains(const T& value) const {
    return std::find(Begin(), End(), value) != End();
  }

 private:
  AtomicStack(const std::string& name, const size_t capacity)
      : name_(name),
        back_index_(0),
        front_index_(0),
        begin_(NULL),
        capacity_(capacity),
        debug_is_sorted_(true) {
  }

  // Size in number of elements.
  void Init() {
    mem_map_.reset(MEM_MAP::MapAnonymous(name_.c_str(), NULL, capacity_ * sizeof(T), PROT_READ | PROT_WRITE));
    CHECK(mem_map_.get() != NULL) << "couldn't allocate mark stack";
    byte* addr = mem_map_->Begin();
    CHECK(addr != NULL);
    debug_is_sorted_ = true;
    begin_ = reinterpret_cast<T*>(addr);
    Reset();
  }

  // Name of the mark stack.
  std::string name_;

  // Memory mapping of the atomic stack.
  UniquePtr<MEM_MAP> mem_map_;

  // Back index (index after the last element pushed).
  AtomicInteger back_index_;

  // Front index, used for implementing PopFront.
  AtomicInteger front_index_;

  // Base of the atomic stack.
  T* begin_;

  // Maximum number of elements.
  size_t capacity_;

  // Whether or not the stack is sorted, only updated in debug mode to avoid performance overhead.
  bool debug_is_sorted_;

  DISALLOW_COPY_AND_ASSIGN(AtomicStack);
};

typedef AtomicStack<mirror::Object*> ObjectStack;
#endif





}  // namespace accounting
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ACCOUNTING_ATOMIC_STACK_H_
