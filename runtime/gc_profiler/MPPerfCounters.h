/*
 * MPPerfCounters.h
 *
 *  Created on: Sep 9, 2014
 *      Author: hussein
 */

#ifndef MPPERFCOUNTERS_H_
#define MPPERFCOUNTERS_H_

#include <stdint.h>
#include "cutils/perflib.h"


namespace art {
namespace mprofiler {

class MPPerfCounter {
public:
	static const bool kGCPerfCountersExcIdle = true;
	static const int 	kGCPerfCountersNameSize = 16;
	const char* event_name_;

	MPPerfCounter(void);
	/*
	 * Initiliazes performance library counters
	 */
	int InitPerflib(void) {
		return init_perflib_counters();
	}

	/*
	 * terminates performance library counters
	 */
	int TerminatePerflib(void) {
		return terminate_perflib_counters();
	}

	/*
	 * Open perflib and process ID
	 */
	bool OpenPerfLib(PerfLibCounterT*, pid_t);


  static MPPerfCounter* Create(const char* event_name);
};

} //namespace mprofiler
} //namespace art

#endif /* MPPERFCOUNTERS_H_ */
