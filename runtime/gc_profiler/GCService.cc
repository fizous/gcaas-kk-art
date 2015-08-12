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


GCServiceDaemon* GCServiceDaemon::GCServiceD_ = NULL;

GCServiceDaemon::GCServiceDaemon(VMProfiler* profiler) :
    daemonStatus_(GCSERVICE_STARTING),
    daemonThread_(NULL),
    global_lock_(profiler->gc_service_mu_) {
}

bool GCServiceDaemon::IsGCServiceStopped(void) {
  if(GCServiceDaemon::GCServiceD_ == NULL)
    return false;
  return (GCServiceDaemon::GCServiceD_->daemonStatus_ == GCSERVICE_STOPPED);
}

bool GCServiceDaemon::IsGCServiceRunning(void) {
  if(GCServiceDaemon::GCServiceD_ == NULL)
    return false;
  return (GCServiceDaemon::GCServiceD_->daemonStatus_ == GCSERVICE_RUNNING);
}

void GCServiceDaemon::LaunchGCService(void* arg) {
  VMProfiler* mProfiler = reinterpret_cast<VMProfiler*>(arg);
  mProfiler->gc_service_mu_->lock();
  GCServiceDaemon::GCServiceD_ = new GCServiceDaemon(mProfiler);
  CHECK_PTHREAD_CALL(pthread_create,
      (&GCServiceDaemon::GCServiceD_->pthread_, NULL,
      &GCServiceDaemon::RunDaemon, GCServiceDaemon::GCServiceD_),
      "GCService Daemon thread");
  Thread* self = Thread::Current();
  GCMMP_VLOG(INFO) << "XXXXXXXXXX-0 gcservice: process is entering the main loop XXXXXXXXX";
  while(!GCServiceDaemon::IsGCServiceStopped()) {
    GCMMP_VLOG(INFO) << "XXXXXXXXXX-1 gcservice: process is waiting to stop XXXXXXXXX";
    ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
    {
      int _ret = GCServiceDaemon::WaitTimedService(mProfiler->gc_service_mu_, 1000);
      if(_ret!= 0) {
        LOG(ERROR) << "YYYY error on timed out for the launch service YYY: " << _ret;
      } else {
        GCMMP_VLOG(INFO) << "XXXXXXXXXX -2 timed out";
      }
    }
  }
  GCMMP_VLOG(INFO) << "XXXXXXXXXX-3 gcservice: process is broadcasting and unlocking XXXXXXXXX";
  mProfiler->gc_service_mu_->broadcastCond();
  mProfiler->gc_service_mu_->unlock();
  GCMMP_VLOG(INFO) << "XXXXXXXXXX-4 gcservice: process is leaving XXXXXXXXX";
}

int GCServiceDaemon::WaitTimedService(android::SharedProcessMutex* globalLock,
    int64_t timeMS) {
  timespec timeout_ts;
  InitTimeSpec(true, CLOCK_REALTIME, timeMS, 0, &timeout_ts);
  return globalLock->waitTimedConditional(&timeout_ts);
}

void* GCServiceDaemon::RunDaemon(void* arg) {
  GCServiceDaemon* _gcServiceInst = GCServiceDaemon::GCServiceD_;//reinterpret_cast<GCServiceDaemon*>(arg);
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
    _gcServiceInst->global_lock_->lock();
    _gcServiceInst->daemonThread_ = self;
    _gcServiceInst->daemonStatus_ = GCSERVICE_RUNNING;
    _gcServiceInst->global_lock_->setServiceStatus((int)GCSERVICE_RUNNING);
    _gcServiceInst->global_lock_->broadcastCond();
    _gcServiceInst->global_lock_->unlock();
  }

  GCMMP_VLOG(INFO) << "GCServiceD locks to enter the loop: " << self->GetTid();
  _gcServiceInst->global_lock_->lock();
  while(_gcServiceInst->daemonStatus_ == GCSERVICE_RUNNING) {
    GCMMP_VLOG(INFO) << "GCServiceD loop-0: " << self->GetTid();
    ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
    {
      int _ret = GCServiceDaemon::WaitTimedService(_gcServiceInst->global_lock_, 1000);
      if(_ret != 0) {
        LOG(ERROR) << "GCServiceD: ERROR in timed Wait";
      }
      //_gcServiceInst->global_lock_->waitConditional();
    }
    GCMMP_VLOG(INFO) << "GCServiceD loop-1: " << self->GetTid()<<
                        ", instance counter = " <<
                        _gcServiceInst->global_lock_->getInstanceCounter();
    _gcServiceInst->global_lock_->broadcastCond();
    _gcServiceInst->global_lock_->unlock();
    GCMMP_VLOG(INFO) << "GCServiceD loop-2: " << self->GetTid();
    _gcServiceInst->global_lock_->lock();
  }
  GCMMP_VLOG(INFO) << "GCServiceD 3: the gcservice is leaving the loop";
  _gcServiceInst->global_lock_->unlock();
  _gcServiceInst->global_lock_->broadcastCond();

  GCMMP_VLOG(INFO) << "GCServiceD: the gcservice unlocked the globallocks";

  return NULL;
}


void GCServiceDaemon::shutdownGCService(void) {
  Thread* self = Thread::Current();
  GCMMP_VLOG(INFO) << "gcservice: start signaling Shutting down the GCservice "
      << self->GetTid();
  global_lock_->lock();
  daemonStatus_ = GCSERVICE_STOPPED;
  global_lock_->setServiceStatus((int)daemonStatus_);
  global_lock_->unlock();
  global_lock_->broadcastCond();

  GCMMP_VLOG(INFO) <<
      "gcservice: Joining Shutting down the GCservice"  <<
      self->GetTid();

  CHECK_PTHREAD_CALL(pthread_join, (pthread_, NULL), "GC service shutdown");

  GCMMP_VLOG(INFO) <<
      "gcservice: done with signaling Shutting down the GCservice"  <<
      self->GetTid();
}

void GCServiceDaemon::ShutdownGCService(void) {
  if(GCServiceDaemon::GCServiceD_ != NULL) {
    GCServiceDaemon::GCServiceD_->shutdownGCService();
  }
}

}//namespace mprofiler
}//namespace art




