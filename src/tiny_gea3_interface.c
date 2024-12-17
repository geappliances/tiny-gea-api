/*!
 * @file
 * @brief
 *
 * # Notes on Interrupt Safety
 * ## Sending
 * Sending is interrupt-safe because the interrupt context only peeks from
 * the first element of the queue and makes no changes to the queue. While
 * sending, the non-interrupt context is free to add elements to the queue
 * as long as it does not remove any elements from the queue or otherwise
 * modify the first element in the queue. Only when the interrupt context
 * is done sending a packet is an element removed from the queue and while
 * this operation is pending the interrupt context is not free to begin
 * sending any additional packets.
 *
 * The non-interrupt context sets the send.in_progress flag and clears
 * the send.completed flag. While send.completed remains false, the first
 * element of the queue is not modified.
 *
 * The interrupt context sets the send.completed flag to indicate that it
 * is no longer reading from the queue. Until send.completed is false, it
 * does not read from the queue.
 *
 * [Non-interrupt]                     [Interrupt]
 *        |                                 |
 *  packet queued                           |
 *        |                                 |
 *        |---                              |
 *        |  | send.in_progress == true     |
 *        |<--                              |
 *        |                                 |
 *        |--- send.in_progress = true ---->|
 *        |                                 |
 *        |                            packet sent
 *        |                                 |
 *        |<------ send.completed = true ---|
 *        |                                 |
 *        |--- send.completed = false ----->|
 *        |--- send.in_progress = false --->|
 *        |                                 |
 *       ...                               ...
 *
 * ## Receiving
 * Receiving is interrupt safe because the receive.packet_ready flag is used
 * to ensure that only one of the interrupt and non-interrupt contexts is
 * using the receive buffer at any time.
 *
 * The interrupt context sets the receive.packet_ready flag. While the flag is
 * true, the interrupt context does not read from or write to the receive
 * buffer. After a valid received packet has been completely written to the
 * receive buffer, the interrupt context sets the receive.packet_ready flag
 * to indicate that it is ready for use by the non-interrupt context.
 *
 * The non-interrupt context clears the receive.packet_ready flag. While the
 * flag is false, the non-interrupt context does not read from or write to the
 * receive buffer. After a received packet has been processed by the non-
 * interrupt context, the it clears the flag to indicate that it is ready for
 * use by the interrupt context.
 */

#include <stdbool.h>
#include "tiny_crc16.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea_constants.h"
#include "tiny_stack_allocator.h"
#include "tiny_utils.h"

typedef tiny_gea3_interface_t self_t;

enum {
  data_length_bytes_not_included_in_data = tiny_gea_packet_transmission_overhead - tiny_gea_packet_overhead,
  crc_size = sizeof(uint16_t),
  packet_bytes_not_included_in_payload = crc_size + offsetof(tiny_gea_packet_t, payload),
  unbuffered_bytes = 2 // STX, ETX
};

enum {
  send_state_destination,
  send_state_payload_length,
  send_state_source,
  send_state_data,
  send_state_crc_msb,
  send_state_crc_lsb,
  send_state_etx,
  send_state_complete
};

#define needs_escape(_byte) ((_byte & 0xFC) == tiny_gea_esc)

static bool received_packet_has_valid_crc(self_t* self)
{
  return self->receive_crc == 0;
}

static bool received_packet_has_minimum_valid_length(self_t* self)
{
  return self->receive_count >= packet_bytes_not_included_in_payload;
}

static bool received_packet_has_valid_length(self_t* self)
{
  reinterpret(packet, self->receive_buffer, tiny_gea_packet_t*);
  return (packet->payload_length == self->receive_count + unbuffered_bytes);
}

static bool received_packet_is_addressed_to_me(self_t* self)
{
  reinterpret(packet, self->receive_buffer, tiny_gea_packet_t*);
  return (packet->destination == self->address) ||
    (packet->destination == tiny_gea_broadcast_address) ||
    (self->ignore_destination_address);
}

static void buffer_received_byte(self_t* self, uint8_t byte)
{
  if(self->receive_count == 0) {
    self->receive_crc = tiny_gea_crc_seed;
  }

  if(self->receive_count < self->receive_buffer_size) {
    self->receive_buffer[self->receive_count++] = byte;

    self->receive_crc = tiny_crc16_byte(
      self->receive_crc,
      byte);
  }
}

static void byte_received(void* context, const void* _args)
{
  reinterpret(self, context, self_t*);
  reinterpret(args, _args, const tiny_uart_on_receive_args_t*);
  reinterpret(packet, self->receive_buffer, tiny_gea_packet_t*);
  uint8_t byte = args->byte;

  if(self->receive_packet_ready) {
    return;
  }

  if(self->receive_escaped) {
    self->receive_escaped = false;
    buffer_received_byte(self, byte);
    return;
  }

  switch(byte) {
    case tiny_gea_esc:
      self->receive_escaped = true;
      break;

    case tiny_gea_stx:
      self->receive_count = 0;
      self->stx_received = true;
      break;

    case tiny_gea_etx:
      if(self->stx_received &&
        received_packet_has_minimum_valid_length(self) &&
        received_packet_has_valid_length(self) &&
        received_packet_has_valid_crc(self) &&
        received_packet_is_addressed_to_me(self)) {
        packet->payload_length -= tiny_gea_packet_transmission_overhead;
        self->receive_packet_ready = true;
      }
      self->stx_received = false;
      break;

    default:
      buffer_received_byte(self, byte);
      break;
  }
}

static bool determine_byte_to_send_considering_escapes(self_t* self, uint8_t byte, uint8_t* byte_to_send)
{
  if(!self->send_escaped && needs_escape(byte)) {
    self->send_escaped = true;
    *byte_to_send = tiny_gea_esc;
  }
  else {
    self->send_escaped = false;
    *byte_to_send = byte;
  }

  return !self->send_escaped;
}

static void begin_send(self_t* self)
{
  tiny_queue_peek_partial(&self->send_queue, &self->send_data_length, sizeof(self->send_data_length), offsetof(tiny_gea_packet_t, payload_length), 0);
  self->send_crc = tiny_gea_crc_seed;
  self->send_state = send_state_destination;
  self->send_offset = 0;
  self->send_in_progress = true;
  tiny_uart_send(self->uart, tiny_gea_stx);
}

static void byte_sent(void* context, const void* args)
{
  reinterpret(self, context, self_t*);
  (void)args;

  uint8_t byte_to_send = 0;

  switch(self->send_state) {
    case send_state_destination: {
      uint8_t destination;
      tiny_queue_peek_partial(&self->send_queue, &destination, sizeof(destination), self->send_offset, 0);
      if(determine_byte_to_send_considering_escapes(self, destination, &byte_to_send)) {
        self->send_crc = tiny_crc16_byte(self->send_crc, byte_to_send);
        self->send_offset++;
        self->send_state = send_state_payload_length;
      }
      break;
    }

    case send_state_payload_length: {
      if(determine_byte_to_send_considering_escapes(self, self->send_data_length, &byte_to_send)) {
        self->send_crc = tiny_crc16_byte(self->send_crc, byte_to_send);
        self->send_offset++;
        self->send_state = send_state_source;
      }
      break;
    }

    case send_state_source: {
      uint8_t source;
      tiny_queue_peek_partial(&self->send_queue, &source, sizeof(source), self->send_offset, 0);
      if(determine_byte_to_send_considering_escapes(self, source, &byte_to_send)) {
        self->send_crc = tiny_crc16_byte(self->send_crc, byte_to_send);
        self->send_offset++;
        if(self->send_data_length == tiny_gea_packet_transmission_overhead) {
          self->send_state = send_state_crc_msb;
        }
        else {
          self->send_state = send_state_data;
        }
      }
      break;
    }

    case send_state_data: {
      uint8_t data;
      tiny_queue_peek_partial(&self->send_queue, &data, sizeof(data), self->send_offset, 0);
      if(determine_byte_to_send_considering_escapes(self, data, &byte_to_send)) {
        self->send_crc = tiny_crc16_byte(self->send_crc, byte_to_send);
        self->send_offset++;
        if(self->send_offset >= self->send_data_length - data_length_bytes_not_included_in_data) {
          self->send_state = send_state_crc_msb;
        }
      }
      break;
    }

    case send_state_crc_msb:
      byte_to_send = self->send_crc >> 8;
      if(determine_byte_to_send_considering_escapes(self, byte_to_send, &byte_to_send)) {
        self->send_state = send_state_crc_lsb;
      }
      break;

    case send_state_crc_lsb:
      byte_to_send = self->send_crc;
      if(determine_byte_to_send_considering_escapes(self, byte_to_send, &byte_to_send)) {
        self->send_state = send_state_etx;
      }
      break;

    case send_state_etx:
      byte_to_send = tiny_gea_etx;
      self->send_state = send_state_complete;
      break;

    case send_state_complete:
      self->send_completed = true;
      return;
  }

  tiny_uart_send(self->uart, byte_to_send);
}

typedef struct {
  self_t* self;
  uint8_t destination;
  uint8_t payload_length;
  tiny_gea_interface_send_callback_t callback;
  void* context;
  bool set_source_address;
  bool queued;
} send_worker_context_t;

static void send_worker_callback(void* _context, void* buffer)
{
  send_worker_context_t* context = _context;
  tiny_gea_packet_t* packet = buffer;

  packet->payload_length = context->payload_length + tiny_gea_packet_transmission_overhead;
  context->callback(context->context, packet);
  if(context->set_source_address) {
    packet->source = context->self->address;
  }
  packet->destination = context->destination;

  context->queued = tiny_queue_enqueue(&context->self->send_queue, buffer, tiny_gea_packet_overhead + context->payload_length);
}

static bool send_worker(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea_interface_send_callback_t callback,
  void* context,
  bool set_source_address)
{
  reinterpret(self, _self, self_t*);

  send_worker_context_t send_worker_context = {
    .self = self,
    .destination = destination,
    .payload_length = payload_length,
    .callback = callback,
    .context = context,
    .set_source_address = set_source_address,
    .queued = false
  };
  tiny_stack_allocator_allocate_aligned(
    sizeof(tiny_gea_packet_t) + payload_length,
    &send_worker_context,
    send_worker_callback);

  if(!send_worker_context.queued) {
    return false;
  }

  if(!self->send_in_progress) {
    begin_send(self);
  }

  return true;
}

static bool send(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return send_worker(_self, destination, payload_length, callback, context, true);
}

static bool forward(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return send_worker(_self, destination, payload_length, callback, context, false);
}

static i_tiny_event_t* on_receive(i_tiny_gea_interface_t* _self)
{
  reinterpret(self, _self, self_t*);
  return &self->on_receive.interface;
}

static const i_tiny_gea_interface_api_t api = { send, forward, on_receive };

void tiny_gea3_interface_init(
  tiny_gea3_interface_t* self,
  i_tiny_uart_t* uart,
  uint8_t address,
  uint8_t* send_queue_buffer,
  size_t send_queue_buffer_size,
  uint8_t* receive_buffer,
  uint8_t receive_buffer_size,
  bool ignore_destination_address)
{
  self->interface.api = &api;

  self->uart = uart;
  self->address = address;
  self->receive_buffer = receive_buffer;
  self->receive_buffer_size = receive_buffer_size;
  self->ignore_destination_address = ignore_destination_address;
  self->receive_escaped = false;
  self->send_in_progress = false;
  self->send_completed = false;
  self->send_escaped = false;
  self->stx_received = false;
  self->receive_packet_ready = false;
  self->receive_count = 0;

  tiny_event_init(&self->on_receive);

  tiny_queue_init(&self->send_queue, send_queue_buffer, send_queue_buffer_size);

  tiny_event_subscription_init(&self->byte_received_subscription, self, byte_received);
  tiny_event_subscription_init(&self->byte_sent_subscription, self, byte_sent);

  tiny_event_subscribe(tiny_uart_on_receive(uart), &self->byte_received_subscription);
  tiny_event_subscribe(tiny_uart_on_send_complete(uart), &self->byte_sent_subscription);
}

void tiny_gea3_interface_run(self_t* self)
{
  if(self->receive_packet_ready) {
    tiny_gea_interface_on_receive_args_t args;
    args.packet = (const tiny_gea_packet_t*)self->receive_buffer;
    tiny_event_publish(&self->on_receive, &args);

    // Can only be cleared _after_ publication so that the buffer isn't reused
    self->receive_packet_ready = false;
  }

  if(self->send_completed) {
    tiny_queue_discard(&self->send_queue);
    self->send_completed = false;
    self->send_in_progress = false;
  }

  if(!self->send_in_progress) {
    if(tiny_queue_count(&self->send_queue) > 0) {
      begin_send(self);
    }
  }
}
