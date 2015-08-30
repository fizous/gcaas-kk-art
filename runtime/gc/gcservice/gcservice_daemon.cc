/*
 * gcservice_daemon.cc
 *
 *  Created on: Aug 30, 2015
 *      Author: hussein
 */


#include "gc/gcservice/common.h"
#include "gc/gcservice/gcservice_daemon.h"
#include "gc/gcservice/service_allocator.h"

namespace art {

namespace gcservice {

GCServiceProcess* GCServiceProcess::process_ = NULL;

void* GCServiceDaemon::RunDaemon(void* arg) {
  GCServiceDaemon* _daemonObj = reinterpret_cast<GCServiceDaemon*>(arg);
  GCServiceProcess* _processObj = _daemonObj->process_;
  Runtime* runtime = Runtime::Current();
  bool _createThread =  runtime->AttachCurrentThread("GCSvcDaemon", true,
      runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler());

  if(!_createThread) {
    LOG(ERROR) << "-------- could not attach internal GC service Daemon ---------";
    return NULL;
  }
  Thread* self = Thread::Current();


  DCHECK_NE(self->GetState(), kRunnable);
  {
    IterProcMutexLock interProcMu(self, *_processObj->service_meta_->mu_);
    _daemonObj->thread_ = self;
    _processObj->service_meta_->status_ = GCSERVICE_STATUS_RUNNING;
    _processObj->service_meta_->cond_->Broadcast(self);
  }

  GCSERV_DAEM_ILOG << "GCServiceDaemon is entering the main loop: " <<
      _daemonObj->thread_->GetTid();

  while(_processObj->service_meta_->status_ == GCSERVICE_STATUS_RUNNING) {
    if(!_daemonObj->mainLoop())
      break;
  }

  GCSERV_DAEM_ILOG << "GCServiceDaemon left the main loop: " <<
      _daemonObj->thread_->GetTid();

  return NULL;
}

void GCServiceDaemon::mainLoop(void) {

}

void GCServiceDaemon::initShutDownSignals(void) {
  Thread* self = Thread::Current();
  shutdown_mu_ = new Mutex("gcService Shutdown");
  MutexLock mu(self, *shutdown_mu_);
  shutdown_cond_.reset(new ConditionVariable("gcService Shutdown condition variable",
      *shutdown_mu_));
}

GCServiceDaemon::GCServiceDaemon(GCServiceProcess* process) :
    process_(process), thread_(NULL) {
  Thread* self = Thread::Current();
  {
    IterProcMutexLock interProcMu(self, *process_->service_meta_->mu_);

    initShutDownSignals();

    CHECK_PTHREAD_CALL(pthread_create,
        (&pthread_, NULL,
        &GCServiceDaemon::RunDaemon, this),
        "GCService Daemon thread");
    process_->service_meta_->cond_->Broadcast(self);
  }
}

GCServiceDaemon* GCServiceDaemon::CreateServiceDaemon(GCServiceProcess* process) {
  return new GCServiceDaemon(process);
}


//----------
//----------------------------- GCServiceProcess ------------------------------


void GCServiceProcess::InitGCServiceProcess(GCServiceMetaData* meta) {
  if(process_ != NULL) {
    process_ = new GCServiceProcess(meta);
  }
}

bool GCServiceProcess::initSvcFD(void) {
  bool returnRes = false;
  IterProcMutexLock interProcMu(thread_, *service_meta_->mu_);
  ScopedThreadStateChange tsc(thread_, kWaitingForGCService);
  {
    service_meta_->cond_->Wait(thread_);
  }
  if(fileMapperSvc_ == NULL) {
    GCSERV_PROC_ILOG << " creating fileMapperSvc_ for first time ";
    fileMapperSvc_ =
        android::FileMapperService::CreateFileMapperSvc();
    returnRes = android::FileMapperService::IsServiceReady();
  } else {
    GCSERV_PROC_ILOG << " reconnecting ";
    returnRes = android::FileMapperService::Reconnect();
  }
  service_meta_->cond_->Broadcast(thread_);
  return returnRes;
}

GCServiceProcess::GCServiceProcess(GCServiceMetaData* meta) :
    service_meta_(meta), fileMapperSvc_(NULL), srvcReady_(false) {
  thread_ = Thread::Current();
  srvcReady_ = initSvcFD();
  daemon_ = GCServiceDaemon::CreateServiceDaemon(this);
}

}//namespace gcservice
}//namespace art
