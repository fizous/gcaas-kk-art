/*
 * MProfilerTypes.cc
 *
 *  Created on: Sep 11, 2014
 *      Author: hussein
 */


#include "gc_profiler/MProfilerTypes.h"
#include "thread.h"

namespace art {
namespace mprofiler {


const GCMMPThreadProfiling& GCMMPThreadProfiling::defaultTHProfiler =
		GCMMPThreadProfiling::GCMMPThreadProfiling();


void GCMMPThreadProfiling::SetMProfiler(MProfiler& mProf) {
	GCMMPThreadProfiling::mProfiler = mProf;
}

GCMMPThreadProfiling::GCMMPThreadProfiling(void)
 : pid_(0),
   suspendedGC_(false),
   state_(GCMMP_TH_STOPPED){

	lifeTime_.startMarker = 0;
	lifeTime_.finalMarker = 0;

	GCMMP_VLOG(INFO) << "GCMMPThreadProfiling is initialized with id=" << pid_;
}



GCMMPThreadProfiling::GCMMPThreadProfiling(Thread& thread)
 : pid_(thread.GetTid()),
   suspendedGC_(false),
   state_(GCMMP_TH_STARTING){

	lifeTime_.startMarker = GCMMPThreadProf::mProfiler->GetRelevantCPUTime();
	lifeTime_.finalMarker = 0;

	InitThreadRecord();


	GCMMP_VLOG(INFO) << "GCMMPThreadProfiling is initialized with id=" << pid_;
}



void GCMMPThreadProfiling::InitThreadRecord(void) {
	InitDataStucture();
	state_ = GCMMP_TH_RUNNING;
}
void GCMMPThreadProfiling::InitDataStucture(void) {

}


}//namespace mprofiler
}// namespace art

class GCMMPThreadProfiling {
protected:
	/* system ID of the thread monitored */
	const pid_t pid_;
	volatile bool suspendedGC_;
	GCMMP_ProfileActivity lifeTime_;
	volatile GCMMPThreadProfState state_;
	static MProfiler& mProfiler;

  bool StopProfiling(void);
  void ForceDeadTime(void);

public:
	GCMMPThreadProfiling(MProfiler*, Thread*);

	int GetThreadType(void);

  uint64_t GetCreationTime(void) const;

  uint64_t GetEndTime(void) const;

  GCMMP_ProfileActivity* GetliveTimeInfo(void) const;

};
