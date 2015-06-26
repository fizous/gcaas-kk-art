/*
 * MProfilerHeap.h
 *
 *  Created on: Jun 21, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_
#define ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_

#if ART_USE_GC_PROFILER
#define DVM_ALLOW_GCPROFILER			1
#else
#define DVM_ALLOW_GCPROFILER			0
#endif

/******************************************************************************
 * 																Macro Definitions
 *
 *
 ******************************************************************************/
/** define the type of the collection **/
#define GCP_DISABLE_CONC_COLLECT					1 /* turn off ConcurrentGC */
#define GCP_DISABLE_EXPL_COLLECT					1 /* turn off explicit GC */
#define GCP_COLLECT_FOR_PROFILE					  1 /* collect on each allocation window */


#define GCP_MAX_COHORT_ARRAYLET_SIZE			128
#define GCP_MAX_COHORT_ROW_SIZE						64
#define GCP_COHORT_LOG										18
#define GCP_COHORT_SIZE										262144 // 256k


#if DVM_ALLOW_GCPROFILER
#define GCP_DECLARE_ADD_ALLOC(x,y)			  (gcpAddObject(x,y))
#define GCP_DECLARE_REMOVE_ALLOC(x,y)			(gcpRemoveObject(x,y))
#define GCP_DECLARE_ADD_PRECISE_ALLOC(x,y,z) (gcpAddObject(x,y,z))
#else//if DVM_ALLOW_GCPROFILER

#define GCP_DECLARE_ADD_ALLOC(x,y)			((void) 0)
#define GCP_DECLARE_REMOVE_ALLOC(x,y)		((void) 0)
#define GCP_DECLARE_ADD_PRECISE_ALLOC(x,y,z) ((void) 0)

#endif//if DVM_ALLOW_GCPROFILER


#define GCP_OFF_CONCURRENT_GC()			(GCP_DISABLE_CONC_COLLECT & DVM_ALLOW_GCPROFILER)
#define GCP_OFF_EXPLICIT_GC()			(GCP_DISABLE_EXPL_COLLECT & DVM_ALLOW_GCPROFILER)

#endif /* ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_ */
