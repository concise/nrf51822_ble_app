#ifndef DATASTORE_H
#define DATASTORE_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint8_t */

typedef enum {
    DATASTORE_NO_ERROR = 0,
    DATASTORE_SOME_OP_PENDING_ERROR,
    DATASTORE_INVALID_ID,
    DATASTORE_INVALID_LENGTH,
    DATASTORE_INVALID_POINTER,
    DATASTORE_PSTORAGE_BLOCKIDGET_ERROR,
    DATASTORE_PSTORAGE_LOAD_ERROR,
    DATASTORE_PSTORAGE_UPDATE_ERROR,
    DATASTORE_PSTORAGE_ASYNC_UPDATE_ERROR
} datastore_error_t;

typedef void (*datastore_write_callback_t)(datastore_error_t);

void datastore_init(void);

datastore_error_t datastore_read(int, uint8_t *, size_t);

datastore_error_t datastore_write_async(
        int, const uint8_t *, size_t, datastore_write_callback_t);

#endif
