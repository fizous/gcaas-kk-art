/*
 * gcservice.cc
 *
 *  Created on: Aug 30, 2015
 *      Author: hussein
 */


#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"
#include "runtime.h"
#include "gc/heap.h"
#include "gc/collector/gc_type.h"
#include "gc/gcservice/common.h"
#include "gc/gcservice/gcservice.h"
#include "gc/gcservice/gcservice_daemon.h"
#include "gc/gcservice/gcservice.h"
#include "gc/gcservice/service_client.h"
#include "gc/space/space.h"
#include "gc/collector/gc_type.h"

namespace art {
namespace gcservice {


GCService* GCService::service_ = NULL;
volatile int GCService::zygoteHeapInitialized = 0;
gc::space::Space* GCService::zygote_space_ = NULL;

bool GCService::InitService(void) {
  if(!service_) {
    service_ = new GCService();
    return false;
  }
  return true;
}

GCService::GCService(void) :
     allocator_(NULL), service_meta_data_(NULL), process_(NULL)  {

  allocator_ = ServiceAllocator::CreateServiceAllocator();
  initServiceMetaData(allocator_->GetGCServiceMeta());
}

void GCService::GCPBlockForServiceReady(void) {
  Thread* self = Thread::Current();
  GCSERV_ZYGOTE_ILOG << self->GetTid() << " :locking to wait for service to start";
  IterProcMutexLock interProcMu(self, *service_->_Mu());
  while(service_->_Status() < GCSERVICE_STATUS_WAITINGSERVER) {
    GCSERV_ZYGOTE_ILOG << self->GetTid() << " : going to wait for service to start";
    ScopedThreadStateChange tsc(self, kWaitingForGCService);
    {
      service_->_Cond()->Wait(self);
    }
  }
  service_->_Cond()->Broadcast(self);

  GCSERV_ZYGOTE_ILOG << self->GetTid() <<
      " : done with blocking until service completion";
}

void GCService::initServiceMetaData(GCServiceMetaData* metaData) {
  Thread* self = Thread::Current();
  GCSERV_ZYGOTE_ILOG << self->GetTid() <<
      " Start Initializing GCDaemonMetaData ";
  metaData->status_  = GCSERVICE_STATUS_NONE;
  metaData->counter_ = 0;
  SharedFutexData* _futexAddress = &metaData->lock_header_.futex_head_;
  SharedConditionVarData* _condAddress = &metaData->lock_header_.cond_var_;
  metaData->mu_   = new InterProcessMutex("GCServiceD Mutex", _futexAddress);
  metaData->cond_ = new InterProcessConditionVariable("GCServiceD CondVar",
      *metaData->mu_, _condAddress);
  service_meta_data_ = metaData;

  GCSERV_ZYGOTE_ILOG << self->GetTid() <<
      " Done Initializing GCServiceMetaData ";

}

void GCService::GCPRegisterWithGCService(bool blockGCService) {
  if(service_ == NULL)
    return;
  Thread* self = Thread::Current();
  GCSERV_CLIENT_ILOG << " +++Registering for GCService+++ " << self->GetTid() <<
      (blockGCService? "; GCService blocked" : ";GCService is not server");
  GCServiceClient::InitClient(blockGCService);
  GCSERV_CLIENT_ILOG << " +++Done registering for GCService+++ " <<
      self->GetTid();
}



bool GCService::IsProcessRegistered() {
  return GCServiceClient::service_client_ != NULL;
}
void GCService::GCPFinalizeHeapAfterInit(void) {
  if(service_ == NULL)
    return;
  Thread* self = Thread::Current();
  GCSERV_CLIENT_ILOG << " +++Registering for GCService+++ " << self->GetTid();
  GCServiceClient::FinalizeInitClient();
  GCSERV_CLIENT_ILOG << " +++Done registering for GCService+++ " << self->GetTid();
}

void GCService::launchProcess(void) {
  process_ = GCServiceProcess::InitGCServiceProcess(service_meta_data_);
}


gc::collector::GcType GCService::FilterCollectionType(gc::collector::GcType gcType) {
  if(false && GCServiceClient::service_client_ != NULL) {
    if(gcType == gc::collector::kGcTypeFull) {
      return gc::collector::kGcTypePartial;
    }
  }
//  if(GCService::zygoteHeapInitialized == 1) {
//    if(gcType == gc::collector::kGcTypeFull) {
//      return gc::collector::kGcTypePartial;
//    }
//  }
  return gcType;
}

bool GCService::SetZygoteSpaceProtection(void) {
//  if(tr) {
    return true;
//  }
//  return false;
}

gc::space::GcRetentionPolicy GCService::GetZygoteRetentionPolicy(gc::space::GcRetentionPolicy policy) {
//  if(GCService::zygoteHeapInitialized) {
//    return kZygotePolicy;
//  }
  return policy;
}


void GCService::PreZygoteFork(void) {
  Runtime* runtime = Runtime::Current();
  Thread* self = Thread::Current();
  static Mutex zygote_creation_lock_("zygote creation lock", kZygoteCreationLock);
  if(!runtime->IsZygote()) {
    GCSERV_ZYGOTE_ILOG <<
        " GCService::preZygoteFork --- The runtime is not a zygote ";
    return;
  }
  int collectionType = 0;
  gc::Heap* _heap = runtime->GetHeap();
  {
    MutexLock mu(self, zygote_creation_lock_);
    if(_heap->isHaveZygoteSpace()) {
      GCSERV_ZYGOTE_ILOG <<
          " GCService::preZygoteFork --- heap already have zygote space ";
      collectionType = 1;
    } else {
      GCSERV_ZYGOTE_ILOG <<
          " GCService::preZygoteFork --- first time fork() ";
      collectionType = 2;
    }
  }
  if(collectionType == 1) {
    //collecting heap partially
    GCSERV_ZYGOTE_ILOG <<
        " GCService::preZygoteFork --- collecting partially for fork() ";
    _heap->CollectGarbageForZygoteFork(true);
    return;
  }
  GCSERV_ZYGOTE_ILOG <<
      " GCService::preZygoteFork --- collecting the whole heap for fork() ";

  _heap->CollectGarbage(true);

  GCSERV_ZYGOTE_ILOG <<
       " GCService::preZygoteFork --- first time fork() ";

  MutexLock mu(self, zygote_creation_lock_);

  _heap->HeapPrepareZygoteSpace(self, false);

  GCService::zygoteHeapInitialized = 1;

}

void GCService::LogImmunedSpaceAllocation() {
  if((zygoteHeapInitialized == 1) && SetZygoteSpaceProtection()) {
    if(zygote_space_ == NULL)
      return;
    GCSERV_SPACE_ILOG << "Alloc in zygote";
  }
}


void GCService::LogImmunedObjectMutation(const void *addr) {
  if((zygoteHeapInitialized == 1) && SetZygoteSpaceProtection()) {
    if(zygote_space_ == NULL)
      return;
    if(zygote_space_->Contains(reinterpret_cast<const mirror::Object*>(addr))) {
      GCSERV_IMMUNE_ILOG << addr;
    }
  }
}

}//namespace gcservice
}//namespace art
