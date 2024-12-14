/*!
 * @file
 * @brief Interface for acting as a GEA2 ERD client. Supports reads and writes.
 */

#ifndef i_tiny_gea2_erd_client_h
#define i_tiny_gea2_erd_client_h

#include <stdint.h>
#include "i_tiny_event.h"
#include "tiny_erd.h"

enum {
  tiny_gea2_erd_client_activity_type_read_completed,
  tiny_gea2_erd_client_activity_type_read_failed,
  tiny_gea2_erd_client_activity_type_write_completed,
  tiny_gea2_erd_client_activity_type_write_failed
};
typedef uint8_t tiny_gea2_erd_client_activity_type_t;

enum {
  tiny_gea2_erd_client_read_failure_reason_retries_exhausted
};
typedef uint8_t tiny_gea2_erd_client_read_failure_reason_t;

enum {
  tiny_gea2_erd_client_write_failure_reason_retries_exhausted
};
typedef uint8_t tiny_gea2_erd_client_write_failure_reason_t;

typedef uint8_t tiny_gea2_erd_client_request_id_t;

typedef struct {
  tiny_gea2_erd_client_activity_type_t type;
  uint8_t address;

  union {
    /*!
     * @warning Data will be in big endian. Implementations will not have enough information to
     * swap on the client's behalf.
     */
    struct {
      tiny_gea2_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
    } read_completed;

    struct {
      tiny_gea2_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      tiny_gea2_erd_client_read_failure_reason_t reason;
    } read_failed;

    struct {
      tiny_gea2_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
    } write_completed;

    struct {
      tiny_gea2_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
      tiny_gea2_erd_client_write_failure_reason_t reason;
    } write_failed;
  };
} tiny_gea2_erd_client_on_activity_args_t;

struct i_tiny_gea2_erd_client_api_t;

typedef struct {
  const struct i_tiny_gea2_erd_client_api_t* api;
} i_tiny_gea2_erd_client_t;

typedef struct i_tiny_gea2_erd_client_api_t {
  bool (*read)(
    i_tiny_gea2_erd_client_t* self,
    tiny_gea2_erd_client_request_id_t* request_id,
    uint8_t address,
    tiny_erd_t erd);

  bool (*write)(
    i_tiny_gea2_erd_client_t* self,
    tiny_gea2_erd_client_request_id_t* request_id,
    uint8_t address,
    tiny_erd_t erd,
    const void* data,
    uint8_t data_size);

  i_tiny_event_t* (*on_activity)(i_tiny_gea2_erd_client_t* self);
} i_tiny_gea2_erd_client_api_t;

/*!
 * Send a read ERD request to ERD host. Returns true if the request could be queued, false otherwise.
 */
static inline bool tiny_gea2_erd_client_read(
  i_tiny_gea2_erd_client_t* self,
  tiny_gea2_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd)
{
  return self->api->read(self, request_id, address, erd);
}

/*!
 * Send a write ERD request to an ERD host. Returns true if the request could be queued, false otherwise.
 * @warning Data must already be in big endian. Implementers will not have enough information to
 *    swap on the client's behalf.
 */
static inline bool tiny_gea2_erd_client_write(
  i_tiny_gea2_erd_client_t* self,
  tiny_gea2_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd,
  const void* data,
  uint8_t data_size)
{
  return self->api->write(self, request_id, address, erd, data, data_size);
}

/*!
 * Event that is raised when a read or write request is completed.
 */
static inline i_tiny_event_t* tiny_gea2_erd_client_on_activity(i_tiny_gea2_erd_client_t* self)
{
  return self->api->on_activity(self);
}

#endif
