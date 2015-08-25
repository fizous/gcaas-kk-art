/*
 * service_ipcfs.cc
 *
 *  Created on: Aug 25, 2015
 *      Author: hussein
 */




/*
 * ipcfs.cpp
 *
 *  Created on: Aug 24, 2015
 *      Author: hussein
 */


#include <jni.h>
#include <sys/mman.h>
#include <binder/IInterface.h>
#include <binder/IBinder.h>
#include <binder/Binder.h>
#include <binder/IInterface.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <gc/gcservice_allocator/service_ipcfs.h>

namespace android {

// The binder message ids
enum {
  SET_FD = IBinder::FIRST_CALL_TRANSACTION
};



// The client interface
class BpSHM : public BpInterface<ISHM> {
  public:
    BpSHM(const sp<IBinder>& impl) : BpInterface<ISHM>(impl) {
      ALOGD("BpSHM::BpSHM()");
    }

    virtual status_t setFD(uint32_t fd)
    {
      ALOGD("BpSHM::setFD(%u)", fd);
      Parcel data, reply;
      data.writeInterfaceToken(ISHM::getInterfaceDescriptor());
      data.writeFileDescriptor(fd);
      remote()->transact(SET_FD, data, &reply);
      return reply.readInt32();
    }
};

IMPLEMENT_META_INTERFACE(SHM, "io.vec.IPC");

// The server interface
status_t BnSHM::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
  ALOGD("BnSHM::onTransact(%u)", code);
  CHECK_INTERFACE(ISHM, data, reply);
  switch(code) {
    case SET_FD: {
           reply->writeInt32(setFD(data.readFileDescriptor()));
           return NO_ERROR;
         } break;
    default: {
           return BBinder::onTransact(code, data, reply, flags);
         } break;
  }
}




void SHM::registerService()
{
  defaultServiceManager()->addService(String16("binder_shm"), new SHM());
}


sp<ISHM> SHM::getService()
{
  sp<IBinder> binder = defaultServiceManager()->getService(String16("binder_shm"));
  return interface_cast<ISHM>(binder);
}

status_t SHM::setFD(uint32_t fd)
{
  ALOGD("SHM::setFD(%u)", fd);
  uint8_t *shm = (uint8_t*)mmap(NULL, 1024, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
  if (shm == MAP_FAILED) {
    ALOGE("mmap failed! %d, %s", errno, strerror(errno));
    return UNKNOWN_ERROR;
  }
  ALOGD("mmap %p", shm);

  // Now we can use shm to share data with Java MemoryFile.
  // For this demo, we just release it.
  munmap(shm, 1024);
  return NO_ERROR;
}

}//namespace android
