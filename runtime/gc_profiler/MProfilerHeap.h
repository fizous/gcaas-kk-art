/*
 * MProfilerHeap.h
 *
 *  Created on: Jun 21, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_
#define ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_

//#if ART_USE_GC_PROFILER
//#define DVM_ALLOW_GCPROFILER			1
//#else
//#define DVM_ALLOW_GCPROFILER			0
//#endif

/******************************************************************************
 * 																Macro Definitions
 *
 *
 ******************************************************************************/
/** define the type of the collection **/




//#if DVM_ALLOW_GCPROFILER
//#define GCP_DECLARE_ADD_ALLOC(x,y)			  (gcpAddObject(x,y))
//#define GCP_DECLARE_REMOVE_ALLOC(x,y)			(gcpRemoveObject(x,y))
//#define GCP_DECLARE_ADD_PRECISE_ALLOC(x,y,z)
//
//#else//if DVM_ALLOW_GCPROFILER
//
//#define GCP_DECLARE_ADD_ALLOC(x,y)			((void) 0)
//#define GCP_DECLARE_REMOVE_ALLOC(x,y)		((void) 0)
//#define GCP_DECLARE_ADD_PRECISE_ALLOC(x,y,z) ((void) 0)
//#define GCP_PROFILE_OBJ_CLASS(klass, obj) 										((void) 0)
//
//#endif//if DVM_ALLOW_GCPROFILER




#endif /* ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_ */
