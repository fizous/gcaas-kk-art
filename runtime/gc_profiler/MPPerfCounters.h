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


/*
 * Initiliazes performance library counters
 */
int GCMMPInitPerflib(void) {
	return init_perflib_counters();
}

/*
 * terminates performance library counters
 */
int GCMMPTerminatePerflib(void)
{
	return terminate_perflib_counters();
}

/*
 * Open perflib and process ID
 */
bool GCMMPperfLibOpen(PerfLibCounterT*, pid_t);


#endif /* MPPERFCOUNTERS_H_ */
