#ifndef PD_COMMUNICATION_H
#define PD_COMMUNICATION_H

/*
 * Small convenience wrapper around the TRDP Process Data (PD) API.
 *
 * A PD_COMM_T represents one TRDP session and can own either a publisher or
 * subscriber.  PD is specified as UDP/IP in TRDP; consequently use_udp must
 * be TRUE.  Passing FALSE to pd_comm_config_set_udp() returns
 * TRDP_PARAM_ERR instead of silently pretending that TCP is supported.
 */

#if !defined(WIN32) && !defined(WIN64)
#include <sys/select.h>
#include <netinet/in.h>
#endif

#include "trdp_if_light.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PD_COMM_DEFAULT_CYCLE_US   1000000u
#define PD_COMM_DEFAULT_TIMEOUT_US 3000000u

typedef struct
{
    UINT32          comid;
    BOOL8           use_udp;
    TRDP_IP_ADDR_T  destination_ip;
    TRDP_IP_ADDR_T  source_ip;
    UINT32          cycle_us;
    UINT32          timeout_us;
} PD_COMM_CONFIG_T;

typedef struct
{
    TRDP_APP_SESSION_T  session;
    TRDP_PUB_T          publisher;
    TRDP_SUB_T          subscriber;
    PD_COMM_CONFIG_T    config;
    BOOL8               initialized;
    BOOL8               publisher_open;
    BOOL8               subscriber_open;
} PD_COMM_T;

/* Initialise config with UDP and automatic source/interface address. */
void pd_comm_config_init (PD_COMM_CONFIG_T *config);

/* Set the four connection parameters requested by applications. */
void       pd_comm_config_set_comid (PD_COMM_CONFIG_T *config, UINT32 comid);
TRDP_ERR_T pd_comm_config_set_udp (PD_COMM_CONFIG_T *config, BOOL8 use_udp);
TRDP_ERR_T pd_comm_config_set_destination_ip (PD_COMM_CONFIG_T *config, const CHAR8 *address);
TRDP_ERR_T pd_comm_config_set_source_ip (PD_COMM_CONFIG_T *config, const CHAR8 *address);

/* Open and close the underlying TRDP session. Only one wrapper may be open
 * at a time because tlc_init()/tlc_terminate() are process-global in TRDP. */
TRDP_ERR_T pd_comm_open (PD_COMM_T *communication, const PD_COMM_CONFIG_T *config);
void       pd_comm_close (PD_COMM_T *communication);

/* Select one role for the opened session, then call pd_comm_process()
 * regularly. destination_ip is the peer address for publishing and the
 * multicast group/default destination for subscribing. */
TRDP_ERR_T pd_comm_open_publisher (PD_COMM_T *communication,
                                   const UINT8 *initial_data,
                                   UINT32 initial_size);
TRDP_ERR_T pd_comm_open_subscriber (PD_COMM_T *communication);
TRDP_ERR_T pd_comm_send (PD_COMM_T *communication, const UINT8 *data, UINT32 size);
TRDP_ERR_T pd_comm_receive (PD_COMM_T *communication,
                            TRDP_PD_INFO_T *info,
                            UINT8 *data,
                            UINT32 *size);
TRDP_ERR_T pd_comm_process (PD_COMM_T *communication);

#ifdef __cplusplus
}
#endif

#endif /* PD_COMMUNICATION_H */
