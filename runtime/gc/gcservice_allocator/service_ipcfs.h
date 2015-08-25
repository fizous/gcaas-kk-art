/*
 * service_ipcfs.h
 *
 *  Created on: Aug 25, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_IPCFS_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_IPCFS_H_

#include <binder/IInterface.h>
#include <binder/IBinder.h>
#include <binder/Binder.h>
#include <binder/IInterface.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>

namespace android {

class ISHM : public IInterface {
  public:
    virtual status_t setFD(uint32_t fd) = 0;

    DECLARE_META_INTERFACE(SHM);
};

// The server interface
class BnSHM : public BnInterface<ISHM> {
public:
  virtual status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags = 0);
};


// The server implementation
class SHM : public BnSHM {
public:
  static void registerService();
  status_t setFD(uint32_t fd);
  static sp<ISHM> getService();
};

}//android

#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_IPCFS_H_ */
