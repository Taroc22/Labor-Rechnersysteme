#include <string.h>

#include "pd_communication.h"
#include "vos_utils.h"

#define PD_COMM_RESERVED_MEMORY 160000u

static TRDP_ERR_T pd_comm_require_open (const PD_COMM_T *communication)
{
    if (communication == NULL || communication->initialized == FALSE)
    {
        return TRDP_PARAM_ERR;
    }
    return TRDP_NO_ERR;
}

void pd_comm_config_init (PD_COMM_CONFIG_T *config)
{
    if (config == NULL)
    {
        return;
    }

    config->comid = 0u;
    config->use_udp = TRUE;
    config->destination_ip = 0u;
    config->source_ip = 0u;
    config->cycle_us = PD_COMM_DEFAULT_CYCLE_US;
    config->timeout_us = PD_COMM_DEFAULT_TIMEOUT_US;
}

void pd_comm_config_set_comid (PD_COMM_CONFIG_T *config, UINT32 comid)
{
    if (config != NULL)
    {
        config->comid = comid;
    }
}

TRDP_ERR_T pd_comm_config_set_udp (PD_COMM_CONFIG_T *config, BOOL8 use_udp)
{
    if (config == NULL || use_udp == FALSE)
    {
        return TRDP_PARAM_ERR;
    }

    config->use_udp = TRUE;
    return TRDP_NO_ERR;
}

static TRDP_ERR_T pd_comm_set_ip (TRDP_IP_ADDR_T *target, const CHAR8 *address)
{
    TRDP_IP_ADDR_T parsed;

    if (target == NULL || address == NULL || *address == '\0')
    {
        return TRDP_PARAM_ERR;
    }

    parsed = vos_dottedIP(address);
    if (parsed == 0u && strcmp(address, "0.0.0.0") != 0)
    {
        return TRDP_PARAM_ERR;
    }

    *target = parsed;
    return TRDP_NO_ERR;
}

TRDP_ERR_T pd_comm_config_set_destination_ip (PD_COMM_CONFIG_T *config, const CHAR8 *address)
{
    return config == NULL ? TRDP_PARAM_ERR : pd_comm_set_ip(&config->destination_ip, address);
}

TRDP_ERR_T pd_comm_config_set_source_ip (PD_COMM_CONFIG_T *config, const CHAR8 *address)
{
    return config == NULL ? TRDP_PARAM_ERR : pd_comm_set_ip(&config->source_ip, address);
}

TRDP_ERR_T pd_comm_open (PD_COMM_T *communication, const PD_COMM_CONFIG_T *config)
{
    TRDP_MEM_CONFIG_T memory_config = {NULL, PD_COMM_RESERVED_MEMORY, {0u}};
    TRDP_PD_CONFIG_T pd_config = {NULL, NULL, TRDP_PD_DEFAULT_SEND_PARAM,
                                  TRDP_FLAGS_NONE, 1000000u, TRDP_TO_SET_TO_ZERO, 0u};
    TRDP_PROCESS_CONFIG_T process_config = {"pd-comm", "", "",
                                            TRDP_PROCESS_DEFAULT_CYCLE_TIME, 0u,
                                            TRDP_OPTION_BLOCK, 0u};
    TRDP_ERR_T error;

    if (communication == NULL || config == NULL || config->use_udp == FALSE)
    {
        return TRDP_PARAM_ERR;
    }

    memset(communication, 0, sizeof(*communication));
    error = tlc_init(NULL, NULL, &memory_config);
    if (error != TRDP_NO_ERR)
    {
        return error;
    }

    error = tlc_openSession(&communication->session, config->source_ip, 0u,
                            NULL, &pd_config, NULL, &process_config);
    if (error != TRDP_NO_ERR)
    {
        tlc_terminate();
        return error;
    }

    communication->config = *config;
    communication->initialized = TRUE;
    return TRDP_NO_ERR;
}

TRDP_ERR_T pd_comm_open_publisher (PD_COMM_T *communication,
                                   const UINT8 *initial_data,
                                   UINT32 initial_size)
{
    TRDP_ERR_T error = pd_comm_require_open(communication);

    if (error != TRDP_NO_ERR || communication->publisher_open == TRUE ||
        communication->subscriber_open == TRUE || communication->config.destination_ip == 0u)
    {
        return TRDP_PARAM_ERR;
    }

    error = tlp_publish(communication->session, &communication->publisher,
                        NULL, NULL, 0u, communication->config.comid, 0u, 0u,
                        communication->config.source_ip, communication->config.destination_ip,
                        communication->config.cycle_us, 0u, TRDP_FLAGS_NONE,
                        initial_data, initial_size);
    if (error != TRDP_NO_ERR)
    {
        return error;
    }

    error = tlc_updateSession(communication->session);
    if (error != TRDP_NO_ERR)
    {
        (void) tlp_unpublish(communication->session, communication->publisher);
        return error;
    }
    communication->publisher_open = TRUE;
    return TRDP_NO_ERR;
}

TRDP_ERR_T pd_comm_open_subscriber (PD_COMM_T *communication)
{
    TRDP_ERR_T error = pd_comm_require_open(communication);

    if (error != TRDP_NO_ERR || communication->publisher_open == TRUE ||
        communication->subscriber_open == TRUE)
    {
        return TRDP_PARAM_ERR;
    }

    error = tlp_subscribe(communication->session, &communication->subscriber,
                          NULL, NULL, 0u, communication->config.comid, 0u, 0u,
                          communication->config.source_ip, VOS_INADDR_ANY,
                          communication->config.destination_ip, TRDP_FLAGS_NONE,
                          communication->config.timeout_us, TRDP_TO_SET_TO_ZERO);
    if (error != TRDP_NO_ERR)
    {
        return error;
    }

    error = tlc_updateSession(communication->session);
    if (error != TRDP_NO_ERR)
    {
        (void) tlp_unsubscribe(communication->session, communication->subscriber);
        return error;
    }
    communication->subscriber_open = TRUE;
    return TRDP_NO_ERR;
}

TRDP_ERR_T pd_comm_send (PD_COMM_T *communication, const UINT8 *data, UINT32 size)
{
    if (pd_comm_require_open(communication) != TRDP_NO_ERR || communication->publisher_open == FALSE)
    {
        return TRDP_PARAM_ERR;
    }
    return tlp_put(communication->session, communication->publisher, data, size);
}

TRDP_ERR_T pd_comm_receive (PD_COMM_T *communication,
                            TRDP_PD_INFO_T *info,
                            UINT8 *data,
                            UINT32 *size)
{
    if (pd_comm_require_open(communication) != TRDP_NO_ERR || communication->subscriber_open == FALSE ||
        info == NULL || data == NULL || size == NULL)
    {
        return TRDP_PARAM_ERR;
    }
    return tlp_get(communication->session, communication->subscriber, info, data, size);
}

TRDP_ERR_T pd_comm_process (PD_COMM_T *communication)
{
    TRDP_FDS_T file_descriptors;
    TRDP_TIME_T interval;
    TRDP_SOCK_T no_descriptors = VOS_INVALID_SOCKET;
    INT32 ready_descriptors;
    TRDP_ERR_T error;

    if (pd_comm_require_open(communication) != TRDP_NO_ERR)
    {
        return TRDP_PARAM_ERR;
    }

    FD_ZERO(&file_descriptors);
    error = tlc_getInterval(communication->session, &interval, &file_descriptors, &no_descriptors);
    if (error != TRDP_NO_ERR)
    {
        return error;
    }
    ready_descriptors = vos_select(no_descriptors, &file_descriptors, NULL, NULL, &interval);
    return tlc_process(communication->session, &file_descriptors, &ready_descriptors);
}

void pd_comm_close (PD_COMM_T *communication)
{
    if (communication == NULL || communication->initialized == FALSE)
    {
        return;
    }

    if (communication->publisher_open == TRUE)
    {
        (void) tlp_unpublish(communication->session, communication->publisher);
    }
    if (communication->subscriber_open == TRUE)
    {
        (void) tlp_unsubscribe(communication->session, communication->subscriber);
    }
    (void) tlc_closeSession(communication->session);
    tlc_terminate();
    memset(communication, 0, sizeof(*communication));
}
