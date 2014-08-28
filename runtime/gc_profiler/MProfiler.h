/*
 * MProfiler.h
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#ifndef MPROFILER_H_
#define MPROFILER_H_

#include <string>
#include "locks.h"


/**********************************************************************
 * 											Macros Definitions
 **********************************************************************/
#define GCMMP_ARRAY_SIZE(array) (sizeof((array))/sizeof((array[0])))


/** definitions for logging and error reporting  **/
#define GCMMP_ALLOW_PROFILE 								0
#define GCMMP_GCPAUSE_ARRAY_SIZE					  32



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

/*
 * enum of the events we are profiling per mutator. we can look for activities.
 * Make sure that GCMMP_GC_BRK_MAXIMUM always at the bottom of the definition
 */
typedef enum {
	GCMMP_GC_BRK_SUSPENSION = 0,
	GCMMP_GC_BRK_WAIT_CONC,
	GCMMP_GC_BRK_GC_EXPL,
	GCMMP_GC_BRK_GC_HAT,
	GCMMP_GC_BRK_HEAP_LOCK,
	GCMMP_GC_BRK_MAXIMUM
} GCMMP_BREAK_DOWN_ENUM;

/*
 * represents the status of the thread. We need to know whether a thread
 * is profiled, stopped, of still running from the profiler point of view.
 */
typedef enum GCMMPThreadProfStateT {
	GCMMP_TH_ALLOC = 0,
	GCMMP_TH_STARTING,
	GCMMP_TH_RUNNING,
	GCMMP_TH_STOPPED
} GCMMPThreadProfState;

/*
 * Flags used to check the functionality of the mprofiler->flags_
 */
typedef enum GCMMPFlagsEnum_s {
	GCMMP_FLAGS_NONE = 0,
	GCMMP_FLAGS_CREATE_DAEMON = 1, //should we create a daemon profiler?
	GCMMP_FLAGS_HAS_DAEMON = 2,		 //does it possess a daemon thread
	GCMMP_FLAGS_MARK_ALLOC_WINDOWS = 4 //should we mark the allocation chunks
} GCMMPFlagsEnum;

/*
 * Struct used to hold the temporary values when we enter the block of an
 * activity. When we exit we add the delta to the accumulated field.
 */
typedef struct GCMMP_ProfileActivity_S {
	/* Used as a temp to hold the counter vaue when we enter the activity block */
	uint64_t startMarker;
	/* represents the total value of the activity */
	uint64_t finalMarker;
} GCMMP_ProfileActivity;

/*
 * Struct used to hold the temporary values when we enter the block of an
 * activity. When we exit we add the delta to the accumulated field.
 */
typedef struct GCPauseThreadMarker_S {
	/* Used as a temp to hold the counter vaue when we enter the activity block */
	uint64_t startMarker;
	/* represents the total value of the activity */
	uint64_t finalMarker;
	/* the type of the pause */
	GCMMP_BREAK_DOWN_ENUM type;
} GCPauseThreadMarker;


class GCPauseThreadManager {
	 GCMMP_ProfileActivity marker;
	 GCPauseThreadMarker* pauseEvents[GCMMP_GCPAUSE_ARRAY_SIZE];
	 int currentEntry;
	 int currEventsIndex;
	 int eventsCount;
};

/*
 * Holds the profiling data per thread . We do not keep a pointer to the thread
 * because threads may terminate before we collect the information
 */
class GCMMPThreadProf {

	/* system ID of the thread monitored */
	pid_t pid;

	/* markers used to set the temporary information to start an event */
	GCMMP_ProfileActivity timeBrks[GCMMP_GC_BRK_MAXIMUM];

	volatile GCMMPThreadProfState state;

	volatile bool suspendedGC;

	GCPauseThreadManager* pauseManager;

};

class MProfiler {
private:
	//Index of the profiler type we are running
	const unsigned int index_;
  // System thread used for the profiling (profileDaemon).
	Thread* prof_thread_;
  // System thread used as main (thread id = 1).
	Thread* main_thread_;
  // System thread used as GC Daemon (thread id = 1).
	Thread* gc_daemon_;

  // flags of the Profiler we are running
  // combines markAllocWindows, createProfDaemon, hasProfThread,
  const unsigned int flags_;

  /* file used to dump the profiling data */
  const char * dump_file_name_;
  UniquePtr<File> dump_file_;
  /*
   * Guards access to the state of the profiler daemon,
   * associated conditional variable is used to signal when a GC completes
   */
  Mutex* prof_thread_mutex_;
  UniquePtr<ConditionVariable> prof_thread_cond_ GUARDED_BY(prof_thread_mutex_);

	/* array of thread records used to keep the data per thread */
	GCMMPThreadProf* thread_recs_;

  void CreateProfilerDaemon(void);

  void InitializeProfiler(void);

  void DumpProfData(void);

  void InitDumpFile(void);

  /* Utilities */
  int GetBenchmarksCount() const {
    return (GCMMP_ARRAY_SIZE(benchmarks));
  }

  /* Utilities */

  bool IsProfilingRunning() const {
    return false;
  }

public:
	static constexpr int kGCMMPDumpSignal = SIGUSR2;
	static const unsigned int kGCMMPEnableProfiling = 0;
	// List of profiled benchmarks in our system
	static const char *benchmarks[];
	/*
	 * list of the directory paths to save the logs
	 */
	static const char *gcMMPRootPath[];

	MProfiler(void);

	~MProfiler();

	//bool VerifyProcessName();

	void ProcessSignal(int);

	void PreForkPreparation(void);

  bool IsProfilingEnabled() const {
    return false;
  }

  void GCMMProfPerfCounters(const char*);
}; //class MProfiler

}  // namespace mprofiler
}  // namespace gc

#endif /* MPROFILER_H_ */
