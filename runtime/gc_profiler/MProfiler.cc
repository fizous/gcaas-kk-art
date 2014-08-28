/*
 * MPprofiler.cc
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#include <string>

#include "gc/heap.h"
#include "os.h"
#include "locks.h"
#include "cutils/sched_policy.h"
#include "cutils/process_name.h"
#include <cutils/trace.h>

#include "thread_list.h"


#include "gc_profiler/MProfiler.h"

namespace art {
namespace mprofiler {

// Member functions definitions including constructor
MProfiler::MProfiler(void)
		: index_(0),
		prof_thread_(NULL),
		main_thread_(NULL),
		gc_daemon_(NULL),
		thread_recs_(NULL)
{
	prof_thread_mutex_ = new Mutex("MProfile Thread lock");
	prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
                                                *prof_thread_mutex_));
	LOG(INFO) << "MProfiler Created";
}


void MProfiler::dvmGCMMProfPerfCounters(const char* name) {
	if(IsProfilingEnabled()){
		for (u4 i = 0; i < GetBenchmarksCount(); i++) {
			if (std::strcmp(name, benchmarks[i].c_str()) == 0) {
				LOG(INFO) << "MProfiler found a target VM " << name;
				return;
			}
		}
	}
	LOG(INFO) << "MProfiler did not find a target VM for " << name;
}

void MProfiler::PreForkPreparation() {
	dvmGCMMPSetName = dvmGCMMProfPerfCounters;
}
}// namespace mprofiler
}// namespace art

