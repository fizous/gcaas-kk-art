/*
 * gcservice.h
 *
 *  Created on: Aug 30, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_GCSERVICE_H_
#define ART_RUNTIME_GC_GCSERVICE_GCSERVICE_H_

#include "gc/gcservice/common.h"
#include "gc/collector/gc_type.h"
#include "gc/space/space.h"
#include "gc/gcservice/service_allocator.h"
#include "gc/gcservice/gcservice_daemon.h"

namespace art {

namespace gcservice {



class GCService {
public:
  static GCService* service_;
  static const gc::space::GcRetentionPolicy kZygotePolicy =
      gc::space::kGcRetentionPolicyNeverCollect;


  void initServiceMetaData(GCServiceMetaData*);

  void launchProcess(void);

  static bool InitService(void);
  static void GCPBlockForServiceReady(void);
  static void GCPRegisterWithGCService(void);
  /******************** setters and getters ************************/
  inline void _Status(GC_SERVICE_STATUS new_status) {
    service_meta_data_->status_ = new_status;
  }

  inline int _Counter() {
    return service_meta_data_->counter_;
  }

  inline int _Status() {
    return service_meta_data_->status_;
  }

  inline InterProcessMutex* _Mu() {
    return service_meta_data_->mu_;
  }

  inline InterProcessConditionVariable* _Cond() {
    return service_meta_data_->cond_;
  }

  /******************** setters and getters ************************/

  bool isStopped(void) {
    return (_Status() == GCSERVICE_STATUS_STOPPED);
  }

  bool isShuttingDown(void) {
    return (_Status() == GCSERVICE_STATUS_SHUTTING_DOWN);
  }

  bool isRunning(void) {
    return (_Status() == GCSERVICE_STATUS_RUNNING);
  }


  bool isNotRunning(void) {
    return (_Status() < GCSERVICE_STATUS_RUNNING);
  }

  static void PreZygoteFork(void);
  static gc::collector::GcType FilterCollectionType(gc::collector::GcType);
  static gc::space::GcRetentionPolicy
      GetZygoteRetentionPolicy(gc::space::GcRetentionPolicy);
  static bool SetZygoteSpaceProtection(void);
private:
  static bool zygoteHeapInitialized;

  GCService(void);

  ServiceAllocator* allocator_;
  GCServiceMetaData* service_meta_data_;


  GCServiceProcess* process_;

};//class GCService






}//namespace gcservice
}//namespace art


#endif /* ART_RUNTIME_GC_GCSERVICE_GCSERVICE_H_ */
