#include "app_button.h"
#include "app_timer.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_debug_assert_handler.h"
#include "ble_error_log.h"
#include "ble_hci.h"
#include "ble_nus.h"
#include "boards.h"
#include "nordic_common.h"
#include "nrf.h"
#include "nrf51_bitfields.h"
#include "softdevice_handler.h"
#include <stdint.h>
#include <string.h>


#define NRF51DK_PCA10028_LED1_PIN_NO    21
#define NRF51DK_PCA10028_LED2_PIN_NO    22
#define NRF51DK_PCA10028_LED3_PIN_NO    23
#define NRF51DK_PCA10028_LED4_PIN_NO    24

#define NRF51822_ADC_IN1                2
#define NRF51822_ADC_IN0                3
#define NRF51822_LED1                   8
#define NRF51822_LED2                   10
#define NRF51822_LED3                   12
#define NRF51822_BUZZ                   14
#define NRF51822_IO_2                   15
#define NRF51822_IO_1                   16

#define APP_ADV_INTERVAL                64
#define APP_ADV_TIMEOUT_IN_SECONDS      180

#define APP_TIMER_PRESCALER             0
#define APP_TIMER_MAX_TIMERS            2
#define APP_TIMER_OP_QUEUE_SIZE         4

#define MIN_CONN_INTERVAL               16
#define MAX_CONN_INTERVAL               60
#define SLAVE_LATENCY                   0
#define CONN_SUP_TIMEOUT                400
#define RADIO_TX_POWER_IN_DBM           -16

#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(20000, APP_TIMER_PRESCALER)
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)
#define MAX_CONN_PARAMS_UPDATE_COUNT    3

#define SEC_PARAM_TIMEOUT               30
#define SEC_PARAM_BOND                  1
#define SEC_PARAM_MITM                  0
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE
#define SEC_PARAM_OOB                   0
#define SEC_PARAM_MIN_KEY_SIZE          7
#define SEC_PARAM_MAX_KEY_SIZE          16


static ble_gap_sec_params_t             m_sec_params;
static uint16_t                         m_conn_handle = BLE_CONN_HANDLE_INVALID;
static ble_nus_t                        m_nus;
static app_timer_id_t                   my_timer_id;
static char                             DEVICE_NAME[40] = "BLE Uninitialized";
static uint8_t                          current_status[4] = "--";


void nus_data_handler(ble_nus_t *p_nus, uint8_t *p_data, uint16_t length)
{
    ble_nus_send_string(p_nus, current_status, 2);
}


static void periodic_task(void)
{
    current_status[0] = nrf_gpio_pin_read(NRF51822_ADC_IN0) ? '1' : '0';
    current_status[1] = nrf_gpio_pin_read(NRF51822_ADC_IN1) ? '1' : '0';
}


void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t *p_file_name)
{
    NVIC_SystemReset();
}


void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name)
{
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}


static void gpio_init(void)
{
    nrf_gpio_cfg_input(NRF51822_ADC_IN0, NRF_GPIO_PIN_PULLDOWN);
    nrf_gpio_cfg_input(NRF51822_ADC_IN1, NRF_GPIO_PIN_PULLDOWN);
    nrf_gpio_cfg_output(NRF51822_LED1); nrf_gpio_pin_clear(NRF51822_LED1);
    nrf_gpio_cfg_output(NRF51822_LED2); nrf_gpio_pin_clear(NRF51822_LED2);
    nrf_gpio_cfg_output(NRF51822_LED3); nrf_gpio_pin_clear(NRF51822_LED3);
    nrf_gpio_cfg_output(NRF51822_BUZZ); nrf_gpio_pin_clear(NRF51822_BUZZ);
    nrf_gpio_cfg_output(NRF51822_IO_1); nrf_gpio_pin_set(NRF51822_IO_1);
    nrf_gpio_cfg_output(NRF51822_IO_2); nrf_gpio_pin_set(NRF51822_IO_2);
}


static void timers_init(void)
{
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_MAX_TIMERS, APP_TIMER_OP_QUEUE_SIZE, false);
}


static void advertising_start(void)
{
    uint32_t             err_code;
    ble_gap_adv_params_t adv_params;

    memset(&adv_params, 0, sizeof(adv_params));

    adv_params.type        = BLE_GAP_ADV_TYPE_ADV_IND;
    adv_params.p_peer_addr = NULL;
    adv_params.fp          = BLE_GAP_ADV_FP_ANY;
    adv_params.interval    = APP_ADV_INTERVAL;
    adv_params.timeout     = APP_ADV_TIMEOUT_IN_SECONDS;

    err_code = sd_ble_gap_adv_start(&adv_params);
    APP_ERROR_CHECK(err_code);
}


static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t                         err_code;
    static ble_gap_evt_auth_status_t m_auth_status;
    ble_gap_enc_info_t *             p_enc_info;

    switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        advertising_start();
        break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        err_code = sd_ble_gap_sec_params_reply(
                m_conn_handle, BLE_GAP_SEC_STATUS_SUCCESS, &m_sec_params);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GAP_EVT_AUTH_STATUS:
        m_auth_status = p_ble_evt->evt.gap_evt.params.auth_status;
        break;

    case BLE_GAP_EVT_SEC_INFO_REQUEST:
        p_enc_info = &m_auth_status.periph_keys.enc_info;
        if (p_enc_info->div == p_ble_evt->evt.gap_evt.params.sec_info_request.div) {
            err_code = sd_ble_gap_sec_info_reply(m_conn_handle, p_enc_info, NULL);
            APP_ERROR_CHECK(err_code);
        } else {
            err_code = sd_ble_gap_sec_info_reply(m_conn_handle, NULL, NULL);
            APP_ERROR_CHECK(err_code);
        }
        break;

    case BLE_GAP_EVT_TIMEOUT:
        if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISEMENT) {
            advertising_start();
        }
        break;

    default:
        break;
    }
}


static void ble_evt_dispatch(ble_evt_t *p_ble_evt)
{
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_nus_on_ble_evt(&m_nus, p_ble_evt);
    on_ble_evt(p_ble_evt);
}


static void ble_stack_init(void)
{
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, false);

    uint32_t err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);
}


static void device_name_init(void)
{
    ble_gap_addr_t ctx;
    char *readable_addr = DEVICE_NAME + 4;

    if (sd_ble_gap_address_get(&ctx) != 0) {
        return;
    }

    readable_addr[0]  = "0123456789ABCDEF"[ctx.addr[5] >> 4];
    readable_addr[1]  = "0123456789ABCDEF"[ctx.addr[5] & 15];
    readable_addr[2]  = ':';
    readable_addr[3]  = "0123456789ABCDEF"[ctx.addr[4] >> 4];
    readable_addr[4]  = "0123456789ABCDEF"[ctx.addr[4] & 15];
    readable_addr[5]  = ':';
    readable_addr[6]  = "0123456789ABCDEF"[ctx.addr[3] >> 4];
    readable_addr[7]  = "0123456789ABCDEF"[ctx.addr[3] & 15];
    readable_addr[8]  = ':';
    readable_addr[9]  = "0123456789ABCDEF"[ctx.addr[2] >> 4];
    readable_addr[10] = "0123456789ABCDEF"[ctx.addr[2] & 15];
    readable_addr[11] = ':';
    readable_addr[12] = "0123456789ABCDEF"[ctx.addr[1] >> 4];
    readable_addr[13] = "0123456789ABCDEF"[ctx.addr[1] & 15];
    readable_addr[14] = ':';
    readable_addr[15] = "0123456789ABCDEF"[ctx.addr[0] >> 4];
    readable_addr[16] = "0123456789ABCDEF"[ctx.addr[0] & 15];
    readable_addr[17] = 0;
}


static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(
            &sec_mode, (const uint8_t *) DEVICE_NAME, strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_tx_power_set(RADIO_TX_POWER_IN_DBM);
    APP_ERROR_CHECK(err_code);
}


static void services_init(void)
{
    uint32_t         err_code;
    ble_nus_init_t   nus_init;

    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;

    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
}


static void advertising_init(void)
{
    uint32_t      err_code;
    ble_advdata_t advdata;
    ble_advdata_t scanrsp;
    uint8_t       flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

    ble_uuid_t adv_uuids[] = {{BLE_UUID_NUS_SERVICE, m_nus.uuid_type}};

    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance      = false;
    advdata.flags.size              = sizeof(flags);
    advdata.flags.p_data            = &flags;

    memset(&scanrsp, 0, sizeof(scanrsp));
    scanrsp.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
    scanrsp.uuids_complete.p_uuids  = adv_uuids;

    err_code = ble_advdata_set(&advdata, &scanrsp);
    APP_ERROR_CHECK(err_code);
}


static void on_conn_params_evt(ble_conn_params_evt_t *p_evt)
{
    uint32_t err_code;
    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


static void sec_params_init(void)
{
    m_sec_params.timeout      = SEC_PARAM_TIMEOUT;
    m_sec_params.bond         = SEC_PARAM_BOND;
    m_sec_params.mitm         = SEC_PARAM_MITM;
    m_sec_params.io_caps      = SEC_PARAM_IO_CAPABILITIES;
    m_sec_params.oob          = SEC_PARAM_OOB;
    m_sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
    m_sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
}


static void periodic_task_start(void)
{
    app_timer_start(my_timer_id, APP_TIMER_TICKS(500, APP_TIMER_PRESCALER), NULL);
}

static void periodic_timeout_handler(void *p_context)
{
    periodic_task();
    periodic_task_start();
}

static void periodic_task_init(void)
{
    app_timer_create(&my_timer_id, APP_TIMER_MODE_SINGLE_SHOT, &periodic_timeout_handler);
}


static void power_manage(void)
{
    uint32_t err_code = sd_app_evt_wait();
    APP_ERROR_CHECK(err_code);
}


int main(void)
{
    gpio_init();
    timers_init();
    ble_stack_init();
    device_name_init();
    gap_params_init();
    services_init();
    advertising_init();
    conn_params_init();
    sec_params_init();
    periodic_task_init();

    advertising_start();
    periodic_task_start();

    for (;;) {
        power_manage();
    }
}
