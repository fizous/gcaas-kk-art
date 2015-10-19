

#include <string>
#include <cutils/ashmem.h>
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "mem_map.h"
#include "gc/service/global_allocator.h"


namespace art {
namespace gc {
namespace gcservice {

ServerCollector::ServerCollector(space::GCSrvSharableHeapData* meta_alloc) :
    heap_data_(meta_alloc),
    run_mu_("ServerLock"),
    run_cond_("ServerLock::cond_", run_mu_),
    thread_(NULL),
    status_(0),
    shake_hand_mu_("shake_hand"),
    shake_hand_cond_("ServerLock::cond_", shake_hand_mu_),
    curr_collector_addr_(NULL) {


  SharedFutexData* _futexAddress = &heap_data_->phase_lock_.futex_head_;
  SharedConditionVarData* _condAddress = &heap_data_->phase_lock_.cond_var_;
  SharedFutexData* _conc_futexAddress = &heap_data_->conc_lock_.futex_head_;
  SharedConditionVarData* _conc_condAddress = &heap_data_->conc_lock_.cond_var_;

  if(true) {
    phase_mu_ = new InterProcessMutex(_futexAddress, "GCServiceD Mutex");
    phase_cond_ =  new InterProcessConditionVariable(*phase_mu_,
        "GCServiceD CondVar", _condAddress);

    conc_req_cond_mu_ = new InterProcessMutex(_conc_futexAddress, "GCConc Mutex");
    conc_req_cond_ = new InterProcessConditionVariable(*conc_req_cond_mu_,
        "GCConc CondVar", _conc_condAddress);


    /* initialize gc complete locks */
    SharedFutexData* _complete_futexAddress = &heap_data_->gc_complete_lock_.futex_head_;
    SharedConditionVarData* _complete_condAddress = &heap_data_->gc_complete_lock_.cond_var_;
    gc_complete_mu_ = new InterProcessMutex(_complete_futexAddress, "GCComplete Mutex");
    gc_complete_cond_ = new InterProcessConditionVariable(
        *gc_complete_mu_, "GCcomplete CondVar", _complete_condAddress );

    CHECK_PTHREAD_CALL(pthread_create,
        (&pthread_, NULL,
        &RunCollectorDaemon, this),
        "Server-Collector");


    Thread* self = Thread::Current();
    MutexLock mu(self, run_mu_);
    LOG(ERROR) << "------------------ServerCollector--->going to wait for initialization of Daemon";
    while (thread_ == NULL) {
      run_cond_.Wait(self);
    }
  }
}


void ServerCollector::SignalCollector(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::SignalCollector..." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    MutexLock mu(self, run_mu_);
    if(thread_ != NULL) {
      status_ = status_ + 1;
      LOG(ERROR) << "ServerCollector::SignalCollector ---- Thread was not null:" << self->GetTid() << "; status=" << status_;
      run_cond_.Broadcast(self);
    } else {
      LOG(ERROR) << "ServerCollector::SignalCollector ---- Thread was  null:" << self->GetTid();
    }
//    if(status_ == 0) {
//      status_ = 1;
//    }
//
  }

  LOG(ERROR) << "ServerCollector::SignalCollector...LEaving: " << self->GetTid();
}

void ServerCollector::WaitForRequest(void) {
    Thread* self = Thread::Current();
//  {
//    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
//    heap_data_->is_gc_running_ = 0;
//    conc_req_cond_->Broadcast(self);
//  }
 // LOG(ERROR) << "ServerCollector::WaitForRequest.." << self->GetTid()  << "status ="   << status_;
  {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      MutexLock mu(self, run_mu_);
      while(status_ <= 0) {
        run_cond_.Wait(self);
      }
      status_ = 0;
      LOG(ERROR) << "ServerCollector::WaitForRequest:: leaving WaitForRequest; status=" << status_;
      run_cond_.Broadcast(self);
    }
  }


}

/*
class ServerMarkReachableTask : public WorkStealingTask {
 public:
  ServerCollector* server_instant_;
  space::GCSrvSharableCollectorData* curr_collector_addr_;

  ServerMarkReachableTask(ServerCollector* server_object) :
    WorkStealingTask(), server_instant_(server_object),
    curr_collector_addr_(NULL) {
    LOG(ERROR) << "creating worker stealing task listener";
  }
  void StealFrom(Thread* self, WorkStealingTask* source) {
    source->Run(self);
  }

  void WaitForPhaseAddress(Thread* self) {
    LOG(ERROR) << " ++++ Phase going for current addreess  ++++ " << self->GetTid();
    server_instant_->BlockOnCollectorAddress(self);
    curr_collector_addr_ = server_instant_->heap_data_->current_collector_;
    LOG(ERROR) << " ++++ Phase done waiting for current addreess  ++++ "
        << self->GetTid() << "; address " << reinterpret_cast<void*>(curr_collector_addr_);;
  }

  void WaitForReachablePhaseAddress(Thread* self) {
    LOG(ERROR) << " ++++ task waiting for the reachable phase ++++ " << self->GetTid();
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->phase_mu_));
      while(true) {
        if(curr_collector_addr_ != NULL) {
          if(curr_collector_addr_->gc_phase_ == space::IPC_GC_PHASE_MARK_REACHABLES) {
            break;
          }
        }
        server_instant_->phase_cond_->Wait(self);
      }
      LOG(ERROR) << " ++++ Phase TASK noticed change  ++++ " << self->GetTid();
    }
  }

  void ExecuteReachableMarking(Thread* self) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->phase_mu_));
      LOG(ERROR) << " ++++ pre Phase TASK updated the phase of the GC: "
          << self->GetTid() << curr_collector_addr_->gc_phase_;
      curr_collector_addr_->gc_phase_ = space::IPC_GC_PHASE_MARK_RECURSIVE;
      LOG(ERROR) << " ++++ post Phase TASK updated the phase of the GC: "
          << self->GetTid() << ", phase:" << curr_collector_addr_->gc_phase_;
      server_instant_->phase_cond_->Broadcast(self);

    }
  }
  // Scans all of the objects
  virtual void Run(Thread* self) {
    WaitForPhaseAddress(self);


    LOG(ERROR) << "@@@@@@@@@@@@@@@@ We ran mark reachables task @@@@@@@@@@@@@@@@@@@ "
        << self->GetTid();
    WaitForReachablePhaseAddress(self);
    ExecuteReachableMarking(self);
  }

  virtual void Finalize() {
    LOG(ERROR) << "@@@@@@@@@@@@@@@@Finalize@@@@@@@@@@@@";
    delete this;
  }
};*/


class ServerIPCListenerTask : public WorkStealingTask {
 public:
  ServerCollector* server_instant_;
  space::GCSrvSharableCollectorData* curr_collector_addr_;
  volatile int* collector_index_;

  ServerIPCListenerTask(ServerCollector* server_object) : WorkStealingTask() ,
    server_instant_(server_object),
    curr_collector_addr_(NULL),
    collector_index_(NULL) {
    collector_index_ = &(server_instant_->heap_data_->collect_index_);
  }
  void StealFrom(Thread* self, WorkStealingTask* source) {
    source->Run(self);
  }


  void SetCurrentCollector(Thread* self) {
    curr_collector_addr_ = server_instant_->heap_data_->current_collector_;
//    *collector_index_ = server_instant_->heap_data_->collect_index_;
    LOG(ERROR) << " server: creating worker stealing task ServerIPCListenerTask: " <<
        "index = " << server_instant_->heap_data_->collect_index_ <<
        "\n    address = " << reinterpret_cast<void*>(curr_collector_addr_);

    server_instant_->UpdateCollectorAddress(self, curr_collector_addr_);

  }

  void WaitForCollector(Thread* self) {
    LOG(ERROR) << "@@@@ going to wait for collector @@@" << self->GetTid();
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->conc_req_cond_mu_));
      while(true) {
        if(server_instant_->heap_data_->conc_flag_ == 2) {
          if(*collector_index_ != -1) {
            SetCurrentCollector(self);
            LOG(ERROR) << "~~~~~~~~~~~~~~~ WaitForCollector is leaving ~~~~~~~" << self->GetTid();
            break;
          }
        }
        server_instant_->conc_req_cond_->Wait(self);
      }
    }
  }

  // Scans all of the objects
  virtual void Run(Thread* self) {
    LOG(ERROR) << "@@@@@@@@@@@@@@@@ Run Wait completion task @@@@@@@@@@@@@@@@@@@ " << self->GetTid();
    WaitForCollector(self);
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->conc_req_cond_mu_));
      LOG(ERROR) << " server: RUN IPC LISTERNERS: server_instant_->heap_data_->collect_index_: "
          <<  server_instant_->heap_data_->collect_index_ << ", tid:" << self->GetTid();
      while(server_instant_->heap_data_->conc_flag_ != 3) {
        server_instant_->conc_req_cond_->Wait(self);
        LOG(ERROR) << "@@ServerCollector::WaitForGCTask.. " << self->GetTid() <<
            ", setting conc flag to " << server_instant_->heap_data_->conc_flag_;
      }
      LOG(ERROR) << "@@ServerCollector::WaitForGCTask.. " << self->GetTid() <<
          ", leaving while flag " << server_instant_->heap_data_->conc_flag_;
      server_instant_->heap_data_->conc_flag_ = 0;
      server_instant_->conc_req_cond_->Broadcast(self);
    }
  }
  virtual void Finalize() {
    LOG(ERROR) << "@@@@@@@@@@@@@@@@ Finalize ServerIPCListenerTask @@@@@@@@@@@@";
    delete this;
  }
};


void ServerCollector::UpdateCollectorAddress(Thread* self,
    space::GCSrvSharableCollectorData* address) {
  MutexLock mu(self, shake_hand_mu_);
  curr_collector_addr_ = address;
  LOG(ERROR) << "ServerCollector::UpdateCollectorAddress " <<  self->GetTid()
      << ", address: " << reinterpret_cast<void*>(address);
  shake_hand_cond_.Broadcast(self);
}

void ServerCollector::BlockOnCollectorAddress(Thread* self) {
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    MutexLock mu(self, shake_hand_mu_);
    while(curr_collector_addr_ == NULL) {
      shake_hand_cond_.Wait(self);
    }
  }
}

void ServerCollector::ExecuteGC(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "-----------------ServerCollector::ExecuteGC-------------------" << self->GetTid();
  {
    MutexLock mu(self, shake_hand_mu_);
    curr_collector_addr_ = NULL;
  }

  gc_workers_pool_->AddTask(self, new ServerIPCListenerTask(this));
  //gc_workers_pool_->AddTask(self, new ServerMarkReachableTask(this));
  gc_workers_pool_->SetMaxActiveWorkers(2);
  //gc_workers_pool_->AddTask(self, reachable_task);
  LOG(ERROR) << "@@@@@@@ Thread Pool starting the tasks " << self->GetTid();
  gc_workers_pool_->StartWorkers(self);
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);

    LOG(ERROR) << "ServerCollector::ExecuteGC: set concurrent flag";
    heap_data_->conc_flag_ = 1;
    conc_req_cond_->Broadcast(self);
    LOG(ERROR) << "ServerCollector::ExecuteGC.. " << self->GetTid() <<
              ", setting conc flag to " << heap_data_->conc_flag_;
  }
  gc_workers_pool_->Wait(self, true, true);
  LOG(ERROR) << "@@@@@@@ Thread Pool LEaving the Wait Call @@@@@";

  gc_workers_pool_->StopWorkers(self);
  LOG(ERROR) << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
}








void ServerCollector::Run(void) {
  LOG(ERROR) << "ServerCollector::Run";

  /* initialize gc_workers_pool_ */
 // Thread* self = Thread::Current();
  gc_workers_pool_ = new WorkStealingThreadPool(3);


  while(true) {
    LOG(ERROR) << "---------------run ServerCollector----------- " << heap_data_->conc_count_;
    WaitForRequest();
    ExecuteGC();

    LOG(ERROR) << "---------------workers are done ------";
    //WaitForGCTask();
  }
}

void* ServerCollector::RunCollectorDaemon(void* args) {
  ServerCollector* _server = reinterpret_cast<ServerCollector*>(args);

  Runtime* runtime = Runtime::Current();
  CHECK(runtime->AttachCurrentThread("serverCollector", true, NULL, false));

  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    MutexLock mu(self, _server->run_mu_);
    _server->thread_ = self;
    _server->run_cond_.Broadcast(self);
  }

//  bool _createThread =  runtime->AttachCurrentThread("serverCollector", true,
//      runtime->GetSystemThreadGroup(),
//      !runtime->IsCompiler());
//
//  if(!_createThread) {
//    LOG(ERROR) << "-------- could not attach internal GC service Daemon ---------";
//    return NULL;
//  }
  LOG(ERROR) << "ServerCollector::RunCollectorDaemon after broadcast" ;

  _server->Run();

  return NULL;
}



ServerCollector* ServerCollector::CreateServerCollector(void* args) {
  space::GCSrvSharableHeapData* _meta_alloc =
      (space::GCSrvSharableHeapData*) args;
  return new ServerCollector(_meta_alloc);
}



#if 0


void ServerCollector::WaitForGCTask(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::WaitForGCTask.." << self->GetTid();

  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    while(heap_data_->conc_flag_ != 2) {
      conc_req_cond_->Wait(self);
      LOG(ERROR) << "ServerCollector::WaitForGCTask.. " << self->GetTid() <<
          ", setting conc flag to " << heap_data_->conc_flag_;
    }
    LOG(ERROR) << "ServerCollector::WaitForGCTask.. " << self->GetTid() <<
        ", leaving while flag " << heap_data_->conc_flag_;
    heap_data_->conc_flag_ = 0;
    conc_req_cond_->Broadcast(self);
  }

  if(false) {

    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *phase_mu_);
      heap_data_->gc_phase_ = space::IPC_GC_PHASE_FINISH;
      LOG(ERROR) << "ServerCollector::WaitForGCTask..setting phase to IPC_GC_PHASE_FINISH: " << self->GetTid();
      phase_cond_->Wait(self);
    }
  }

//  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
//  {
//    MutexLock mu(self, run_mu_);
//    status_ = 0;
//    LOG(ERROR) << "ServerCollector::WaitForGCTask.. leaving" << self->GetTid();
//  }
//  IPMutexLock interProcMu(self, *phase_mu_);
//  while(heap_data_->gc_phase_ != space::IPC_GC_PHASE_FINISH) {
//    LOG(ERROR) << "ServerCollector::WaitForGCTask..inside while loop." << self->GetTid();
//    phase_cond_->Wait(self);
//  }
//  LOG(ERROR) << "ServerCollector::WaitForGCTask..left the wait condition.." << self->GetTid();
//  if(heap_data_->gc_phase_ == space::IPC_GC_PHASE_FINISH) {
//    heap_data_->gc_phase_ = space::IPC_GC_PHASE_NONE;
//  }
//  phase_cond_->Broadcast(self);
}

void ServerCollector::WaitForConcMarkPhaseGC(void) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *phase_mu_);
    LOG(ERROR) << "Server going to wait for finish phase = " << heap_data_->gc_phase_;
    while(heap_data_->gc_phase_ != space::IPC_GC_PHASE_CONC_MARK)
      phase_cond_->Wait(self);
    LOG(ERROR) << "Server Acknowledge the finishing phase: phase = " << heap_data_->gc_phase_;
  }
}

#define GC_IPC_SERVER_BLOCK_ON_PHASE(PHASE, THREAD) \
    ScopedThreadStateChange tsc(THREAD, kWaitingForGCProcess); \
    IPMutexLock interProcMu(THREAD, *phase_mu_); \
    while(heap_data_->gc_phase_ != PHASE) \
      phase_cond_->Wait(THREAD);

void ServerCollector::ConcMarkPhaseGC(void) {
  Thread* currThread = Thread::Current();
  LOG(ERROR) << "ServerCollector::ConcMarkPhaseGC. startingA: " <<
      currThread->GetTid() << "; phase:" << heap_data_->gc_phase_;
  {
//    GC_IPC_SERVER_BLOCK_ON_PHASE(space::IPC_GC_PHASE_PRE_CONC_ROOT_MARK, currThread);
//    phase_cond_->Broadcast(currThread);
  }
  LOG(ERROR) << "ServerCollector::ConcMarkPhaseGC. endingA: " <<
      currThread->GetTid() << "; phase:" << heap_data_->gc_phase_;
  {
    ScopedThreadStateChange tsc(currThread, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(currThread, *phase_mu_);
      heap_data_->gc_phase_ = space::IPC_GC_PHASE_CONC_MARK;
      phase_cond_->Broadcast(currThread);
    }
  }
  LOG(ERROR) << "ServerCollector::ConcMarkPhaseGC. endingB: " <<
      currThread->GetTid() << "; phase:" << heap_data_->gc_phase_;
}

void ServerCollector::WaitForFinishPhaseGC(void) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *phase_mu_);
    LOG(ERROR) << "Server going to wait for finish phase = " << heap_data_->gc_phase_;
    while(heap_data_->gc_phase_ != space::IPC_GC_PHASE_FINISH)
      phase_cond_->Wait(self);
    LOG(ERROR) << "Server Acknowledge the finishing phase: count = " << heap_data_->conc_count_;
  }
}

void ServerCollector::PostFinishPhaseGC(void) {
  Thread* self = Thread::Current();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *phase_mu_);
    LOG(ERROR) << "Server inside post finish phase = " << heap_data_->gc_phase_;
    heap_data_->gc_phase_ = space::IPC_GC_PHASE_POST_FINISH;
    phase_cond_->Broadcast(self);
    LOG(ERROR) << "Server leaving post finish phase: phase = " << heap_data_->gc_phase_;
  }
}
#endif

}
}
}
