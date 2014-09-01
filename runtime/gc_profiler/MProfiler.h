/*
 * MProfiler.h
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#ifndef MPROFILER_H_
#define MPROFILER_H_

#include <string>
#include <list>
#include "locks.h"
#include "base/mutex.h"
#include "base/unix_file/fd_file.h"
#include "os.h"

#include "gc_profiler/MProfilerTypes.h"
/**********************************************************************
 * 											Macros Definitions
 **********************************************************************/
#define GCMMP_ARRAY_SIZE(array) (sizeof((array))/sizeof((array[0])))


/** definitions for logging and error reporting  **/
#define GCMMP_ALLOW_PROFILE 								0


/*
 * Checks if the VM is one of the profiled Benchmarks.
 *
 * Note: found that the VM name was set multiple times. I have no idea
 */
void dvmGCMMProfPerfCounters(const char*);




namespace art {
class ConditionVariable;
class Mutex;


namespace mprofiler {

typedef void (*GCMMPDumpCurrentUsage)(bool);

/* types of Profiling defined here */
typedef struct GCMMPProfilingEntry_S {
	int 						id_;					/* id of the profiling */
	unsigned int 		flags_;				/* the flag vector used to specify the functionality*/
	const char			*name_;	     	/* event name */
	const char			*desc_;	     	/* event description */
	const char			*logFile_;	  /* log file name */
	GCMMPDumpCurrentUsage dumpMethod;
}GCMMPProfilingEntry;

class MProfiler {
private:

	//Index of the profiler type we are running
	const int index_;
  // System thread used as main (thread id = 1).
	Thread* main_thread_;
  // System thread used as GC Daemon (thread id = 1).
	Thread* gc_daemon_;

  // flags of the Profiler we are running
  // combines markAllocWindows, createProfDaemon, hasProfThread,
  unsigned int flags_;

  /* file used to dump the profiling data */
  const char * dump_file_name_;
  art::File* dump_file_;

  /*
   * Guards access to the state of the profiler daemon,
   * associated conditional variable is used to signal when a GC completes
   */
  Mutex* prof_thread_mutex_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> prof_thread_cond_ GUARDED_BY(prof_thread_mutex_);
  pthread_t pthread_ GUARDED_BY(prof_thread_mutex_);
	/* array of thread records used to keep the data per thread */
  // The actual list of all threads.
  std::list<GCMMPThreadProf*> threadProflist_;
  // System thread used for the profiling (profileDaemon).
	Thread* prof_thread_;
  void CreateProfilerDaemon(void);

  void InitializeProfiler(void);

  void DumpProfData(void);

  const bool enabled_;

  volatile bool running_;

  static void* Run(void* arg);


  void OpenDumpFile(void);

  bool IsCreateProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_CREATE_DAEMON);
  }
  bool IsProfilingRunning() {
    return running_;
  }


  void SetMProfileFlags(void);

  void AttachThreads(void);

  bool ProfiledThreadsContain(Thread*);
  void RemoveThreadProfile(GCMMPThreadProf*);
public:
	static constexpr int kGCMMPDumpSignal = SIGUSR2;
	static const unsigned int kGCMMPEnableProfiling = 0;
  static const int kGCMMPDisableMProfile = 999;
  static const int kGCMMPDefaultGrowMethod = 0;
  static const int kGCMMPMAXThreadCount = 64;
	// List of profiled benchmarks in our system
	static const char * benchmarks[];
	/*
	 * list of the directory paths to save the logs
	 */
	static const char * gcMMPRootPath[];

	/*
	 * Predefined List of types
	 */
	static const GCMMPProfilingEntry profilTypes[];

	static MProfiler* instance_;

	static MProfiler* Current() {
		return instance_;
	}

	MProfiler(GCMMP_Options*);

	~MProfiler();

	//bool VerifyProcessName();

	void ProcessSignal(int);

	void PreForkPreparation(void);

  bool IsProfilingEnabled() const {
    return enabled_;
  }
  void AttachThread(Thread*);
  void DettachThread(Thread*);
  void GCMMProfPerfCounters(const char*);

  GCMMPDumpCurrentUsage dumpCurrUsage;

  static bool IsMProfRunning() {
  	MProfiler* mP = MProfiler::Current();
  	if(mP != NULL)
  		return mP->IsProfilingRunning();
  	return false;
  }

  static void MProfAttachThread(art::Thread*);

  static void MProfDetachThread(art::Thread*);

  friend class GCMMPThreadProf;
}; //class MProfiler



}  // namespace mprofiler
}  // namespace gc

#endif /* MPROFILER_H_ */
