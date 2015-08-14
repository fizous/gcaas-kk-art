/*
 * GCService.cpp
 *
 *  Created on: Aug 11, 2015
 *      Author: hussein
 */

#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"

#include "gc_profiler/MPPerfCounters.h"
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MProfiler.h"
#include "gc_profiler/MProfilerHeap.h"
#include "gc_profiler/GCService.h"



namespace art {
namespace mprofiler {


GCServiceDaemon* GCServiceDaemon::GCServiceD = NULL;

GCServiceDaemon::GCServiceDaemon(GCDaemonHeader* service_header) :
    service_header_(service_header),
    daemonThread_(NULL) {
  initShutDownSignals();
}

bool GCServiceDaemon::IsGCServiceStopped(void) {
  GCServiceDaemon* _service = GCServiceDaemon::GCServiceD;
  if(_service == NULL)
    return false;
  return _service->isStopped();
}

bool GCServiceDaemon::isStopped(void) {
  return (_Status() == GCSERVICE_STATUS_STOPPED);
}

bool GCServiceDaemon::isShuttingDown(void) {
  return (_Status() == GCSERVICE_STATUS_SHUTTING_DOWN);
}

bool GCServiceDaemon::isRunning(void) {
  return (_Status() == GCSERVICE_STATUS_RUNNING);
}


bool GCServiceDaemon::IsGCServiceRunning(void) {
  GCServiceDaemon* _service = GCServiceDaemon::GCServiceD;
  if(_service == NULL)
    return false;
  return _service->isRunning();
}

void GCServiceDaemon::GCPRegisterGCService(void) {
  Thread* self = Thread::Current();
  GCDaemonHeader* _header = Runtime::Current()->gcserviceHeader_;
  if(_header == NULL) {
    LOG(ERROR) << " ############### Service Header was NULL While Registering #############" << self->GetTid();
    return;
  }
  GCSERV_VLOG(INFO) << self->GetTid() <<
      "-----0 register to GCService -------";
  IterProcMutexLock interProcMu(self, *_header->mu_);
  _header->meta_data_->counter_++;
  GCSERV_VLOG(INFO) << self->GetTid() <<
      "-----1 service counter ------- " << _header->meta_data_->counter_;
  _header->cond_->Broadcast(self);
  GCSERV_VLOG(INFO) << self->GetTid() <<
      "-----3 leaving registration -------";
}

void GCServiceDaemon::LaunchGCService(void* arg) {
  GCSERV_VLOG(ERROR) << " ---------- ART_USE_FUTEX = " <<
      BaseMutex::IsARTUseFutex() << " -------------------";

  GCDaemonHeader* _serviceHeader = reinterpret_cast<GCDaemonHeader*>(arg);
  Thread* self = Thread::Current();
  {
    IterProcMutexLock interProcMu(self, *_serviceHeader->mu_);

    GCServiceDaemon::GCServiceD = new GCServiceDaemon(_serviceHeader);

    CHECK_PTHREAD_CALL(pthread_create,
        (&GCServiceDaemon::GCServiceD->pthread_, NULL,
        &GCServiceDaemon::RunDaemon, GCServiceDaemon::GCServiceD),
        "GCService Daemon thread");
  }

  GCSERV_VLOG(INFO) << "XXXXXXXXXX-0 process is locking shutdown mu XXXXXXXXX";
  MutexLock mu(self, *GCServiceDaemon::GCServiceD->shutdown_mu_);
  while(!GCServiceDaemon::IsGCServiceStopped()) {
    GCSERV_VLOG(INFO) << "XXXXXXXXXX-1 process is waiting to stop XXXXXXXXX";
    ScopedThreadStateChange tsc(self, kWaitingForGCService);
    {
      GCServiceDaemon::GCServiceD->shutdown_cond_->Wait(self);
    }
  }
  GCSERV_VLOG(INFO) << "XXXXXXXXXX-2 process left waiting loop XXXXXXXXX";
}


bool GCServiceDaemon::gcserviceMain(Thread* thread) {
  int _oldCounter = 0;
  IterProcMutexLock interProcMu(thread, *_Mu());
  _oldCounter = _Counter();
  ScopedThreadStateChange tsc(thread, kWaitingForGCService);
  {
    _Cond()->Wait(thread);
  }

  if(isShuttingDown()) {
    _Cond()->Broadcast(thread);
    return false;
  }
  if(_oldCounter != _Counter()) {
      GCSERV_VLOG(INFO) << thread->GetTid() <<
          ":GCServiceD: counterReceived = " <<
          _Counter();
  }
  _Cond()->Broadcast(thread);
  return true;
}

void* GCServiceDaemon::RunDaemon(void* arg) {
  GCServiceDaemon* _gcServiceInst = GCServiceDaemon::GCServiceD;//reinterpret_cast<GCServiceDaemon*>(arg);
  Runtime* runtime = Runtime::Current();
  bool _createThread =  runtime->AttachCurrentThread("GCServiceD", true,
      runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler());
  if(!_createThread) {
    LOG(ERROR) << "-------- could not attach internal GC service Daemon ---------";
    return NULL;
  }
  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    IterProcMutexLock interProcMu(self, *_gcServiceInst->_Mu());
    _gcServiceInst->daemonThread_ = self;
    _gcServiceInst->_Status(GCSERVICE_STATUS_RUNNING);
    _gcServiceInst->_Cond()->Broadcast(self);
  }

  GCSERV_VLOG(INFO) << "GCServiceD is entering the main loop: " << self->GetTid();

  while(_gcServiceInst->isRunning()) {
    if(!_gcServiceInst->gcserviceMain(self))
      break;
  }

  GCSERV_VLOG(INFO) << "GCServiceD left the main loop: " << self->GetTid();
  if(_gcServiceInst->isShuttingDown()) {
    GCSERV_VLOG(INFO) << "GCServiceD: shuttingDown is true: " << self->GetTid();
    MutexLock mu(self, *_gcServiceInst->shutdown_mu_);
    _gcServiceInst->_Status(GCSERVICE_STATUS_STOPPED);
    _gcServiceInst->shutdown_cond_->Broadcast(self);
    GCSERV_VLOG(INFO) << "GCServiceD: updated status to stopped: " << self->GetTid();
  }


  GCSERV_VLOG(INFO) << self->GetTid() << "-GCServiceD: leaving the daemon code" ;

  return NULL;
}


void GCServiceDaemon::shutdown(void) {
  Thread* self = Thread::Current();
  GCSERV_VLOG(INFO) << self->GetTid() << " :start signaling Shutting down the GCservice "
      << self->GetTid();
  {
    IterProcMutexLock interProcMu(self, *_Mu());
    _Status(GCSERVICE_STATUS_SHUTTING_DOWN);
    _Cond()->Broadcast(self);
    GCSERV_VLOG(INFO) << self->GetTid() << " :change status to shutting down";
  }
  CHECK_PTHREAD_CALL(pthread_join, (pthread_, NULL), "GC service shutdown");
  GCSERV_VLOG(INFO) << self->GetTid() << " :joined on pthread";
}

void GCServiceDaemon::ShutdownGCService(void) {
  if(GCServiceDaemon::GCServiceD != NULL) {
    GCServiceDaemon::GCServiceD->shutdown();
  }
}


void GCServiceDaemon::GCPBlockForServiceReady(GCDaemonHeader* dHeader) {
  Thread* self = Thread::Current();
  GCSERV_VLOG(INFO) << self->GetTid() << " :locking to wait for service to start";
  IterProcMutexLock interProcMu(self, *dHeader->mu_);
  while(dHeader->meta_data_->status_ != GCSERVICE_STATUS_RUNNING) {
    GCSERV_VLOG(INFO) << self->GetTid() << " : going to wait for service to start";
    ScopedThreadStateChange tsc(self, kWaitingForGCService);
    {
      dHeader->cond_->Wait(self);
    }
  }
  dHeader->cond_->Broadcast(self);

  GCSERV_VLOG(INFO) << self->GetTid() << " : done with blocking until service completion";
}


GCDaemonHeader* GCServiceDaemon::CreateServiceHeader(void) {
  int fileDescript = 0;

  MemMap* mu_mem_map =
      MemMap::MapSharedMemoryAnonymous("SharedLockRegion", NULL, 1024,
                                                 PROT_READ | PROT_WRITE,
                                                 &fileDescript);
  if (mu_mem_map == NULL) {
    LOG(ERROR) << "Failed to allocate pages for alloc space (" <<
        "SharedLockingRegion" << ") of size "
        << PrettySize(1024);
    return NULL;
  }

  Thread* self = Thread::Current();

  GCDaemonHeader* _header_address =
      reinterpret_cast<GCDaemonHeader*>(mu_mem_map->Begin());
  memset((void*) _header_address, 0, sizeof(GCDaemonHeader));

  GCDaemonMetaData* _meta_data =
      reinterpret_cast<GCDaemonMetaData*>(mu_mem_map->Begin() + sizeof(GCDaemonHeader));

  memset((void*) _meta_data, 0, sizeof(GCDaemonMetaData));

  _header_address->meta_data_ = _meta_data;
  SharedFutexData* _futexAddress =
      &_header_address->meta_data_->lock_header_.futex_head_;
  SharedConditionVarData* _condAddress =
      &_header_address->meta_data_->lock_header_.cond_var_;

  _header_address->mu_ =
      new InterProcessMutex("GCServiceD Mutex", _futexAddress);
  _header_address->cond_ =
      new InterProcessConditionVariable("GCServiceD CondVar",
          *_header_address->mu_, _condAddress);
  GCSERV_VLOG(INFO) << self->GetTid() <<
      " :created the GCDaemonHeader, file descriptor = " << fileDescript;
  return _header_address;
}



/******************** private methods *************************/
void GCServiceDaemon::initShutDownSignals(void) {
  shutdown_mu_ = new Mutex("gcService Shutdown");
  shutdown_cond_.reset(new ConditionVariable("gcService Shutdown condition variable",
      *shutdown_mu_));
}


}//namespace mprofiler
}//namespace art




