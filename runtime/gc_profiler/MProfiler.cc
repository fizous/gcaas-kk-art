/*
 * MPprofiler.cc
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#include <string>
#include "runtime.h"
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

const char * MProfiler::benchmarks[] = {
			"com.aurorasoftworks.quadrant.ui.professional",
			"purdue.specjvm98",
			"purdue.dacapo",
			"com.antutu.ABenchMark",
			"com.android.cm3",
			"purdue.gcbench",
			"com.pandora.android"
			//"com.android.systemui"  //we can add this to the profiled targets
			//"com.android.launcher" // the problem with this service is its lack of permissions to access Sdcard
};

const char * MProfiler::gcMMPRootPath[] = {
		"/sdcard/gcperf/", "/data/anr/"
};
// Member functions definitions including constructor
MProfiler::MProfiler(void)
		: index_(0),
		prof_thread_(NULL),
		main_thread_(NULL),
		gc_daemon_(NULL),
		flags_(0),
		dump_file_name_(NULL),
		dump_file_(NULL),
		thread_recs_(NULL)
{
	if(IsProfilingEnabled()) {
		prof_thread_mutex_ = new Mutex("MProfile Thread lock");
		prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
																									*prof_thread_mutex_));

		LOG(INFO) << "MProfiler Profiling is Disabled";
	}
	LOG(INFO) << "MProfiler Created";
}

MProfiler::~MProfiler() {
  delete prof_thread_mutex_;

}
void MProfiler::GCMMProfPerfCounters(const char* name) {
	if(IsProfilingEnabled()){
		for (size_t i = 0; i < GetBenchmarksCount(); i++) {
			if (strcmp(name, benchmarks[i]) == 0) {
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

void dvmGCMMProfPerfCounters(const char* vmName){
	art::mprofiler::MProfiler* mProfiler =
			art::Runtime::Current()->GetMProfiler();
	mProfiler->GCMMProfPerfCounters(vmName);
}
