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

namespace art {
namespace gcservice {


GCService* GCService::service_ = NULL;

void GCService::InitService(void) {
  if(!service_) {
    service_ = new GCService();
  }

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

  GCSERV_ZYGOTE_ILOG << self->GetTid() << " : done with blocking until service completion";
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

void GCService::GCPRegisterWithGCService(void) {
  GCSERV_CLIENT_ILOG << "Registering for GCService";
}

void GCService::launchProcess(void) {
  process_ = GCServiceProcess::InitGCServiceProcess(service_meta_data_);
}


void GCService::preZygoteFork(void) {
  Runtime* runtime = Runtime::Current();
  static Mutex zygote_creation_lock_("zygote creation lock", kZygoteCreationLock);
  if(!runtime->IsZygote()) {
    GCSERV_ZYGOTE_ILOG <<
        " GCService::preZygoteFork --- The runtime is not a zygote ";
    return;
  }
  gc::Heap* _heap = runtime->GetHeap();
  if(_heap->isHaveZygoteSpace) {
    GCSERV_ZYGOTE_ILOG <<
        " GCService::preZygoteFork --- heap already have zygote space ";
    //collecting heap partially
    _heap->CollectGarbageForZygoteFork(true);
    return;
  } else {
    GCSERV_ZYGOTE_ILOG <<
        " GCService::preZygoteFork --- heap already have zygote space ";
    _heap->CollectGarbage(true);
  }



}

}//namespace gcservice
}//namespace art
