#ifndef SYSLOG_DEFS_H_
#define SYSLOG_DEFS_H_

/*********************************************************************
 ** Syslog Facility                                                 **
 *********************************************************************/

typedef enum SyslogFacility
{
    kNoFacility,
    kKernelFacility,
    kUserFacility,
    kMailFacility,
    kDaemonFacility,
    kAuthFacility,
    kSyslogFacility,
    kLPRFacility,
    kNewsFacility,
    kUUCPFacility,
    kCronFacility,
    kAuthPrivateFacility,
    kFTPFacility,
    kLocal0Facility,
    kLocal1Facility,
    kLocal2Facility,
    kLocal3Facility,
    kLocal4Facility,
    kLocal5Facility,
    kLocal6Facility,
    kLocal7Facility,
} SyslogFacility;

#endif /* SYSLOG_DEFS_H_ */
