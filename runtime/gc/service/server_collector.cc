

#include <string>
#include <cutils/ashmem.h>
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "mem_map.h"
#include "gc/service/global_allocator.h"
#include "gc/collector/ipc_server_sweep.h"

namespace art {
namespace gc {
namespace gcservice {

ServerCollector::ServerCollector(GCServiceClientRecord* client_rec,
                                space::GCSrvSharableHeapData* meta_alloc) :
    heap_data_(meta_alloc),
    run_mu_("ServerLock"),
    run_cond_("ServerLock::cond_", run_mu_),
    thread_(NULL),
    status_(0),
    shake_hand_mu_("shake_hand"),
    shake_hand_cond_("ServerLock::cond_", shake_hand_mu_),
    curr_collector_addr_(NULL),
    cycles_count_(0),
    trims_count_(0) {


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
    //LOG(ERROR) << "------------------ServerCollector--->going to wait for initialization of Daemon";
    while (thread_ == NULL) {
      run_cond_.Wait(self);
    }

    if(true) {
      ipc_msweep_ = new collector::IPCServerMarkerSweep(client_rec);
      ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
      {
        IPMutexLock interProcMu(self, *(conc_req_cond_mu_));
        conc_req_cond_->Broadcast(self);
        //Signaled the client that we are done with all necessary initialization
      }
    }
  }

}


void ServerCollector::SignalCollector(GCSrvceAgent* curr_srvc_agent, GCServiceReq* gcsrvc_req) {
  Thread* self = Thread::Current();
  GC_SERVICE_TASK req_type =  static_cast<GC_SERVICE_TASK>(gcsrvc_req->req_type_);
 // LOG(ERROR) << "ServerCollector::SignalCollector..." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    MutexLock mu(self, run_mu_);
    curr_srvc_agent_ = curr_srvc_agent;
    if(thread_ != NULL) {
      int32_t old_status = 0;
      int32_t new_value = 0;
      do {
        old_status = status_;
        new_value = old_status | req_type;//+ (is_explicit? (1 << 16) : 1);
      } while (android_atomic_cas(old_status, new_value, &status_) != 0);
      curr_srvc_req_ = gcsrvc_req;
     // LOG(ERROR) << "ServerCollector::SignalCollector ---- Thread was not null:" << self->GetTid() << "; status=" << status_;
      run_cond_.Broadcast(self);
    } else {
      IPC_MS_VLOG(INFO) << "ServerCollector::SignalCollector ---- Thread was  null:" << self->GetTid();
    }
//    if(status_ == 0) {
//      status_ = 1;
//    }
//
  }

  //LOG(ERROR) << "ServerCollector::SignalCollector...LEaving: " << self->GetTid();
}

int ServerCollector::WaitForRequest(void) {
  int32_t _result = 0;
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
      int32_t _status_barrier = 0;
      while((_status_barrier = android_atomic_release_load(&status_)) <= 0) {
        run_cond_.Wait(self);
      }
      _result = _status_barrier;
//      if(_status_barrier >= (1<<16)) {
//        _result = 2;
//      }

      while (android_atomic_cas(_status_barrier, 0U, &status_) != 0) {
        _status_barrier = android_atomic_release_load(&status_);
      }
     // LOG(ERROR) << "ServerCollector::WaitForRequest:: leaving WaitForRequest; status=" << status_;
      run_cond_.Broadcast(self);
    }
  }
  return _result;


}


class ServerMarkReachableTask : public WorkStealingTask {
 public:
  ServerCollector* server_instant_;
  space::GCSrvSharableCollectorData* volatile curr_collector_addr_;
  static volatile int performed_cycle_index_;

  ServerMarkReachableTask(ServerCollector* server_object) :
    WorkStealingTask(), server_instant_(server_object),
    curr_collector_addr_(NULL) {
   // LOG(ERROR) << "creating worker stealing task listener";
  }
  void StealFrom(Thread* self, WorkStealingTask* source) {
    if(ServerMarkReachableTask::performed_cycle_index_ !=
        server_instant_->cycles_count_)
      source->Run(self);
  }

  void WaitForPhaseAddress(Thread* self) {
    //LOG(ERROR) << " ++++ Phase going for current addreess  ++++ " << self->GetTid();
    server_instant_->BlockOnCollectorAddress(self);
    curr_collector_addr_ = server_instant_->heap_data_->current_collector_;
    //LOG(ERROR) << " ++++ Phase done waiting for current addreess  ++++ "
   //     << self->GetTid() << "; address " << reinterpret_cast<void*>(curr_collector_addr_);
  }

  void WaitForReachablePhaseAddress(Thread* self) {
   // LOG(ERROR) << " ++++ task waiting for the reachable phase ++++ " << self->GetTid();
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
      //LOG(ERROR) << " ++++ Phase TASK noticed change  ++++ " << self->GetTid()
      //    << " phase=" << curr_collector_addr_->gc_phase_;

      if(true)
        server_instant_->ipc_msweep_->MarkReachableObjects(curr_collector_addr_);

     // LOG(ERROR) << " ++++ post Phase TASK updated the phase of the GC: "
     //     << self->GetTid() << ", phase:" << curr_collector_addr_->gc_phase_;
      curr_collector_addr_->gc_phase_ = space::IPC_GC_PHASE_MARK_RECURSIVE;
//      performed_cycle_index_ = server_instant_->cycles_count_;
      server_instant_->phase_cond_->Broadcast(self);
    }
  }


  void WaitForSweepPhase(Thread* self) {
    //LOG(ERROR) << " ++++ task waiting for the sweeping phase ++++ " << self->GetTid();
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->phase_mu_));
      while(true) {
        if(curr_collector_addr_ != NULL) {
          if(curr_collector_addr_->gc_phase_ == space::IPC_GC_PHASE_SWEEP) {
            break;
          }
        }
        server_instant_->phase_cond_->Wait(self);
      }
     // LOG(ERROR) << " ++++ Phase TASK noticed change  ++++ " << self->GetTid()
     //     << " phase=" << curr_collector_addr_->gc_phase_;

      if(true)
        server_instant_->ipc_msweep_->SweepSpaces(curr_collector_addr_);

    //  LOG(ERROR) << " ++++ post Phase TASK updated the phase of the GC: "
       //   << self->GetTid() << ", phase:" << curr_collector_addr_->gc_phase_;
      curr_collector_addr_->gc_phase_ = space::IPC_GC_PHASE_FINALIZE_SWEEP;
      performed_cycle_index_ = server_instant_->cycles_count_;
      server_instant_->phase_cond_->Broadcast(self);
    }
  }

//  void ExecuteReachableMarking(Thread* self) {
//    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
//    {
//      IPMutexLock interProcMu(self, *(server_instant_->phase_mu_));
//      LOG(ERROR) << " ++++ pre Phase TASK updated the phase of the GC: "
//          << self->GetTid() << curr_collector_addr_->gc_phase_;
//      curr_collector_addr_->gc_phase_ = space::IPC_GC_PHASE_MARK_RECURSIVE;
//      LOG(ERROR) << " ++++ post Phase TASK updated the phase of the GC: "
//          << self->GetTid() << ", phase:" << curr_collector_addr_->gc_phase_;
//      performed_cycle_index_ = server_instant_->cycles_count_;
//      server_instant_->phase_cond_->Broadcast(self);
//
//    }
//  }
  // Scans all of the objects
  virtual void Run(Thread* self) {
    if(performed_cycle_index_ == server_instant_->cycles_count_) {
      IPC_MS_VLOG(INFO) << " XXXX No need to rerun the Phase reachable XXXX";
      return;
    }
    WaitForPhaseAddress(self);



    //LOG(ERROR) << "@@@@@@@@@@@@@@@@ We ran mark reachables task @@@@@@@@@@@@@@@@@@@ "
   //     << self->GetTid();
    WaitForReachablePhaseAddress(self);
    //LOG(ERROR) << "@@@@@@@@@@@@@@@@ We ran Sweeping phase task @@@@@@@@@@@@@@@@@@@ "
   //     << self->GetTid();
    WaitForSweepPhase(self);
    //ExecuteReachableMarking(self);
  }

  virtual void Finalize() {
   // LOG(ERROR) << "@@@@@@@@@@@@@@@@Finalize@@@@@@@@@@@@";
    delete this;
  }
};
volatile int ServerMarkReachableTask::performed_cycle_index_ = -1;

class ServerIPCListenerTask : public WorkStealingTask {
 public:
  ServerCollector* server_instant_;
  space::GCSrvSharableCollectorData* volatile curr_collector_addr_;
  volatile int* collector_index_;
  static volatile int performed_cycle_index_;

  ServerIPCListenerTask(ServerCollector* server_object) : WorkStealingTask() ,
    server_instant_(server_object),
    curr_collector_addr_(NULL),
    collector_index_(NULL){
    collector_index_ = &(server_instant_->heap_data_->collect_index_);
  }
  void StealFrom(Thread* self, WorkStealingTask* source) {
    if(ServerIPCListenerTask::performed_cycle_index_ !=
        server_instant_->cycles_count_)
      source->Run(self);
  }


  void SetCurrentCollector(Thread* self) {
    curr_collector_addr_ = server_instant_->heap_data_->current_collector_;
//    *collector_index_ = server_instant_->heap_data_->collect_index_;
//    LOG(ERROR) << " server: creating worker stealing task ServerIPCListenerTask: " <<
//        "index = " << server_instant_->heap_data_->collect_index_ <<
 //       "\n    address = " << reinterpret_cast<void*>(curr_collector_addr_);

    server_instant_->UpdateCollectorAddress(self, curr_collector_addr_);

  }

  void WaitForCollector(Thread* self) {

    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->conc_req_cond_mu_));
 //     LOG(ERROR) << "@@@@ going to wait for collector @@@" << self->GetTid()
  //        << "; conc flag = " << server_instant_->heap_data_->conc_flag_;
      if(server_instant_->heap_data_->conc_flag_ >= 3) {
  //      LOG(ERROR) << "@@@@ rturning @@@" << self->GetTid()
  //          << "; conc flag = " << server_instant_->heap_data_->conc_flag_;
        return;
      }
      while(true) {
        if(server_instant_->heap_data_->conc_flag_ == 2) {
          if(*collector_index_ != -1) {
            SetCurrentCollector(self);
            server_instant_->heap_data_->conc_flag_ = 3;
//            LOG(ERROR) << "~~~~~~~~~~~~~~~ WaitForCollector is leaving ~~~~~~~"
//                << self->GetTid() << "; conc_flag = "
//                << server_instant_->heap_data_->conc_flag_;
            server_instant_->conc_req_cond_->Broadcast(self);
            break;
          }
        }
        if(server_instant_->heap_data_->conc_flag_ >= 3)
          break;
        server_instant_->conc_req_cond_->Wait(self);
      }
    }
  }

  // Scans all of the objects
  virtual void Run(Thread* self) {
    if(performed_cycle_index_ == server_instant_->cycles_count_) {
      LOG(ERROR) << " XXXX No need to Run since we already done for that tour XXXX ";
      return;
    }
  //  LOG(ERROR) << "@@@@@@@@@@@@@@@@ Run Wait completion task @@@@@@@@@@@@@@@@@@@ "
  //      << self->GetTid() << "; conc_flag=" << server_instant_->heap_data_->conc_flag_;
    WaitForCollector(self);
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *(server_instant_->conc_req_cond_mu_));
  //   LOG(ERROR) << " server: RUN IPC LISTERNERS: server_instant_->heap_data_->collect_index_: "
   //       <<  server_instant_->heap_data_->collect_index_ << ", tid:" << self->GetTid();
      while(server_instant_->heap_data_->conc_flag_ < 5) {
        server_instant_->conc_req_cond_->Wait(self);
   //     LOG(ERROR) << "@@ServerCollector::WaitForGCTask.. " << self->GetTid() <<
   //         ", setting conc flag to " << server_instant_->heap_data_->conc_flag_;
      }
      if(server_instant_->heap_data_->conc_flag_ == 5) {
        server_instant_->heap_data_->conc_flag_ = 6;
    //    LOG(ERROR) << "@@ServerCollector::WaitForGCTask.. " << self->GetTid() <<
     //       ", leaving while flag " << server_instant_->heap_data_->conc_flag_;
        performed_cycle_index_ = server_instant_->cycles_count_;
        server_instant_->conc_req_cond_->Broadcast(self);
      }

    }



  }
  virtual void Finalize() {
    //LOG(ERROR) << "@@@@@@@@@@@@@@@@ Finalize ServerIPCListenerTask @@@@@@@@@@@@";
    delete this;
  }
};
volatile int ServerIPCListenerTask::performed_cycle_index_ = -1;

void ServerCollector::UpdateCollectorAddress(Thread* self,
    space::GCSrvSharableCollectorData* address) {
  MutexLock mu(self, shake_hand_mu_);
  curr_collector_addr_ = address;
  //LOG(ERROR) << "ServerCollector::UpdateCollectorAddress " <<  self->GetTid()
    //  << ", address: " << reinterpret_cast<void*>(address);
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

void ServerCollector::FinalizeGC(Thread* self) {
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *(conc_req_cond_mu_));
    curr_srvc_agent_->UpdateRequestStatus(curr_srvc_req_);
    heap_data_->conc_flag_ = 0;
    conc_req_cond_->Broadcast(self);
  }
}


void ServerCollector::ExecuteTrim() {
  Thread* self = Thread::Current();

  {
    MutexLock mu(self, shake_hand_mu_);
    curr_collector_addr_ = NULL;
    trims_count_++;
  }

  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    heap_data_->conc_flag_ = 1;
    heap_data_->gc_type_ = GC_SERVICE_TASK_TRIM;
    conc_req_cond_->Broadcast(self);

    while(heap_data_->conc_flag_ < 5) {
      conc_req_cond_->Wait(self);
    }

  }

  FinalizeGC(self);

  //LOG(ERROR) << "-----------------ServerCollector:: Leaving ExecuteTrim-------------------" << self->GetTid();
}

void ServerCollector::ExecuteGC(GC_SERVICE_TASK gc_type) {
  Thread* self = Thread::Current();
  //LOG(ERROR) << "-----------------ServerCollector::ExecuteGC-------------------" << self->GetTid();
  {
    MutexLock mu(self, shake_hand_mu_);
    curr_collector_addr_ = NULL;
    cycles_count_++;
  }

  gc_workers_pool_->AddTask(self, new ServerIPCListenerTask(this));
  gc_workers_pool_->AddTask(self, new ServerMarkReachableTask(this));
  gc_workers_pool_->SetMaxActiveWorkers(2);
  //gc_workers_pool_->AddTask(self, reachable_task);
  //LOG(ERROR) << "@@@@@@@ Thread Pool starting the tasks " << self->GetTid();
  gc_workers_pool_->StartWorkers(self);
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);

 //   LOG(ERROR) << "ServerCollector::ExecuteGC: set concurrent flag";
    heap_data_->conc_flag_ = 1;
    heap_data_->gc_type_ = gc_type;
    conc_req_cond_->Broadcast(self);
  //  LOG(ERROR) << "ServerCollector::ExecuteGC.. " << self->GetTid() <<
  //            ", setting conc flag to " << heap_data_->conc_flag_ <<
 //             "gctype=" << heap_data_->gc_type_;
  }
  gc_workers_pool_->Wait(self, true, true);
  //LOG(ERROR) << "@@@@@@@ Thread Pool LEaving the Wait Call @@@@@";

  gc_workers_pool_->StopWorkers(self);
  FinalizeGC(self);
  //LOG(ERROR) << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
}








void ServerCollector::Run(void) {
 // LOG(ERROR) << "ServerCollector::Run";

  /* initialize gc_workers_pool_ */
 // Thread* self = Thread::Current();
  gc_workers_pool_ = new WorkStealingThreadPool(3);

  bool propagate = false;
  int cpu_id = 0;
  bool _setAffin =
      gcservice::GCServiceGlobalAllocator::GCSrvcIsClientDaemonPinned(&cpu_id,
                                                                      &propagate,
                                                                      false);

  gc_workers_pool_->dumpThreadsID();
  if(_setAffin) {
    gc_workers_pool_->setThreadsAffinity(cpu_id);
  }


  GC_SERVICE_TASK _gc_type = GC_SERVICE_TASK_NOP;
  while(true) {
   // LOG(ERROR) << "---------------run ServerCollector----------- " << cycles_count_;
    _gc_type = static_cast<GC_SERVICE_TASK>(WaitForRequest());

    if(_gc_type == GC_SERVICE_TASK_TRIM)
      ExecuteTrim();
    else if(_gc_type != GC_SERVICE_TASK_NOP)
      ExecuteGC(_gc_type);


   // LOG(ERROR) << "---------------workers are done ------";
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
 // LOG(ERROR) << "ServerCollector::RunCollectorDaemon after broadcast" ;

  _server->Run();

  return NULL;
}



ServerCollector* ServerCollector::CreateServerCollector(void* args) {
  gcservice::GCServiceClientRecord* _client_rec =
      (gcservice::GCServiceClientRecord*) args;

  return new ServerCollector(_client_rec,
      &(_client_rec->sharable_space_->heap_meta_));
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
