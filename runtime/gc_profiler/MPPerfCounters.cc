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

int init_perflib_counters(void) {
	return 0;
}

/*
 * Open perflib and process ID
 */
bool GCMMPperfLibOpen(PerfLibCounterT* prfRec, pid_t pid) {
	int _locRet = 0;
	bool _lResult = true;
	art::Thread* self = art::Thread::Current();
	_locRet = create_perf_counter(prfRec);

	return false;
}
