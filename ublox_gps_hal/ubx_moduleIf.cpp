/*******************************************************************************
 *
 * Copyright (C) u-blox AG 
 * u-blox AG, Thalwil, Switzerland
 *
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose without fee is hereby granted, provided that this entire notice
 * is included in all copies of any software which is or includes a copy
 * or modification of this software and in all copies of the supporting
 * documentation for such software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTY. IN PARTICULAR, NEITHER THE AUTHOR NOR U-BLOX MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY
 * OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 *******************************************************************************
 *
 * Project: PE_ANS
 *
 ******************************************************************************/
/*!
  \file
  \brief  GPS main function

  Module for framework interface definition
*/
/*******************************************************************************
 * $Id: ubx_moduleIf.cpp 65270 2013-01-25 08:42:48Z andrea.foni $
 ******************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#ifndef ANDROID_BUILD
// Needed for Linux build
#include <malloc.h>
#endif

#include "std_types.h"
#include "ubx_log.h"

//#include "ubx_debugIf.h"
#ifdef SUPL_ENABLED
 #include "ubx_rilIf.h"
 #include "ubx_niIf.h"
 #include "ubx_agpsIf.h"
#endif
#include "ubx_xtraIf.h"
#include "ubx_moduleIf.h"

#include "gps_thread.h"

static ControlThreadInfo  s_controlThreadInfo;
static pthread_t s_mainControlThread = (pthread_t)NULL;

#if (PLATFORM_SDK_VERSION > 8 /* >2.2 */)
/*******************************************************************************
 * HAL MODULE
 ******************************************************************************/

hw_module_t HAL_MODULE_INFO_SYM = { // hardware/libhardware/include/hardware/hardware.h
	tag:					HARDWARE_MODULE_TAG,        // uint32_t
    version_major:			2,                          // uint16_t
    version_minor:			0,                          // uint16_t
    id:						GPS_HARDWARE_MODULE_ID,     // const char *
    name:					"u-blox GPS/GNSS library",  // const char *
    author:					"u-blox AG - Switzerland",  // const char *
    methods:				&CGpsIf::s_hwModuleMethods, // struct hw_module_methods_t *
    dso:					NULL,                       // module's dso
    reserved:				{0}                         // uint32_t *, padding
};

struct hw_module_methods_t CGpsIf::s_hwModuleMethods =
{
    open: CGpsIf::hwModuleOpen // open a specific device
};

int CGpsIf::hwModuleOpen(const struct hw_module_t* module, 
						 char const* name, struct hw_device_t** device)
{
    ((void) (name));
    struct gps_device_t *dev = new gps_device_t;
    memset(dev, 0, sizeof(*dev));
    dev->common.tag			= HARDWARE_DEVICE_TAG;
    dev->common.version		= 0;
    dev->common.module		= const_cast<struct hw_module_t*>(module);
    dev->common.close		= CGpsIf::hwModuleClose;
    dev->get_gps_interface	= CGpsIf::getIf;
    *device = (struct hw_device_t*) (void *) dev;

    return 0;
}

int CGpsIf::hwModuleClose(struct hw_device_t* device)
{
    delete device;
    return 0;
}
#else // (PLATFORM_SDK_VERSION <= 8 /* <=2.2 */)
const GpsInterface* gps_get_interface()
{
	return CGpsIf::getIf(NULL);
}
#endif

/*******************************************************************************
 * INTERFACE
 ******************************************************************************/

static CGpsIf s_myIf;

const GpsInterface CGpsIf::s_interface = {
    IF_ANDROID23( size:				sizeof(GpsInterface), )
    init:                   		CGpsIf::init,
    start:                  		CGpsIf::start,
    stop:                   		CGpsIf::stop,
#if (PLATFORM_SDK_VERSION <= 8)
	set_fix_frequency:				CGpsIf::setFixFrequency,
#endif
    cleanup:                		CGpsIf::cleanup,
    inject_time:            		CGpsIf::injectTime,
    IF_ANDROID23( inject_location:	CGpsIf::injectLocation, )
    delete_aiding_data:				CGpsIf::deleteAidingData,
    set_position_mode:				CGpsIf::setPositionMode,
    get_extension:					CGpsIf::getExtension,
};

CGpsIf::CGpsIf()
{
	m_ready = false;
	m_mode = GPS_POSITION_MODE_MS_BASED;
	m_lastStatusValue = GPS_STATUS_NONE;
    m_capabilities = 0;
    memset(&m_callbacks,0,sizeof(m_callbacks));
}

CGpsIf* CGpsIf::getInstance()
{
	return &s_myIf;
}

#if (PLATFORM_SDK_VERSION <= 8)
extern "C" void* CGpsIfThread22(void *info)
{
    ubx_thread(info);
    pthread_exit(NULL);
    return NULL;
}
#endif

int CGpsIf::init(GpsCallbacks* callbacks)
{
    if (s_myIf.m_ready)
	{
        LOGE("CGpsIf::%s : already initialized", __FUNCTION__);
		return 0;	// Report success since we are already initialised
	}
	
 #if (PLATFORM_SDK_VERSION > 8 /* >2.2 */)
 LOGV("CGpsIf::%s :", __FUNCTION__);
	memcpy(&s_myIf.m_callbacks, callbacks, 
				(callbacks->size < sizeof(GpsCallbacks)) ? callbacks->size : sizeof(GpsCallbacks));
	if (callbacks->size != sizeof(GpsCallbacks))
		LOGW("CGpsIf::%s : callback size %zd != %zd", __FUNCTION__, callbacks->size, sizeof(GpsCallbacks));
#endif
	
	controlThreadInfoInit(&s_controlThreadInfo);
    
	LOGD("CGpsIf::%s (%u): Initializing - pid %i", __FUNCTION__, (unsigned int) pthread_self(), getpid());
	
#if (PLATFORM_SDK_VERSION > 8 /* >2.2 */)
 	s_myIf.m_capabilities = GPS_CAPABILITY_SCHEDULING;
 #if (PLATFORM_SDK_VERSION >= 14 /* >=4.0 */)
	s_myIf.m_capabilities |= GPS_CAPABILITY_ON_DEMAND_TIME;
 #endif
 #ifdef SUPL_ENABLED
	s_myIf.m_capabilities |=  GPS_CAPABILITY_MSB | GPS_CAPABILITY_MSA;
 #endif
	LOGV("CGpsIf::%s : set_capabilities=%d(%s)", __FUNCTION__, s_myIf.m_capabilities, _LOOKUPSTRX(s_myIf.m_capabilities, GpsCapabilityFlags));
	s_myIf.m_callbacks.set_capabilities_cb(s_myIf.m_capabilities);
    s_mainControlThread = s_myIf.m_callbacks.create_thread_cb("gps thread", ubx_thread, &s_controlThreadInfo);
#else // (PLATFORM_SDK_VERSION <= 8 /* <=2.2 */)
     /* do somthing here */
    s_mainControlThread = (pthread_t)NULL;
    pthread_create(&s_mainControlThread, NULL, CGpsIfThread22, &s_controlThreadInfo);
#endif
    pthread_cond_wait(&s_controlThreadInfo.threadCmdCompleteCond,
                      &s_controlThreadInfo.threadCmdCompleteMutex);
    s_myIf.m_ready = (s_controlThreadInfo.cmdResult != 0);
	
	LOGD("CGpsIf::%s Initialized complete: result %i", __FUNCTION__, s_myIf.m_ready);
    if (!s_myIf.m_ready)
    {
        // Init failed -  release resources
		controlThreadInfoRelease(&s_controlThreadInfo);
    }
	gpsStatus(GPS_STATUS_ENGINE_OFF);
	return s_myIf.m_ready  ? 0 : 1;
//lint -e{818} remove  Pointer parameter 'callbacks' (line 130) could be declared as pointing to const
}

int CGpsIf::start(void)
{
    LOGV("CGpsIf::%s (%u):", __FUNCTION__, (unsigned int) pthread_self());
	
	if (s_myIf.m_ready)
	{
		return controlThreadInfoSendCmd(&s_controlThreadInfo, CMD_START_SI) ? 0 : 1;
	}

	LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
	return 1;
}

int CGpsIf::stop(void)
{
    LOGV("CGpsIf::%s (%u):", __FUNCTION__, (unsigned int) pthread_self());
	
	if (s_myIf.m_ready)
	{
		return controlThreadInfoSendCmd(&s_controlThreadInfo, CMD_STOP_SI) ? 0 : 1;
	}

	LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
	return 1;
}

void CGpsIf::cleanup(void)
{
    LOGD("CGpsIf::%s (%u):", __FUNCTION__, (unsigned int) pthread_self()); 
	
	if (s_myIf.m_ready)
	{
		controlThreadInfoSendCmd(&s_controlThreadInfo, CMD_STOP_SI);
	}
	else
	{
		LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
	}
}

int CGpsIf::injectTime(GpsUtcTime timeGpsUtc, int64_t timeReference, int uncertainty)
{
	time_t tUtc = (long) (timeGpsUtc/1000);
	char s[20];
	struct tm t;
	strftime(s, 20, "%Y.%m.%d %H:%M:%S", gmtime_r(&tUtc, &t));
	
	LOGV("CGpsIf::%s : timeGpsUtc=%s.%03d timeReference=%lli uncertainty=%.3f ms", 
				__FUNCTION__, s, (int)(timeGpsUtc%1000), timeReference,uncertainty*0.001);
	
	if (s_myIf.m_ready)
	{
		gps_state_inject_time(timeGpsUtc, timeReference, uncertainty);
		return 0;
	}

	LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
    return 1;
}

int CGpsIf::injectLocation(double latitude, double longitude, float accuracy)
{
    LOGV("CGpsIf::%s : latitude=%.6f longitude=%.6f accuracy=%.2f", 
				__FUNCTION__, latitude, longitude, accuracy);
	if (s_myIf.m_ready)
	{
		gps_state_inject_location(latitude, longitude, accuracy);
		return 0;
	}
	
	LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
    return 1;
}

void CGpsIf::deleteAidingData(GpsAidingData flags)
{
    LOGD("CGpsIf::%s : flags=0x%X", __FUNCTION__, flags);
	if (s_myIf.m_ready)
	{
		gps_state_delete_aiding_data(flags);
	}
	else
	{
		LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
	}	
}

#if (PLATFORM_SDK_VERSION <= 8 /* <=2.2 */)
void CGpsIf::setFixFrequency(int frequency)
{
	LOGD("CGpsIf::%s (%u): frequency=%i", 
			__FUNCTION__,
			(unsigned int) pthread_self(),
            frequency); 
	if (s_myIf.m_ready)
	{
		frequency = frequency ? frequency : 1000;
		gps_state_set_interval(frequency);
	}
	else
	{
		LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
	}
}

int CGpsIf::setPositionMode(GpsPositionMode mode, int fix_frequency)
{
	LOGD("CGpsIf::%s (%u): mode=%i(%s) fix_frequency=%i", 
			__FUNCTION__,
			(unsigned int) pthread_self(),
            mode, _LOOKUPSTR(mode, GpsPositionMode),
            fix_frequency); 
	int min_interval = fix_frequency;
#else // (PLATFORM_SDK_VERSION > 8 /* >2.2 */)
int CGpsIf::setPositionMode(GpsPositionMode mode, GpsPositionRecurrence recurrence,
			uint32_t min_interval, uint32_t preferred_accuracy, uint32_t preferred_time)
{
	LOGD("CGpsIf::%s (%u): mode=%i(%s) recurrence=%i(%s) min_interval=%u preferred_accuracy=%u preferred_time=%u", 
			__FUNCTION__,
			(unsigned int) pthread_self(),
            mode, _LOOKUPSTR(mode, GpsPositionMode),
            recurrence, _LOOKUPSTR(recurrence, GpsPositionRecurrence),
            min_interval,
            preferred_accuracy,
            preferred_time); 
#endif
	if (s_myIf.m_ready)
	{
		s_myIf.m_mode = mode;
		
		min_interval = min_interval ? min_interval : 1000;
		gps_state_set_interval(min_interval);
	}
	else
	{
		LOGE("CGpsIf::%s : Not initialised", __FUNCTION__);
	}
	
    return 0;
}

const void* CGpsIf::getExtension(const char* name)
{
    LOGD("%s : name='%s'", __FUNCTION__, name); 
#if defined CDEBUGIF_EN
    if (!strcmp(name, GPS_DEBUG_INTERFACE))	return CDebugIf::getIf();
#endif
#ifdef SUPL_ENABLED
	if (!strcmp(name, AGPS_INTERFACE))		return CAgpsIf::getIf();
    if (!strcmp(name, AGPS_RIL_INTERFACE))	return CRilIf::getIf();
    if (!strcmp(name, GPS_NI_INTERFACE))	return CNiIf::getIf();
#endif
    if (!strcmp(name, GPS_XTRA_INTERFACE))	return CXtraIf::getIf();
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// operations

void CGpsIf::gpsStatus(GpsStatusValue gpsStatusValue)
{
	if (!s_myIf.m_ready)
    {
        LOGE("CGpsIf::%s: class not initialized", __FUNCTION__);
        return;
    }
	LOGV("CGpsIf::%s: gpsStatusValue=%d(%s)", __FUNCTION__, 
				gpsStatusValue, _LOOKUPSTR(gpsStatusValue, GpsStatusValue));
	if (gpsStatusValue != s_myIf.m_lastStatusValue)
	{
		s_myIf.m_lastStatusValue = gpsStatusValue;
		if (gpsStatusValue == GPS_STATUS_SESSION_END)
		{
			GpsSvStatus svStatus;
			memset(&svStatus, 0, sizeof(GpsSvStatus));
			IF_ANDROID23( svStatus.size = sizeof(GpsSvStatus); )
			s_myIf.m_callbacks.sv_status_cb(&svStatus);
		}
		GpsStatus gpsStatusVar;
		IF_ANDROID23( gpsStatusVar.size = sizeof(gpsStatusVar); )
		gpsStatusVar.status = gpsStatusValue;
		s_myIf.m_callbacks.status_cb(&gpsStatusVar);
	}
}

#if (PLATFORM_SDK_VERSION > 8 /* >2.2 */)
void CGpsIf::nmea(const char* data, int length)
{
	if (!s_myIf.m_ready)
    {
        LOGE("CGpsIf::%s: class not initialized", __FUNCTION__);
        return;
    }
	if (s_myIf.m_callbacks.nmea_cb != NULL)
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		GpsUtcTime gpsUtcTime = (long long) tv.tv_sec * 1000 + (long long) tv.tv_usec / 1000;
		s_myIf.m_callbacks.nmea_cb(gpsUtcTime, data, length);
	}
}
#endif

#if (PLATFORM_SDK_VERSION >= 14 /* =4.0 */)
void CGpsIf::requestUtcTime(void)
{
	if (!s_myIf.m_ready)
    {
        LOGE("CGpsIf::%s: class not initialized", __FUNCTION__);
        return;
    }
	if (s_myIf.m_callbacks.request_utc_time_cb != NULL)
	{
		LOGV("CGpsIf::%s : ", __FUNCTION__);
		s_myIf.m_callbacks.request_utc_time_cb();
	}
}
#endif

#ifdef CONTROL_PLANE_AGPS

void CGpsIf::agpsAssistReport(double lat, double lon, double alt, double acc)
{
	if (!s_myIf.m_ready)
    {
        LOGE("CRilIf::%s: class not initialized", __FUNCTION__);
        return;
    }
	if (s_myIf.m_callbacks.agps_assist_cb)
	{
		const double pi = 3.1415926535898;
		agps_report_msg_t report; 
		memset(&report, 0, sizeof(report));

		//report.result = 
		//report.ep_type = 
		//report.calc_3d_positon = 
		// report.gps_tow_msec = 
		report.lat_sign = (lat < 0) ? 1 : 0;
		lat = (lat < 0) ? -lat : lat;
		report.latitude  = lat / 90.0 * (1<<23);
		report.longitude = lon / 360.0 * (1<<24);
		int uncert_code = (int)(log(acc / 10 + 1) / log(1.1));
		if (uncert_code > 127) uncert_code = 127;
		report.uncert_smi = report.uncert_sma = uncert_code;
		report.confidence = 66;
		//report.orientation = 
		report.alt_dir = (alt < 0) ? 1 : 0;
		alt = (alt < 0) ? -alt : alt;
		if (alt > 0x7FFF) alt = 0x7FFF;
		report.alt = alt;
		int uncert_alt = (int)(log(acc / 45 + 1) / log(1.0 + 0.025));
		if (uncert_alt > 127) uncert_alt = 127;
		report.uncert_alt = uncert_alt; 
		
		LOGV("CRilIf::%s: lat=%.6f lon=%.6f alt=%.1f acc=%.2f %d %d ", __FUNCTION__, 
					lat, lon, alt, acc, uncert_code, uncert_alt);
		// report.measurement_paramlist ...
		s_myIf.m_callbacks.agps_assist_cb(0,(const char*)&report,sizeof(report));
	}
}

void CGpsIf::agpsAssistReportFail(const agps_report_fail_msg_t* data)
{
	if (!s_myIf.m_ready)
    {
        LOGE("CRilIf::%s: class not initialized", __FUNCTION__);
        return;
    }
	LOGV("CRilIf::%s : ", __FUNCTION__);
	if (s_myIf.m_callbacks.agps_assist_cb)
	{
		s_myIf.m_callbacks.agps_assist_cb(1,(const char*)data,sizeof(*data));
	}
}

#endif

///////////////////////////////////////////////////////////////////////////////
// Debug / Testing support

#ifndef ANDROID_BUILD

extern "C" void endControlThread(void)
{
    LOGD("CGpsIf::%s : Send thread exit command", __FUNCTION__);
    bool ok = controlThreadInfoSendCmd(&s_controlThreadInfo, CMD_EXIT);
    pthread_join(s_mainControlThread, NULL);
	s_mainControlThread = NULL;
    LOGD("CGpsIf::%s : Thread exited ok=%d", __FUNCTION__, ok);
}
#endif

