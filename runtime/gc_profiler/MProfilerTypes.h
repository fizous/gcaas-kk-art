/*
 * MProfilerTypes.h
 *
 *  Created on: Aug 29, 2014
 *      Author: hussein
 */

#ifndef MPROFILERTYPES_H_
#define MPROFILERTYPES_H_

#define GCMMP_GCPAUSE_ARRAY_SIZE					  32

namespace art {

namespace mprofiler {



typedef struct GCMMP_Options_s {
	int mprofile_type_;
	int mprofile_grow_method_;
}GCMMP_Options;

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

class MProfiler;
/*
 * Holds the profiling data per thread . We do not keep a pointer to the thread
 * because threads may terminate before we collect the information
 */
class GCMMPThreadProf {

	/* system ID of the thread monitored */
	const pid_t pid;

	/* markers used to set the temporary information to start an event */
	GCMMP_ProfileActivity timeBrks[GCMMP_GC_BRK_MAXIMUM];

	volatile GCMMPThreadProfState state;

	volatile bool suspendedGC;

	GCPauseThreadManager* pauseManager;

public:
	GCMMPThreadProf(MProfiler*, Thread*);
	~GCMMPThreadProf(void);

  void Destroy(MProfiler*);

  pid_t GetTid() const {
    return pid;
  }

  bool StopProfiling(void);
};
} // namespace mprofiler
} // namespace art

#endif /* MPROFILERTYPES_H_ */
