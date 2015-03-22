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
#include "base/logging.h"
#include "thread_state.h"
#include "gc_profiler/MProfilerTypes.h"
#include "cutils/system_clock.h"
#include "utils.h"

/**********************************************************************
 * 											Macros Definitions
 **********************************************************************/
#define VERBOSE_PROFILER 0
/* log information. used to monitor the flow of the profiler.*/
#define GCMMP_VLOG(severity) if (VERBOSE_PROFILER) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream()


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
class VMProfiler;




typedef void (*GCMMPDumpCurrentUsage)(bool);
typedef VMProfiler* (*VMProfilerConstructor) (GCMMP_Options*);
/* types of Profiling defined here */
typedef struct PACKED(4) GCMMPProfilingEntry_S {
	int 						id_;					/* id of the profiling */
	unsigned int 		flags_;				/* the flag vector used to specify the functionality*/
	const char			*name_;	     	/* event name */
	const char			*desc_;	     	/* event description */
	const char			*logFile_;	  /* log file name */
	GCMMPDumpCurrentUsage dumpMethod;
	VMProfilerConstructor creator_;
}GCMMPProfilingEntry;


template <typename T>
art::mprofiler::VMProfiler* createVMProfiler(GCMMP_Options* opts)
{
	return new T(opts);
}



class VMProfiler {

protected:
	//Index of the profiler type we are running
	const int index_;



  /* file used to dump the profiling data */
  const char * dump_file_name_;
  art::File* dump_file_;

  const char			*perfName_;

  void InitializeProfiler(void);

  void DumpProfData(bool);



  /*
   * Guards access to the state of the profiler daemon,
   * associated conditional variable is used to signal when a GC completes
   */
  Mutex* prof_thread_mutex_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> prof_thread_cond_ GUARDED_BY(prof_thread_mutex_);
  pthread_t pthread_ GUARDED_BY(prof_thread_mutex_);
public:
  static constexpr int kGCMMPDumpSignal = SIGUSR2;
  static const int kGCMMPDefaultAffinity = -1;
	static constexpr int kGCMMPLogAllocWindow = 16;
	// List of profiled benchmarks in our system
	static const char * benchmarks[];
  // combines markAllocWindows, createProfDaemon, hasProfThread,
	/*
	 * list of the directory paths to save the logs
	 */
	static const char * gcMMPRootPath[];

	/*
	 * Predefined List of types
	 */
	static const GCMMPProfilingEntry profilTypes[];

	const bool enabled_;
	// System thread used as main (thread id = 1).

	int gcDaemonAffinity_;

	Thread* prof_thread_;


	Thread* main_thread_;
  // System thread used as GC Daemon.
	Thread* gc_daemon_;
  // System thread used as GC Timmer.
	Thread* gc_trimmer_;
  // flags of the Profiler we are running
  unsigned int flags_;
	volatile bool has_profDaemon_;
	volatile bool receivedShutdown_;
	volatile bool running_;
	volatile bool receivedSignal_ GUARDED_BY(prof_thread_mutex_);

  size_t 		start_heap_bytes_;
  size_t 		end_heap_bytes_;

  uint64_t 	start_time_ns_;
  uint64_t 	end_time_ns_;
  uint64_t 	cpu_time_ns_;
  uint64_t 	end_cpu_time_ns_;


  AtomicInteger total_alloc_bytes_;

  uint64_t GetRelevantCPUTime(void) const {
  	return ProcessTimeNS() - cpu_time_ns_;
  }

  uint64_t GetRelevantRealTime(void) const {
  	return uptime_nanos() - start_time_ns_;
  }


  std::vector<GCMMPThreadProf*> threadProfList_;
  void attachThreads(void);
  void attachSingleThread(Thread* t);
  void notifyAllocation(size_t);
  void createProfDaemon();

  VMProfiler(GCMMP_Options*);
	virtual ~VMProfiler(){}

	static void* runDaemon(void* arg);

  bool IsProfilingEnabled() const {
    return enabled_;
  }
  bool IsCreateProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_CREATE_DAEMON);
  }

  bool IsProfilingRunning() {
    return running_;
  }

  void setIsProfilingRunning(bool val) {
    running_ = val;
  }

  bool IsAttachProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_ATTACH_PROF_DAEMON);
  }

  bool IsSetAffinityThread() const {
    return IsProfilingEnabled() && (gcDaemonAffinity_ != kGCMMPDefaultAffinity) ;
  }

  bool IsAttachGCDaemon () {
    return (flags_ & GCMMP_FLAGS_ATTACH_GCDAEMON);
  }

  bool IsAllocWindowsSet() {
  	return (flags_ & GCMMP_FLAGS_MARK_ALLOC_WINDOWS);
  }

  bool IsProfilingTimeEvent() {
    return IsProfilingRunning() && isMarkTimeEvents();
  }

	void setMainThread(Thread* thread) {
		main_thread_ = thread;
	}

	void setGcDaemon(Thread* thread) {
		gc_daemon_ = thread;
	}

	void setGcTrimmer(Thread* thread) {
		gc_trimmer_ = thread;
	}


  void OpenDumpFile(void);
  void InitCommonData(void);
  virtual bool periodicDaemonExec(void) = 0;
  void ShutdownProfiling(void);
  void startProfiling(void);
  void ProcessSignalCatcher(int);

  virtual MPPerfCounter* createHWCounter(Thread*)=0;

  size_t getRelevantAllocBytes(void);

  void setThreadAffinity(art::Thread* th, bool complementary);

  bool createHWEvents(void) {return false;}
  virtual bool isMarkTimeEvents(void) {return false;}
  virtual bool dettachThread(GCMMPThreadProf*)=0;

  void setProfDaemon(bool);
  void setReceivedShutDown(bool);
  bool getRecivedShutDown(void);
  bool hasProfDaemon(void);

	virtual void addHWStartEvent(GCMMP_BREAK_DOWN_ENUM evt){};
	virtual void addHWEndEvent(GCMMP_BREAK_DOWN_ENUM evt) {};

	GCMMPHeapStatus heapStatus;
	virtual double getAllocIndex(){return heapStatus.index;};
	virtual void resetHeapAllocStatus(){memset((void*)&heapStatus, 0, sizeof(GCMMPHeapStatus));};
	void updateHeapAllocStatus(void);
	void updateHeapPerfStatus(uint64_t, uint64_t);

	void PreForkPreparation(void);
	void GCMMProfPerfCounters(const char*);


  void MarkWaitTimeEvent(GCMMPThreadProf*, GCMMP_BREAK_DOWN_ENUM);
  void MarkEndWaitTimeEvent(GCMMPThreadProf*, GCMMP_BREAK_DOWN_ENUM);

	static bool IsMProfRunning();
	static bool IsMProfilingTimeEvent();
	static void MProfAttachThread(art::Thread*);
	static void MProfNotifyAlloc(size_t);
	static void MProfileSignalCatcher(int);
	static void MProfDetachThread(art::Thread*);

  static void MProfMarkStartAllocGCHWEvent(void);
  static void MProfMarkEndAllocGCHWEvent(void);
  static void MProfMarkStartExplGCHWEvent(void);
  static void MProfMarkEndExplGCHWEvent(void);


  static void MProfMarkWaitTimeEvent(art::Thread*);
  static void MProfMarkEndWaitTimeEvent(art::Thread*);
  static void MProfMarkGCHatTimeEvent(art::Thread*);
  static void MProfMarkEndGCHatTimeEvent(art::Thread*);
  static void MProfMarkGCExplTimeEvent(art::Thread*);
  static void MProfMarkEndGCExplTimeEvent(art::Thread*);
  static void MProfMarkStartSafePointEvent(art::Thread*);
  static void MProfMarkEndSafePointEvent(art::Thread*);
  static void MProfMarkSuspendTimeEvent(art::Thread*, art::ThreadState);
  static void MProfMarkEndSuspendTimeEvent(art::Thread*, art::ThreadState);

};


class MMUProfiler : public VMProfiler {
public:

	MMUProfiler(GCMMP_Options* opts);
	~MMUProfiler(){};


	MPPerfCounter* createHWCounter(Thread*);
	bool createHWEvents(void) {return false;}
	bool isMarkTimeEvents(void) {return true;}
	bool periodicDaemonExec(void);
	bool dettachThread(GCMMPThreadProf*);




};

class PerfCounterProfiler : public VMProfiler {

public:
	const char			*hwEvent_;

	PerfCounterProfiler(GCMMP_Options* opts);
	~PerfCounterProfiler(){};
	int initCounters(const char*);
	MPPerfCounter* createHWCounter(Thread*);

	bool createHWEvents(void) {return true;}
	bool isMarkTimeEvents(void) {return false;}
	bool periodicDaemonExec(void);
	void readPerfData(void);
	bool dettachThread(GCMMPThreadProf*);
	void getPerfData(void);
	void logPerfData(void);


	void addHWStartEvent(GCMMP_BREAK_DOWN_ENUM evt);
	void addHWEndEvent(GCMMP_BREAK_DOWN_ENUM evt);

};

class MProfiler {
private:

	//Index of the profiler type we are running
	const int index_;
  // System thread used as main (thread id = 1).
	Thread* main_thread_;
  // System thread used as GC Daemon.
	Thread* gc_daemon_;
  // System thread used as GC Timmer.
	Thread* gc_trimmer_;
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

  void DumpProfData(bool);

	int gcDaemonAffinity_;

  const bool enabled_;

  volatile bool running_;

  static void* Run(void* arg);


  void OpenDumpFile(void);

  bool IsCreateProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_CREATE_DAEMON);
  }

  bool IsAttachProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_ATTACH_PROF_DAEMON);
  }

  bool IsProfilingRunning() {
    return vmProfile->IsProfilingRunning();
  }

  bool IsProfilingTimeEvent() {
    return vmProfile->IsProfilingRunning() && vmProfile->isMarkTimeEvents();
  }

  bool IsAttachGCDaemon () {
    return (flags_ & GCMMP_FLAGS_ATTACH_GCDAEMON);
  }

  bool hasProfDaemon_;

  volatile bool receivedSignal_ GUARDED_BY(prof_thread_mutex_);

  void ShutdownProfiling(void);

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
  static const int kGCMMPDefaultAffinity = -1;
  static const int kGCMMPDumpEndMarker;



	MProfiler(GCMMP_Options*);

	~MProfiler();

	//bool VerifyProcessName();

	void ProcessSignal(int);

  bool IsProfilingEnabled() const {
    return enabled_;
  }

  bool SetAffinityThread() const {
    return IsProfilingEnabled() && (gcDaemonAffinity_ != kGCMMPDefaultAffinity) ;
  }

  art::File* GetDumpFile(void) const {
  	return dump_file_;
  }


  void AttachThread(Thread*);
  bool DettachThread(GCMMPThreadProf*);


  void SetThreadAffinity(Thread*,bool);

  GCMMPDumpCurrentUsage dumpCurrUsage;
  void DumpCurrentOutpu(void);


  void ForEach(void (*callback)(GCMMPThreadProf*, void*), void* context);
  bool MainProfDaemonExec(void);









  void ProcessSignalCatcher(int);

  /* represents the time captured when we started the profiling */
  uint64_t 	start_time_ns_;
  uint64_t 	end_time_ns_;
  uint64_t 	cpu_time_ns_;
  uint64_t 	end_cpu_time_ns_;
  size_t 		start_heap_bytes_;
  size_t 		end_heap_bytes_;

  uint64_t GetRelevantCPUTime(void) const {
  	return ProcessTimeNS() - cpu_time_ns_;
  }

  uint64_t GetRelevantRealTime(void) const {
  	return uptime_nanos() - start_time_ns_;
  }

  size_t GetRelevantAllocBytes(void);

  int GetMainID(void);

  int GetGCDaemonID(void);

  friend class GCMMPThreadProf;


  VMProfiler* vmProfile;
}; //class MProfiler



}  // namespace mprofiler
}  // namespace gc

#endif /* MPROFILER_H_ */
