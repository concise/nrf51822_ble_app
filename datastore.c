/*
 * nRF51 SDK >> Libraries >> Persistent Storage Manager
 * https://developer.nordicsemi.com/nRF51_SDK/nRF51_SDK_v5.x.x/doc/5.2.0/html/a00131.html
 */

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint8_t */
#include <string.h> /* memset memcpy */
#include "nrf.h"
#include "pstorage.h"
#include "softdevice_handler.h"
#include "datastore.h"

#define N_BLOCKS    16      /* How many blocks to acquire */
#define BLOCK_SIZE  32      /* How many bytes each block contains */

/* Because the pstorage write operation is asynchronous and does not have an
 * internal swap memory, we must provide a pointer to a memory chunk that is
 * not reused or freed even after the current scope ends.  So I decided that I
 * should just use a global static (symbol only accessible within this file)
 * array. */
static uint8_t write_swap[BLOCK_SIZE];

/* The base block handle to be registered for all future use. */
static pstorage_handle_t base_handle;

/* Note that at any time there should only be at most one pending operation.
 * Also note that when a connection is dropped, things should be cleaned up. */
static int pstorage_operation_pending = 0;

/* The function pointer that user provided previously in a WRITE request.  If
 * non-NULL, it should be invoked after the operation is finished, with one
 * boolean argument indicating success or failure. */
static datastore_write_callback_t async_write_cb = 0;

static void on_pstorage_update_op_finished(uint32_t result)
{
    memset(write_swap, 0, BLOCK_SIZE);

    pstorage_operation_pending -= 1;
    if (pstorage_operation_pending != 0) {
        /* SHOULD NEVER HAPPEN */
    }

    if (async_write_cb == 0)
        return;

    (*async_write_cb)((result == NRF_SUCCESS) ?
            DATASTORE_NO_ERROR : DATASTORE_PSTORAGE_ASYNC_UPDATE_ERROR);
}

static void pstorage_event_handler(
        pstorage_handle_t *p_handle, uint8_t op_code, uint32_t result,
        uint8_t *p_data, uint32_t data_len)
{
    if (op_code == PSTORAGE_UPDATE_OP_CODE) {
        on_pstorage_update_op_finished(result);
    }
}

void datastore_init(void)
{
    uint32_t err_code;
    pstorage_module_param_t param;

    err_code = pstorage_init();
    APP_ERROR_CHECK(err_code);

    /* Register how many block we want to use and the size of each block.
     * Set the pstorage event handler which is for write/update operation. */
    param.block_count = N_BLOCKS;
    param.block_size = BLOCK_SIZE;
    param.cb = &pstorage_event_handler;
    err_code = pstorage_register(&param, &base_handle);
    APP_ERROR_CHECK(err_code);

    /* We do not have a seperated sys_evt_dispatch() function because there
     * seems to be no other system event handler in this project...  So we
     * directly set the handler to the one (from pstorage.c) for pstorage. */
    err_code = softdevice_sys_evt_handler_set(&pstorage_sys_event_handler);
    APP_ERROR_CHECK(err_code);
}

datastore_error_t datastore_read(int id, uint8_t *obuf, size_t nbytes_to_read)
{
    pstorage_handle_t block_handle;
    uint32_t err_code;
    uint8_t tmp_swap[BLOCK_SIZE];

    if (pstorage_operation_pending) {
        return DATASTORE_SOME_OP_PENDING_ERROR;
    }

    if (nbytes_to_read == 0) {
        return DATASTORE_NO_ERROR;
    }

    if (nbytes_to_read > BLOCK_SIZE) {
        return DATASTORE_INVALID_LENGTH;
    }

    if (id < 0 || id > N_BLOCKS - 1) {
        return DATASTORE_INVALID_ID;
    }

    if (obuf == 0) {
        return DATASTORE_INVALID_POINTER;
    }

    err_code = pstorage_block_identifier_get(&base_handle, id, &block_handle);

    if (err_code != NRF_SUCCESS) {
        return DATASTORE_PSTORAGE_BLOCKIDGET_ERROR;
    }

    err_code = pstorage_load(tmp_swap, &block_handle, BLOCK_SIZE, 0);

    if (err_code != NRF_SUCCESS) {
        return DATASTORE_PSTORAGE_LOAD_ERROR;
    }

    memcpy(obuf, tmp_swap, nbytes_to_read);
    return DATASTORE_NO_ERROR;
}

/* Write `nbytes_to_write` bytes to a 32-byte block identified by `id`.
 * If provided number of bytes is less than 32, 0x00 is padded at the end. */
datastore_error_t datastore_write_async(
        int id, const uint8_t *ibuf, size_t nbytes_to_write,
        datastore_write_callback_t result_cb)
{
    pstorage_handle_t block_handle;
    uint32_t err_code;

    if (pstorage_operation_pending) {
        return DATASTORE_SOME_OP_PENDING_ERROR;
    }

    if (nbytes_to_write > BLOCK_SIZE) {
        return DATASTORE_INVALID_LENGTH;
    }

    if (id < 0 || id > N_BLOCKS - 1) {
        return DATASTORE_INVALID_ID;
    }

    if (nbytes_to_write > 0 && ibuf == 0) {
        return DATASTORE_INVALID_POINTER;
    }

    err_code = pstorage_block_identifier_get(&base_handle, id, &block_handle);
    if (err_code != NRF_SUCCESS) {
        return DATASTORE_PSTORAGE_BLOCKIDGET_ERROR;
    }

    memset(write_swap, 0, BLOCK_SIZE);
    if (nbytes_to_write > 0) {
        memcpy(write_swap, ibuf, nbytes_to_write);
    }
    err_code = pstorage_update(&block_handle, write_swap, 16, 0);
    if (err_code != NRF_SUCCESS) {
        return DATASTORE_PSTORAGE_UPDATE_ERROR;
    }

    async_write_cb = result_cb;
    pstorage_operation_pending += 1;
    return DATASTORE_NO_ERROR;
}
