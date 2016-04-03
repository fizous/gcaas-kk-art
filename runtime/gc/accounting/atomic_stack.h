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

#include "cutils/atomic.h"
#include "atomic_integer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "UniquePtr.h"
#include "mem_map.h"
#include "utils.h"


#ifndef ATOMIC_STACK_KLASS
  #if (ART_GC_SERVICE)
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



#if (ART_GC_SERVICE)

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
class ServerStructuredAtomicStack;

template <typename T>
class StructuredAtomicStack {
 public:
  // Capacity is how many elements we can store in the stack.
  static StructuredAtomicStack* Create(const std::string& name,
      size_t capacity, bool shareMem) {
    UniquePtr<StructuredAtomicStack> mark_stack(new StructuredAtomicStack(name,
        capacity, NULL, shareMem));
    mark_stack->Init(shareMem ? 1 : 0);
    return mark_stack.release();
  }


  static StructuredAtomicStack* CreateServerAtomicStack(
      StructuredObjectStackData* memory_data, byte* server_begin) {
    return new ServerStructuredAtomicStack<T>(memory_data, server_begin);
  }

  // Capacity is how many elements we can store in the stack.
  static StructuredAtomicStack* ShareStack(StructuredAtomicStack* original,
      StructuredObjectStackData* memory_data, bool shareMem, size_t high_capacity) {
//    LOG(ERROR) << "....Calling ShareStack...." << memory_data <<
//        ", with high capacity " << high_capacity;
    UniquePtr<StructuredAtomicStack> mark_stack(
        new StructuredAtomicStack(std::string(original->stack_data_->name_),
            high_capacity, memory_data, shareMem));
    mark_stack->Init(shareMem);
    if(!original->stack_data_->is_shared_) {
//      LOG(ERROR) << "....Original stack was not shared....";
      free(original->stack_data_);
    }
    return mark_stack.release();
  }

  static bool SwapStacks(StructuredAtomicStack* stackA, StructuredAtomicStack* stackB) {
    if(stackA->stack_data_->is_shared_ && stackA->stack_data_->is_shared_) {
      LOG(ERROR) << "Swapping shared Allocation stacksssss";
          StructuredObjectStackData _temp_data;
          memcpy(&_temp_data, stackA->stack_data_,
              SERVICE_ALLOC_ALIGN_BYTE(StructuredObjectStackData));
          memcpy(stackA->stack_data_, stackB->stack_data_,
              SERVICE_ALLOC_ALIGN_BYTE(StructuredObjectStackData));
          memcpy(stackB->stack_data_, &_temp_data,
              SERVICE_ALLOC_ALIGN_BYTE(StructuredObjectStackData));
          return true;
    }
    return false;


  }

  virtual ~StructuredAtomicStack() {}

  virtual void SetEntryIndex(int32_t ind, const T& value) {
    stack_data_->begin_[ind]= value;
  }


  virtual T GetEntryIndex(int32_t ind) {
    return stack_data_->begin_[ind];
  }

  virtual T* GetBaseAddress(void) const {
    return stack_data_->begin_;
  }


  // Returns false if we overflowed the stack.
  bool AtomicPushBack(const T& value) {
    if (kIsDebugBuild) {
      stack_data_->debug_is_sorted_ = 0;
    }
    int32_t index;
    do {
      index = stack_data_->back_index_;
      if (UNLIKELY(static_cast<size_t>(index) >= stack_data_->capacity_)) {
        // Stack overflow.
        return false;
      }
    } while (android_atomic_cas(index, index + 1, &stack_data_->back_index_) != 0);

    SetEntryIndex(index, value);
    return true;
  }

  virtual void Reset() {
    DCHECK(mem_map_.get() != NULL);
    DCHECK(GetBaseAddress() != NULL);
    android_atomic_acquire_store(0, &(stack_data_->front_index_));
    android_atomic_acquire_store(0, &(stack_data_->back_index_));
//    stack_data_->front_index_ = 0;
//    stack_data_->back_index_ = 0;
    stack_data_->debug_is_sorted_ = 1;
    if(stack_data_->is_shared_ == 1) {
//      LOG(ERROR) << "AAAAA.......Resetting Shared atomic stack......., before the memset call";
      size_t _mem_length =  sizeof(T) * stack_data_->capacity_;
      if(mem_map_.get()!=NULL) {
        byte* _calc_end = mem_map_->End();
        byte* _end = reinterpret_cast<byte*>(stack_data_->begin_) + _mem_length;
        if(_calc_end < _end) {
//          LOG(ERROR) << "...Need to resize the stack located at " <<
//              reinterpret_cast<void*>(stack_data_->begin_) <<
//              ", mem_end = " << reinterpret_cast<void*>(_calc_end) <<
//              ", atomic_pointer = " <<reinterpret_cast<void*>(_end) ;
          MemBaseMap::AshmemResize(&stack_data_->memory_, _mem_length);
          stack_data_->begin_ = reinterpret_cast<T*>(stack_data_->memory_.begin_);
//          LOG(ERROR) << "...Done with to resize the stack located at " <<
//              reinterpret_cast<void*>(GetBaseAddress());
        }
//        LOG(ERROR) << ".......Resetting Shared atomic stack......., memlength:" <<
//            _mem_length << ", end:" <<
//            reinterpret_cast<void*>((reinterpret_cast<byte*>(GetBaseAddress()) + _mem_length));
//        LOG(ERROR) << ", calcEnd:" << reinterpret_cast<void*>(mem_map_->End());
      }
      //memset(reinterpret_cast<void*>(GetBaseAddress()), 0, _mem_length);
      int result = madvise(GetBaseAddress(),
          _mem_length, MADV_DONTNEED);
      if (result == -1) {
        PLOG(WARNING) << "madvise failed in Atomic Stack shared here";
      }
    } else {
//      LOG(ERROR) << ".......Resetting Non Shared atomic stack.......";
      int result = madvise(GetBaseAddress(),
          sizeof(T) * stack_data_->capacity_, MADV_DONTNEED);
      if (result == -1) {
        PLOG(WARNING) << "madvise failed";
      }
    }
  }




  void PushBack(const T& value) {
    if (kIsDebugBuild) {
      stack_data_->debug_is_sorted_ = 0;
    }
    int32_t index = android_atomic_release_load(&(stack_data_->back_index_));
    DCHECK_LT(static_cast<size_t>(index), stack_data_->capacity_);
    //stack_data_->back_index_ = index + 1;

    if(static_cast<size_t>(index) >= stack_data_->capacity_) {
      LOG(FATAL) << "ERROR in pushing back " << index << "; capacity = " <<
          stack_data_->capacity_;
    }

    android_atomic_add(1, &(stack_data_->back_index_));
    SetEntryIndex(index, value);
  }

  T PopBack() {
    DCHECK_GT(stack_data_->back_index_, stack_data_->front_index_);
    // Decrement the back index non atomically.
    //stack_data_->back_index_ = stack_data_->back_index_ - 1;
    android_atomic_add(-1, &(stack_data_->back_index_));
    return GetEntryIndex(stack_data_->back_index_);
  }

  // Take an item from the front of the stack.
  T PopFront() {
    int32_t index = android_atomic_release_load(&(stack_data_->front_index_));
    DCHECK_LT(index, stack_data_->back_index_);
    //stack_data_->front_index_ = stack_data_->front_index_ + 1;
    android_atomic_add(1, &(stack_data_->front_index_));
    return GetEntryIndex(index);
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
    return android_atomic_release_load(&(stack_data_->back_index_)) -
        android_atomic_release_load(&(stack_data_->front_index_));
  }



  T* Begin() const {
    return const_cast<T*>(GetBaseAddress() +
        android_atomic_release_load(&(stack_data_->front_index_)));
  }

  T* End() const {
    return const_cast<T*>(GetBaseAddress() +
        android_atomic_release_load(&(stack_data_->back_index_)));
  }

  size_t Capacity() const {
    return stack_data_->capacity_;
  }

  // Will clear the stack.
  virtual void Resize(size_t new_capacity) {
//    LOG(ERROR) << ".......Resizing atomic stack.......: " <<
//        stack_data_->capacity_ << ", to newCapacity: "<<  new_capacity;

    stack_data_->capacity_ = new_capacity;
if(stack_data_->is_shared_) {
  LOG(FATAL) << ".......Resizing atomic stack WHILE IT IS SHARED.......: " << new_capacity;
}
    //Reset();
    Init(stack_data_->is_shared_);
  }

  void Sort() {
    int32_t start_back_index = android_atomic_release_load(&(stack_data_->back_index_));
    int32_t start_front_index = android_atomic_release_load(&(stack_data_->front_index_));
    std::sort(Begin(), End());
    CHECK_EQ(start_back_index, stack_data_->back_index_);
    CHECK_EQ(start_front_index, stack_data_->front_index_);
    if (kIsDebugBuild) {
      stack_data_->debug_is_sorted_ = 1;
    }
  }

  bool ContainsSorted(const T& value) const {
    DCHECK(stack_data_->debug_is_sorted_ == 1);
    return std::binary_search(Begin(), End(), value);
  }

  bool Contains(const T& value) const {
    return std::find(Begin(), End(), value) != End();
  }
  // Memory mapping of the atomic stack.
  UniquePtr<MEM_MAP> mem_map_;

//  void ReShareAllocStack(bool shareFlag) {
//    if(shareFlag) {
//      Init(shareFlag);
//    }
//  }


  StructuredAtomicStack(StructuredObjectStackData* data_addr) :
          stack_data_(data_addr) {
    mem_map_.reset(NULL);
  }


  void DumpDataEntries(bool dumpEntries){
//    LOG(ERROR) << "~~~~~~~~~~~~~ AtomicStackDump (size:" << Size() << ") ~~~~~~~~~~~~~"
//        << ", data_address: " << reinterpret_cast<void*>(stack_data_)
//        << ", begin: " << GetBaseAddress() ;

    if(dumpEntries && !IsEmpty()) {
      int _index = 0;
      T* limit = End();
      for (T* it = Begin(); it != limit; ++it) {
        T obj = *it;
        LOG(ERROR) << " = entry = " << _index++ << "; addr= " <<
                  reinterpret_cast<void*>(obj);
      }
    }




//    if(Size() > 0) {
//      int _index = 0;
//      T* limit = End();
//      for (T* it = Begin(); it != limit; ++it) {
//        T obj = *it;
//        LOG(ERROR) << " = entry = " << _index++ << "; addr= " <<
//            reinterpret_cast<void*>(obj);
//      }
//    }
//    for(int i = stack_data_->front_index_; i < stack_data_->back_index_; i++) {
//      LOG(ERROR) << " = entry = " << i << "addr= " <<
//          reinterpret_cast<void*>(stack_data_->begin_[i]);
//    }
    LOG(ERROR) << "___________________________________________________________________";
  }


  void VerifyDataEntries(bool dumpEntries, uintptr_t startA, uintptr_t endA,
      uintptr_t startB, uintptr_t endB){
    LOG(ERROR) << "~~~~~~~~~~~~~ AtomicStackDump (size:" << Size() << ") ~~~~~~~~~~~~~"
        << ", data_address: " << reinterpret_cast<void*>(stack_data_)
        << ", begin: " << GetBaseAddress() ;

    if(dumpEntries && !IsEmpty()) {
      int _index = 0;
      T* limit = End();
      for (T* it = Begin(); it != limit; ++it) {
        T obj = *it;
        uintptr_t obj_t = reinterpret_cast<uintptr_t>(obj);

        if(!(obj_t < endA && obj_t >= startA)) {
          if(!(obj_t < endB && obj_t >= startB)) {
            LOG(ERROR) << " not in any space = entry = " << _index++ << "; addr= " <<
                      reinterpret_cast<void*>(obj);
          }
        }

      }
    }
    LOG(ERROR) << "___________________________________________________________________";
  }

  typedef void Callback(T obj, void* arg);


  void OperateOnStack(Callback* visitor, void* args) {

    T* limit = End();
    for (T* it = Begin(); it != limit; ++it) {
      T obj = *it;
      //uintptr_t obj_t = reinterpret_cast<uintptr_t>(obj);
      visitor(obj, args);
    }
  }

  StructuredObjectStackData* stack_data_;

  // Size in number of elements.
  virtual void Init(int shareMem) {

    if(mem_map_.get() != NULL) { // we should unmap first?
//      LOG(ERROR) << "Reinitializing allocation stack to size: " <<
//          stack_data_->capacity_;
      mem_map_.reset(NULL);
//      LOG(ERROR) << "Unmapping the structuredMemMapfirst " <<
//          stack_data_->capacity_;
    } else {
//      LOG(ERROR) << "Rinitializing allocation stack to initial size: " <<
//          stack_data_->capacity_;
    }
    mem_map_.reset(MEM_MAP::CreateStructedMemMap(stack_data_->name_, NULL,
        stack_data_->capacity_ * sizeof(T), PROT_READ | PROT_WRITE,
        (shareMem == 1), &(stack_data_->memory_)));
//    LOG(ERROR) << "..........Created mem_map of the atomic stack.....";
    CHECK(mem_map_.get() != NULL) << "couldn't allocate mark stack";
    byte* addr = mem_map_->Begin();
    CHECK(addr != NULL);
    stack_data_->debug_is_sorted_ = 1;
    stack_data_->begin_ = reinterpret_cast<T*>(addr);
    stack_data_->is_shared_ = shareMem;
    Reset();
  }


 private:


  StructuredAtomicStack(const std::string& name, const size_t capacity,
      StructuredObjectStackData* atomic_stack_addr, bool shareMem) :
        stack_data_(atomic_stack_addr) {
    if(stack_data_ == NULL) {
      stack_data_ =
          reinterpret_cast<StructuredObjectStackData*>(calloc(1,
              SERVICE_ALLOC_ALIGN_BYTE(StructuredObjectStackData)));
    }
    COPY_NAME_TO_STRUCT(stack_data_->name_, name);
    stack_data_->capacity_ = capacity;
    stack_data_->is_shared_ = shareMem ? 1 : 0;
    mem_map_.reset(NULL);
  }




  DISALLOW_COPY_AND_ASSIGN(StructuredAtomicStack);
};

typedef StructuredAtomicStack<mirror::Object*> StructuredObjectStack;
typedef StructuredAtomicStack<android::MappedPairProcessFD*> StructuredMappedPairStack;

template <typename T>
class ServerStructuredAtomicStack : public StructuredObjectStack {
 public:
  ServerStructuredAtomicStack(StructuredObjectStackData* data_addr,
      byte* serv_begin) : StructuredObjectStack(data_addr) {
    stack_data_->memory_.server_begin_ = serv_begin;
//    LOG(ERROR) << "server..begin = " <<
//        reinterpret_cast<void*>(stack_data_->memory_.server_begin_);
    Init(1);
  }

  // Will clear the stack.
  void Resize(size_t new_capacity) {
//    LOG(ERROR) << "ServerStructuredAtomicStack::.......Resizing atomic stack.......: " <<
//        stack_data_->capacity_ << ", to newCapacity: "<<  new_capacity;

    stack_data_->capacity_ = new_capacity;

    Reset();
    //Init(stack_data_->is_shared_);
  }

  void SetEntryIndex(int32_t ind, const T& value) {
    stack_data_->server_begin_[ind]= value;
  }


  T GetEntryIndex(int32_t ind) {
    return stack_data_->server_begin_[ind];
  }

  T RemoveEntryIndex(int32_t ind) {
    T _value = stack_data_->server_begin_[ind];

    return stack_data_->server_begin_[ind];
  }


  T* GetBaseAddress(void) const {
    return stack_data_->server_begin_;
  }



  // Size in number of elements.
  void Init(int shareMem) {

    if(mem_map_.get() != NULL) { // we should unmap first?
//      LOG(ERROR) << "Reinitializing allocation stack to size: " <<
//          stack_data_->capacity_;
      mem_map_.reset(NULL);
//      LOG(ERROR) << "Unmapping the structuredMemMapfirst " <<
//          stack_data_->capacity_;
    } else {
//      LOG(ERROR) << "ServerStructuredAtomicStack::Rinitializing allocation stack to initial size: " <<
//          stack_data_->capacity_;
    }
    mem_map_.reset(MEM_MAP::CreateStructedMemMap(&(stack_data_->memory_)));
    LOG(ERROR) << "ServerStructuredAtomicStack::..........Created mem_map of the atomic stack.....";
    CHECK(mem_map_.get() != NULL) << "couldn't allocate mark stack";
    byte* addr = mem_map_->ServerBegin();
    CHECK(addr != NULL);
    stack_data_->debug_is_sorted_ = 1;
    stack_data_->server_begin_ = reinterpret_cast<T*>(addr);
    stack_data_->is_shared_ = shareMem;
//    LOG(ERROR) << "ServerStructuredAtomicStack::.........begin of the stack_data....." <<
//        reinterpret_cast<void*>(GetBaseAddress()) <<
//        ", and addr=" << reinterpret_cast<void*>(addr);
    //Reset();
  }

  void AdjustIndeces(void){
    android_atomic_acquire_store(0, &(stack_data_->front_index_));
    android_atomic_acquire_store(0, &(stack_data_->back_index_));
  }

  void Reset() {
    DCHECK(mem_map_.get() != NULL);
    DCHECK(GetBaseAddress() != NULL);
    android_atomic_acquire_store(0, &(stack_data_->front_index_));
    android_atomic_acquire_store(0, &(stack_data_->back_index_));
//    stack_data_->front_index_ = 0;
//    stack_data_->back_index_ = 0;
    stack_data_->debug_is_sorted_ = 1;
    if(stack_data_->is_shared_ == 1) {
//      LOG(ERROR) << "ServerStructuredAtomicStack::.......Resetting Shared atomic stack......., before the memset call";
      size_t _mem_length =  sizeof(T) * stack_data_->capacity_;

//        LOG(ERROR) << "ServerStructuredAtomicStack::Resetting Shared atomic stack......., memlength:" <<
//            _mem_length << ", end:" <<
//            reinterpret_cast<void*>((reinterpret_cast<byte*>(GetBaseAddress()) + _mem_length));

      int result = madvise(GetBaseAddress(),
          _mem_length, MADV_DONTNEED);
      if (result == -1) {
        PLOG(WARNING) << "madvise failed in Atomic Stack";
      }
     // memset(reinterpret_cast<void*>(GetBaseAddress()), 0, _mem_length);
    } else {
      PLOG(ERROR) << "shared atomic stack has to be shared failed";
    }
  }


  typedef bool CallBackRemoval(T obj, void* arg);


  void OperateRemovalOnStack(CallBackRemoval* visitor, void* args) {

    //int _back_index = android_atomic_release_load(&(stack_data_->back_index_));
    int _front_index = android_atomic_release_load(&(stack_data_->front_index_));

    int _index = _front_index;
    while(true) {
      int _back_index = android_atomic_release_load(&(stack_data_->back_index_));
      if(_back_index == _index)
        break;
      T obj = GetEntryIndex(_index);
      if(visitor(obj, args)) {//we should remove that element from the stack
        android_atomic_add(-1, &(stack_data_->back_index_));
        _back_index = android_atomic_release_load(&(stack_data_->back_index_));
        for(int _elem = _index; _elem < _back_index; _elem++) {
          SetEntryIndex(_elem, GetEntryIndex(_elem + 1));
        }
      } else {
        _index++;
      }
    }
//    while(_index < _back_index) {
//      T obj = GetEntryIndex(_index);
//      if(visitor(obj, args)) {//we should remove that element from the stack
//        _back_index -= 1;
//        for(int _elem = _index; _elem < _back_index; _elem++) {
//          SetEntryIndex(_elem, GetEntryIndex(_elem + 1));
//        }
//      } else {
//        _index++;
//      }
//    }
//    android_atomic_acquire_store(_front_index, &(stack_data_->front_index_));
//    android_atomic_acquire_store(_back_index, &(stack_data_->back_index_));
  }


  DISALLOW_COPY_AND_ASSIGN(ServerStructuredAtomicStack);



};
typedef ServerStructuredAtomicStack<mirror::Object*> ServerStructuredObjectStack;

#else

template <typename T>
class AtomicStack {
 public:
  // Capacity is how many elements we can store in the stack.
  static AtomicStack* Create(const std::string& name, size_t capacity,
      bool shareMem = false) {
    UniquePtr<AtomicStack> mark_stack(new AtomicStack(name, capacity));
    mark_stack->Init();
    return mark_stack.release();
  }

  virtual ~AtomicStack() {}

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


  virtual T* GetBaseAddress(void) const {
    return begin_;
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
