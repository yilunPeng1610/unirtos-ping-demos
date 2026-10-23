/*****************************************************************/ /**
* @file ping_demo.c
* @brief Ping demo implementation
* @author harry.li@quectel.com
* @date 2025-05-7
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-05-7 <td>1.0 <td>harry.li <td> Initial version
* </table>
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qcm_ping_app.h"
#if (defined(CONFIG_QOSA_BK7238_PLATFORM_FUNC) ||defined(CONFIG_QOSA_BK7258_PLATFORM_FUNC) || defined(CONFIG_QOSA_ECR6600_PLATFORM_FUNC)) 
#include "qosa_wlan.h"
#endif
/*===========================================================================
 *  Macro Definition
 ===========================================================================*/

#define QOS_LOG_TAG                    LOG_TAG

#define UNIR_PING_DEMO_TASK_STACK_SIZE 4096

// PING server address
#define UNIR_PING_DEMO_SERVER          "www.baidu.com"

// PING configuration parameters
#define UNIR_PING_DEMO_SIMID           0
#define UNIR_PING_DEMO_PDPID           1
// PING count
#define UNIR_PING_DEMO_CNT             4
// PING timeout per attempt
#define UNIR_PING_DEMO_TIMEOUT         4
// PING TTL
#define UNIR_PING_DEMO_TTL             255

/**
 * @brief Ping callback response data structure
 *
 * This structure is used to store ping operation response information,
 * including event type, error code, response data, and user-defined parameters
 */
typedef struct
{
    qcm_ping_event_type event_id;   /**< ping event type */
    qcm_ping_error_e    evt_code;   /**< ping event error code */
    qcm_ping_resp_t     resp_ptr;   /**< ping response data pointer */
    void               *user_param; /**< user-defined parameter pointer */
} ping_demo_resp_t;

/*===========================================================================
 *  Variate
 ===========================================================================*/

static qosa_task_t g_ping_demo_task = QOSA_NULL;
static qosa_msgq_t g_ping_demo_msg = QOSA_NULL;

/*===========================================================================
 *  Static API Functions
 ===========================================================================*/
#if (defined(CONFIG_QOSA_BK7258_PLATFORM_FUNC) || defined(CONFIG_QOSA_BK7238_PLATFORM_FUNC) || defined(CONFIG_QOSA_ECR6600_PLATFORM_FUNC)) 

static qosa_sem_t  wifi_sem = QOSA_NULL;

static void qcm_socket_app_wifi_event_cb(void *param)
{
    qosa_wlan_event_msg_t  *event_info = (qosa_wlan_event_msg_t *)param;

    switch(event_info->event)
    {
    case QOSA_WLAN_EVT_STA_CONNECTED:
        {
            QLOGI("wlan connected!\n");
        }
        break;
    case QOSA_WLAN_EVT_STA_GOT_IP:
        {
            QLOGI("wlan got ip!\n");
            QLOGI("wifi_sem=%p\n", wifi_sem);
            if(wifi_sem != QOSA_NULL)
                qosa_sem_release(wifi_sem);
        }
        break;
    case QOSA_WLAN_EVT_STA_DISCONNECTED:
        {
            QLOGI("wlan disconnected!\n");            
        }
        break;
    case QOSA_WLAN_EVT_STA_PASSWORD_WRONG:
    case QOSA_WLAN_EVT_STA_NO_AP_FOUND:
    case QOSA_WLAN_EVT_STA_CONNECT_FAILED:
        {
            QLOGI("wlan connect failed!\n");
        }
        break;
    default:
        break;
    }
}
static int qcm_socket_app_wifi_active(void)
{
    qosa_wlan_network_t  wlan_cfg = {0};
    qosa_wlan_sta_config_t sta_cfg = {0};
    qosa_wlan_driver_init();

    sta_cfg.dhcp_enabled = QOSA_TRUE;    
    qosa_memcpy(sta_cfg.ssid, "cbq666", qosa_strlen("cbq666"));
    qosa_memcpy(sta_cfg.pwd, "12345678", qosa_strlen("12345678"));
    sta_cfg.ip.ipv4.addr_type = QOSA_WLAN_ADDR_TYPE_DHCP;

    wlan_cfg.mode = QOSA_WLAN_MODE_STA;
    wlan_cfg.sta_cfg = &sta_cfg;
    
    qosa_wlan_register_status_cb(qcm_socket_app_wifi_event_cb);

    qosa_wlan_start(&wlan_cfg);
    
    return 0;
}
#endif
/**
 * @brief Ping result callback function
 *
 * This function handles ping operation result callbacks, encapsulates ping response data,
 * and sends it through the message queue
 *
 * @param event_id ping event type
 * @param evt_code ping error code
 * @param resp_ptr ping response data pointer
 * @param user_param user-defined parameter pointer
 */
static void unir_ping_result_cb(qcm_ping_event_type event_id, qcm_ping_error_e evt_code, qcm_ping_resp_t *resp_ptr, void *user_param)
{
    ping_demo_resp_t ping_rsp = {0};

    ping_rsp.event_id = event_id;
    ping_rsp.evt_code = evt_code;
    ping_rsp.user_param = user_param;

    /* Copy corresponding response data based on event type */
    if (ping_rsp.event_id == QCM_PING_STATS)
    {
        qosa_memcpy(&ping_rsp.resp_ptr.type.status, &resp_ptr->type.status, sizeof(qcm_ping_stats_type));
    }
    else
    {
        qosa_memcpy(&ping_rsp.resp_ptr.type.summary, &resp_ptr->type.summary, sizeof(qcm_ping_summary_type));
    }

    /* Send message */
    qosa_msgq_release(g_ping_demo_msg, sizeof(ping_demo_resp_t), (qosa_uint8_t *)&ping_rsp, QOSA_NO_WAIT);
}

/**
 * @brief Process PING operation result callback function, parse and record PING statistics or summary information based on event type.
 *
 * This function handles response events from the PING module, including PING status updates and final summary information.
 * Distinguishes between single PING result (QCM_PING_STATS) and final summary (QCM_PING_SUMMARY) based on event ID,
 * formats the relevant information and outputs it through logs. Marks processing as complete when summary event is received.
 *
 * @param[in] ping_ptr Pointer to PING response data structure, containing event ID, event code and response content
 * @return Flag indicating whether processing is complete, QOSA_TRUE means processing ended, QOSA_FALSE means not ended
 */
static qosa_bool_t unir_ping_result_handler(ping_demo_resp_t *ping_ptr)
{
    char         resp_buf[256] = {0};            // Buffer for storing formatted response string
    qosa_int32_t resp_len = 0;                   // Response string length (currently not actually used)
    qosa_int32_t evt_code = ping_ptr->evt_code;  // Extract event code
    qosa_bool_t  finish = QOSA_FALSE;            // Completion flag, default to not completed

    // Print event ID and event code for debugging and tracking
    QLOGD("event_id=%d,%x", ping_ptr->event_id, evt_code);

    // Process based on different event IDs
    switch (ping_ptr->event_id)
    {
        // Handle single PING statistics information event
        case QCM_PING_STATS: {
            // Only process when event code indicates success
            if (evt_code == QCM_PING_OK)
            {
                // Get PING status information structure pointer
                qcm_ping_stats_type *stats = &ping_ptr->resp_ptr.type.status;
                if (stats != QOSA_NULL)
                {
                    // Format PING status information
                    resp_len += qosa_snprintf(resp_buf, 256, "\"%s\",%ld,%ld,%ld", stats->resolved_ip_addr, stats->ping_size, stats->ping_rtt, stats->ping_ttl);
                    // Print PING status information
                    QLOGV("PING: [%s]", resp_buf);
                }
            }
        }
        break;
        // Handle PING summary information event at completion
        case QCM_PING_SUMMARY: {
            // Only process when event code indicates success
            if (evt_code == QCM_PING_OK)
            {
                // Get PING summary information structure pointer
                qcm_ping_summary_type *summary = &ping_ptr->resp_ptr.type.summary;
                if (summary != QOSA_NULL)
                {
                    // Format PING summary information
                    resp_len += qosa_snprintf(
                        resp_buf,
                        256,
                        "%ld,%ld,%ld,%ld,%ld,%ld",
                        summary->num_pkts_sent,
                        summary->num_pkts_recvd,
                        summary->num_pkts_lost,
                        summary->min_rtt,
                        summary->max_rtt,
                        summary->avg_rtt
                    );
                    // Print PING completion summary information
                    QLOGD("PING_END: [%s]", resp_buf);
                }
            }
            // Mark processing as complete
            finish = QOSA_TRUE;
        }
        break;
        default:
            break;
    }
    // Return completion flag
    return finish;
}

/**
 * @brief Ping function demonstration processing function
 *
 * This function implements the complete process of a ping operation, including initializing ping parameters,
 * starting ping operation, waiting for and processing ping results. The function enters a loop waiting state
 * after starting ping, until the ping operation completes or an error occurs.
 */
static void unir_ping_demo_process(void *ctx)
{
    qcm_ping_config_type ping_options = {0};
    ping_demo_resp_t     rsp = {0};
    qcm_ping_error_e     ret = 0;
    qosa_bool_t          is_finish = QOSA_FALSE;
    QLOGV("PING demo_process start");
    // Wait 10 seconds to facilitate log capture and network registration on boot
    qosa_task_sleep_sec(10);
#if (defined(CONFIG_QOSA_BK7238_PLATFORM_FUNC) ||defined(CONFIG_QOSA_BK7258_PLATFORM_FUNC) || defined(CONFIG_QOSA_ECR6600_PLATFORM_FUNC)) 
    
    qosa_sem_create(&wifi_sem, 0);
    QOSA_ASSERT(wifi_sem != QOSA_NULL);
    QLOGI("wifi_sem=%p\n", wifi_sem);
    if(qcm_socket_app_wifi_active() != 0)
    {
        QLOGE("wlan error");
        return;
    }
    qosa_sem_wait(wifi_sem, QOSA_WAIT_FOREVER);
    QLOGI("wlan actived!\n");
#endif
    // Assign values for ping operation data size, count, timeout; use default values if no parameters
    ping_options.num_data_bytes = 64;
    ping_options.num_pings = UNIR_PING_DEMO_CNT;
    ping_options.ping_response_time_out = UNIR_PING_DEMO_TIMEOUT;
    ping_options.ttl = UNIR_PING_DEMO_TTL;

    // Start ping operation, specifying PDPID, SIM card ID, server address and other parameters
#if (defined(CONFIG_QOSA_BK7238_PLATFORM_FUNC) ||defined(CONFIG_QOSA_BK7258_PLATFORM_FUNC) || defined(CONFIG_QOSA_ECR6600_PLATFORM_FUNC))   
    ret = qcm_ping_start(UNIR_PING_DEMO_SERVER, &ping_options, unir_ping_result_cb, QOSA_NULL);
#else
    ret = qcm_ping_start(UNIR_PING_DEMO_PDPID, UNIR_PING_DEMO_SIMID, UNIR_PING_DEMO_SERVER, &ping_options, unir_ping_result_cb, QOSA_NULL);
#endif
    if (ret != QCM_PING_OK)
    {
        QLOGE("ping start err =%x", ret);
        qosa_msgq_delete(g_ping_demo_msg);
        return;
    }

    // Loop waiting for and processing ping result messages
    while (1)
    {
        // Wait for synchronous result message and process it
        qosa_msgq_wait(g_ping_demo_msg, (qosa_uint8_t *)&rsp, sizeof(ping_demo_resp_t), QOSA_WAIT_FOREVER);
        is_finish = unir_ping_result_handler(&rsp);
        if (is_finish)
        {
            // ping processing ended
            break;
        }
    }
    qosa_msgq_delete(g_ping_demo_msg);
    QLOGV("PING END");
}

void unir_ping_demo_init(void)
{
    QLOGV("enter UniRTOS PING DEMO !!!");
    if (g_ping_demo_msg == QOSA_NULL)
    {
        qosa_msgq_create(&g_ping_demo_msg, sizeof(ping_demo_resp_t), 10);
    }
    if (g_ping_demo_task == QOSA_NULL)
    {
        qosa_task_create(&g_ping_demo_task, UNIR_PING_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "ping_demo", unir_ping_demo_process, QOSA_NULL);
    }
}
UNIRTOS_APP_EXPORT(320, "ping_demo", unir_ping_demo_init);
