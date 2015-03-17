/*
 * MPPerfCounters.cc
 *
 *  Created on: Sep 9, 2014
 *      Author: hussein
 */


#include "cutils/perflib.h"
#include "os.h"
#include "runtime.h"
#include "thread.h"
#include "gc_profiler/MPPerfCounters.h"
#include "gc_profiler/MProfiler.h"


namespace art {
namespace mprofiler {



MPPerfCounter::MPPerfCounter(void) :
		event_name_("CYCLES") {

}

MPPerfCounter::MPPerfCounter(const char* event_name)  {
	event_name_ = event_name;
}

MPPerfCounter* MPPerfCounter::Create(const char* event_name){
	return new MPPerfCounter(event_name);
}


/*
 * Open perflib and process ID
 */
bool MPPerfCounter::OpenPerfLib(PerfLibCounterT* prfRec, pid_t pid) {
	int _locRet = 0;
	//art::Thread* self = art::Thread::Current();

	GCMMP_VLOG(INFO) << "MPPerfCounters: openPerfLib for event:" << event_name_;
	prfRec->event_name = NULL;
	prfRec->event_name =
			(char*) calloc(1, sizeof(char) * MPPerfCounter::kGCPerfCountersNameSize);
	strcpy(prfRec->event_name, event_name_);

	_locRet = create_perf_counter(prfRec);

	if (_locRet < 0) {
		LOG(FATAL) << "could not create perflib for tid: " << pid;
		return false;
	}
	prfRec->pid = pid;
	_locRet = open_perf_counter(prfRec);
	if (_locRet < 0) {
		LOG(FATAL) << "could not open perflib for tid: " << pid;
		return false;
	}
	set_exclude_idle(prfRec, MPPerfCounter::kGCPerfCountersExcIdle);

	GCMMP_VLOG(INFO) << "MPPerfCounters: Finished creating the performance counters for tid:" << pid;
	return true;
}
}// namespace mprofiler
}// namespace art
