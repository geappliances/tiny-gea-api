/*!
 * @file
 * @brief Simplified GEA3 interface that only supports sending and receiving packets.
 *
 * @note that this interface does not support queueing so if a new message is sent before
 * the last send has been completed then the last send will be interrupted.
 */

#ifndef i_tiny_gea3_interface_h
#define i_tiny_gea3_interface_h

#include <stdbool.h>
#include "i_tiny_event.h"
#include "tiny_gea3_packet.h"

typedef struct {
  const tiny_gea3_packet_t* packet;
} tiny_gea3_interface_on_receive_args_t;

typedef void (*tiny_gea3_interface_send_callback_t)(void* context, tiny_gea3_packet_t* packet);

struct i_tiny_gea3_interface_api_t;

typedef struct {
  const struct i_tiny_gea3_interface_api_t* api;
} i_tiny_gea3_interface_t;

typedef struct i_tiny_gea3_interface_api_t {
  void (*send)(
    i_tiny_gea3_interface_t* self,
    uint8_t destination,
    uint8_t payload_length,
    tiny_gea3_interface_send_callback_t callback,
    void* context);

  i_tiny_event_t* (*on_receive)(i_tiny_gea3_interface_t* self);
} i_tiny_gea3_interface_api_t;

/*!
 * Send a packet by getting direct access to the internal send buffer (given to
 * the client via the provided callback). Sets the source and destination addresses
 * of the packet automatically. If the requested payload size is too large then the
 * callback will not be invoked.
 */
static inline void tiny_gea3_interface_send(
  i_tiny_gea3_interface_t* self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea3_interface_send_callback_t callback,
  void* context)
{
  self->api->send(self, destination, payload_length, callback, context);
}

/*!
 * Event raised when a packet is received.
 */
static inline i_tiny_event_t* tiny_gea3_interface_on_receive(i_tiny_gea3_interface_t* self)
{
  return self->api->on_receive(self);
}

#endif
