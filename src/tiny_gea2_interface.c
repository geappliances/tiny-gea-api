/*!
 * @file
 * @brief
 */

#include <stdbool.h>
#include "tiny_crc16.h"
#include "tiny_fsm.h"
#include "tiny_gea2_interface.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea_constants.h"
#include "tiny_gea_packet.h"
#include "tiny_stack_allocator.h"
#include "tiny_utils.h"

enum {
  gea2_reflection_timeout_msec = 6,
  gea2_ack_timeout_msec = 8,
  gea2_interbyte_timeout_msec = 6,
};

typedef tiny_gea2_interface_t self_t;

enum {
  signal_byte_received = tiny_fsm_signal_user_start,
  signal_interbyte_timeout,
  signal_send_ready,
  signal_idle_cooldown_timeout,
  signal_reflection_timeout,
  signal_collision_idle_timeout,
  signal_ack_timeout
};

enum {
  data_length_bytes_not_included_in_data = tiny_gea_packet_transmission_overhead - tiny_gea_packet_overhead,
  crc_size = sizeof(uint16_t),
  packet_bytes_not_included_in_payload = crc_size + offsetof(tiny_gea_packet_t, payload),
  unbuffered_bytes = 2 // STX, ETX
};

enum {
  send_state_stx,
  send_state_destination,
  send_state_payload_length,
  send_state_source,
  send_state_data,
  send_state_crc_msb,
  send_state_crc_lsb,
  send_state_etx,
  send_state_done_sending
};

static void state_idle(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_receive(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_idle_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_send(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_wait_for_ack(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_collision_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);

static self_t* interface_from_fsm(tiny_fsm_t* fsm)
{
  return container_of(self_t, fsm, fsm);
}

static void byte_received(void* context, const void* _args)
{
  self_t* self = context;
  const tiny_uart_on_receive_args_t* args = _args;

  tiny_fsm_send_signal(&self->fsm, signal_byte_received, &args->byte);
}

#define needs_escape(_byte) ((_byte & 0xFC) == tiny_gea_esc)

static void state_idle(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* self = interface_from_fsm(fsm);
  switch(signal) {
    case tiny_fsm_signal_entry:
    case signal_send_ready:
      if(!self->send.completed && self->send.in_progress) {
        tiny_fsm_transition(fsm, state_send);
      }
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;

      if(self->receive.packet_ready) {
        break;
      }

      if(*byte == tiny_gea_stx && !self->receive.packet_ready) {
        tiny_fsm_transition(fsm, state_receive);
      }
      else {
        tiny_fsm_transition(fsm, state_idle_cooldown);
      }
    } break;
  }
}

static void reflection_timeout(void* context)
{
  self_t* self = context;
  tiny_fsm_send_signal(&self->fsm, signal_reflection_timeout, NULL);
}

static bool determine_byte_to_send_considering_escapes(self_t* self, uint8_t byte, uint8_t* byte_to_send)
{
  if(!self->send.escaped && needs_escape(byte)) {
    self->send.escaped = true;
    *byte_to_send = tiny_gea_esc;
  }
  else {
    self->send.escaped = false;
    *byte_to_send = byte;
  }

  return !self->send.escaped;
}

static void send_next_byte(self_t* self)
{
  uint8_t byte_to_send = 0;

  tiny_timer_start(
    &self->timer_group,
    &self->timer,
    gea2_reflection_timeout_msec,
    self,
    reflection_timeout);

  switch(self->send.state) {
    case send_state_stx:
      byte_to_send = tiny_gea_stx;
      self->send.state = send_state_data;
      break;

    case send_state_destination: {
      uint8_t destination;
      tiny_queue_peek_partial(&self->send.queue, &destination, sizeof(destination), self->send.offset, 0);
      if(determine_byte_to_send_considering_escapes(self, destination, &byte_to_send)) {
        self->send.crc = tiny_crc16_byte(self->send.crc, byte_to_send);
        self->send.offset++;
        self->send.state = send_state_payload_length;
      }
      break;
    }

    case send_state_payload_length: {
      if(determine_byte_to_send_considering_escapes(self, self->send.data_length, &byte_to_send)) {
        self->send.crc = tiny_crc16_byte(self->send.crc, byte_to_send);
        self->send.offset++;
        self->send.state = send_state_source;
      }
      break;
    }

    case send_state_source: {
      uint8_t source;
      tiny_queue_peek_partial(&self->send.queue, &source, sizeof(source), self->send.offset, 0);
      if(determine_byte_to_send_considering_escapes(self, source, &byte_to_send)) {
        self->send.crc = tiny_crc16_byte(self->send.crc, byte_to_send);
        self->send.offset++;
        if(self->send.data_length == tiny_gea_packet_transmission_overhead) {
          self->send.state = send_state_crc_msb;
        }
        else {
          self->send.state = send_state_data;
        }
      }
      break;
    }

    case send_state_data: {
      uint8_t data;
      tiny_queue_peek_partial(&self->send.queue, &data, sizeof(data), self->send.offset, 0);
      if(determine_byte_to_send_considering_escapes(self, data, &byte_to_send)) {
        self->send.crc = tiny_crc16_byte(self->send.crc, byte_to_send);
        self->send.offset++;
        if(self->send.offset >= self->send.data_length - data_length_bytes_not_included_in_data) {
          self->send.state = send_state_crc_msb;
        }
      }
      break;
    }

    case send_state_crc_msb:
      byte_to_send = self->send.crc >> 8;
      if(determine_byte_to_send_considering_escapes(self, byte_to_send, &byte_to_send)) {
        self->send.state = send_state_crc_lsb;
      }
      break;

    case send_state_crc_lsb:
      byte_to_send = self->send.crc;
      if(determine_byte_to_send_considering_escapes(self, byte_to_send, &byte_to_send)) {
        self->send.state = send_state_etx;
      }
      break;

    case send_state_etx:
      byte_to_send = tiny_gea_etx;
      self->send.state = send_state_done_sending;
      break;
  }

  self->send.expected_reflection = byte_to_send;
  tiny_uart_send(self->uart, byte_to_send);
}

static void handle_send_success(self_t* self)
{
  self->send.completed = true;
  tiny_fsm_transition(&self->fsm, state_idle_cooldown);
}

static void handle_send_failure(self_t* self)
{
  if(self->send.retries > 0) {
    self->send.retries--;
  }
  else {
    self->send.completed = true;
  }

  tiny_fsm_transition(&self->fsm, state_collision_cooldown);
}

static void state_send(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* self = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      self->send.state = send_state_stx;
      self->send.offset = 0;
      self->send.escaped = false;
      self->send.crc = tiny_gea_crc_seed;

      send_next_byte(self);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == self->send.expected_reflection) {
        if(self->send.state == send_state_done_sending) {
          uint8_t destination;
          tiny_queue_peek_partial(&self->send.queue, &destination, sizeof(destination), offsetof(tiny_gea_packet_t, destination), 0);

          if(destination == tiny_gea_broadcast_address) {
            handle_send_success(self);
          }
          else {
            tiny_fsm_transition(fsm, state_wait_for_ack);
          }
        }
        else {
          send_next_byte(self);
        }
      }
      else {
        handle_send_failure(self);
      }
    } break;

    case signal_reflection_timeout:
      handle_send_failure(self);
      tiny_fsm_transition(fsm, state_idle_cooldown);
      break;
  }
}

static void ack_timeout(void* context)
{
  self_t* self = context;
  tiny_fsm_send_signal(&self->fsm, signal_ack_timeout, NULL);
}

static void start_ack_timeout_timer(self_t* self)
{
  tiny_timer_start(
    &self->timer_group,
    &self->timer,
    gea2_ack_timeout_msec,
    self,
    ack_timeout);
}

static void state_wait_for_ack(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* self = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      start_ack_timeout_timer(self);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == tiny_gea_ack) {
        handle_send_success(self);
      }
      else {
        handle_send_failure(self);
      }
    } break;

    case signal_ack_timeout:
      handle_send_failure(self);
      break;
  }
}

static bool received_packet_has_valid_crc(self_t* self)
{
  return self->receive.crc == 0;
}

static bool received_packet_has_minimum_valid_length(self_t* self)
{
  return self->receive.count >= packet_bytes_not_included_in_payload;
}

static bool received_packet_has_valid_length(self_t* self)
{
  reinterpret(packet, self->receive.buffer, tiny_gea_packet_t*);
  return (packet->payload_length == self->receive.count + unbuffered_bytes);
}

static void buffer_received_byte(self_t* self, uint8_t byte)
{
  if(self->receive.count == 0) {
    self->receive.crc = tiny_gea_crc_seed;
  }

  if(self->receive.count < self->receive.buffer_size) {
    self->receive.buffer[self->receive.count++] = byte;

    self->receive.crc = tiny_crc16_byte(
      self->receive.crc,
      byte);
  }
}

static bool received_packet_is_addressed_to_me(self_t* self)
{
  reinterpret(packet, self->receive.buffer, tiny_gea_packet_t*);
  return (packet->destination == self->address) ||
    (packet->destination == tiny_gea_broadcast_address) ||
    self->ignore_destination_address;
}

static void send_ack(self_t* self, uint8_t address)
{
  if(address != tiny_gea_broadcast_address) {
    tiny_uart_send(self->uart, tiny_gea_ack);
  }
}

static void process_received_byte(self_t* self, const uint8_t byte)
{
  reinterpret(packet, self->receive.buffer, tiny_gea_packet_t*);

  if(self->receive.escaped) {
    self->receive.escaped = false;
    buffer_received_byte(self, byte);
    return;
  }

  switch(byte) {
    case tiny_gea_esc:
      self->receive.escaped = true;
      break;

    case tiny_gea_stx:
      self->receive.count = 0;
      break;

    case tiny_gea_etx:
      if(!received_packet_has_minimum_valid_length(self) || !received_packet_has_valid_length(self)) {
        break;
      }

      if(!received_packet_has_valid_crc(self)) {
        break;
      }

      if(!received_packet_is_addressed_to_me(self)) {
        break;
      }

      packet->payload_length -= tiny_gea_packet_transmission_overhead;
      self->receive.packet_ready = true;

      send_ack(self, packet->destination);

      tiny_fsm_transition(&self->fsm, state_idle_cooldown);
      break;

    default:
      buffer_received_byte(self, byte);
      break;
  }
}

static tiny_timer_ticks_t get_collision_timeout(uint8_t address, uint8_t pseudo_random_number)
{
  return 43 + (address & 0x1F) + ((pseudo_random_number ^ address) & 0x1F);
}

static void collision_idle_timeout(void* context)
{
  self_t* self = context;
  tiny_fsm_send_signal(&self->fsm, signal_collision_idle_timeout, NULL);
}

static void start_collision_idle_timeout_timer(self_t* self)
{
  uint8_t current_ticks = (uint8_t)tiny_time_source_ticks(self->timer_group.time_source);
  tiny_timer_ticks_t collision_timeout_ticks = get_collision_timeout(self->address, current_ticks);

  tiny_timer_start(
    &self->timer_group,
    &self->timer,
    collision_timeout_ticks,
    self,
    collision_idle_timeout);
}

static void state_collision_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* self = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      start_collision_idle_timeout_timer(self);
      break;

    case signal_collision_idle_timeout:
      tiny_fsm_transition(fsm, state_idle);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == tiny_gea_stx) {
        tiny_fsm_transition(fsm, state_receive);
      }
    } break;
  }
}

static void interbyte_timeout(void* context)
{
  self_t* self = context;
  tiny_fsm_send_signal(&self->fsm, signal_interbyte_timeout, NULL);
}

static void start_interbyte_timeout_timer(self_t* self)
{
  tiny_timer_start(
    &self->timer_group,
    &self->timer,
    gea2_interbyte_timeout_msec,
    self,
    interbyte_timeout);
}

static void state_receive(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* self = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      self->receive.count = 0;
      start_interbyte_timeout_timer(self);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      start_interbyte_timeout_timer(self);
      process_received_byte(self, *byte);
      break;
    }

    case signal_interbyte_timeout:
      tiny_fsm_transition(fsm, state_idle_cooldown);
      break;
  }
}

static void idle_cooldown_timeout(void* context)
{
  self_t* self = context;
  tiny_fsm_send_signal(&self->fsm, signal_idle_cooldown_timeout, NULL);
}

static tiny_timer_ticks_t get_idle_timeout(uint8_t address)
{
  return 10 + (address & 0x1F);
}

static void state_idle_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* self = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:

      tiny_timer_start(
        &self->timer_group,
        &self->timer,
        get_idle_timeout(self->address),
        self,
        idle_cooldown_timeout);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == tiny_gea_stx && !self->receive.packet_ready) {
        tiny_fsm_transition(fsm, state_receive);
      }
      else {
        tiny_fsm_transition(fsm, state_idle_cooldown);
      }

      break;
    }

    case signal_idle_cooldown_timeout:
      tiny_fsm_transition(fsm, state_idle);
      break;

    case tiny_fsm_signal_exit:
      break;
  }
}

static void begin_send(self_t* self)
{
  tiny_queue_peek_partial(&self->send.queue, &self->send.data_length, sizeof(self->send.data_length), offsetof(tiny_gea_packet_t, payload_length), 0);
  self->send.state = send_state_destination;
  self->send.offset = 0;
  self->send.in_progress = true;
  self->send.packet_queued_in_background = true;
  self->send.retries = self->retries;
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

  context->queued = tiny_queue_enqueue(&context->self->send.queue, buffer, tiny_gea_packet_overhead + context->payload_length);
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

  if(!self->send.in_progress) {
    begin_send(self);
  }

  return true;
}

static void msec_interrupt_callback(void* context, const void* _args)
{
  self_t* self = context;
  (void)_args;

  if(self->send.packet_queued_in_background) {
    self->send.packet_queued_in_background = false;
    tiny_fsm_send_signal(&self->fsm, signal_send_ready, NULL);
  }

  tiny_timer_group_run(&self->timer_group);
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

static i_tiny_event_t* get_on_receive_event(i_tiny_gea_interface_t* _self)
{
  reinterpret(self, _self, self_t*);
  return &self->on_receive.interface;
}

static const i_tiny_gea_interface_api_t api = { send, forward, get_on_receive_event };

void tiny_gea2_interface_init(
  tiny_gea2_interface_t* self,
  i_tiny_uart_t* uart,
  i_tiny_time_source_t* time_source,
  i_tiny_event_t* msec_interrupt,
  uint8_t address,
  uint8_t* receive_buffer,
  uint8_t receive_buffer_size,
  uint8_t* send_queue_buffer,
  size_t send_queue_buffer_size,
  bool ignore_destination_address,
  uint8_t retries)
{
  self->interface.api = &api;
  self->uart = uart;
  self->address = address;
  self->ignore_destination_address = ignore_destination_address;
  self->receive.buffer = receive_buffer;
  self->receive.buffer_size = receive_buffer_size;
  self->receive.packet_ready = false;
  self->receive.escaped = false;
  self->send.in_progress = false;
  self->send.completed = false;
  self->send.packet_queued_in_background = false;
  self->retries = retries;

  tiny_queue_init(&self->send.queue, send_queue_buffer, send_queue_buffer_size);

  tiny_timer_group_init(&self->timer_group, time_source);

  tiny_event_subscription_init(&self->byte_received_subscription, self, byte_received);
  tiny_event_subscribe(tiny_uart_on_receive(uart), &self->byte_received_subscription);

  tiny_event_subscription_init(&self->msec_interrupt_subscription, self, msec_interrupt_callback);
  tiny_event_subscribe(msec_interrupt, &self->msec_interrupt_subscription);

  tiny_event_init(&self->on_receive);
  tiny_event_init(&self->on_diagnostics_event);

  tiny_fsm_init(&self->fsm, state_idle);
}

void tiny_gea2_interface_run(self_t* self)
{
  if(self->receive.packet_ready) {
    tiny_gea_interface_on_receive_args_t args;
    args.packet = (const tiny_gea_packet_t*)self->receive.buffer;

    tiny_event_publish(&self->on_receive, &args);
    self->receive.packet_ready = false;
  }

  if(self->send.completed) {
    tiny_queue_discard(&self->send.queue);
    self->send.in_progress = false;
    self->send.completed = false;
  }

  if(!self->send.in_progress) {
    if(tiny_queue_count(&self->send.queue) > 0) {
      begin_send(self);
    }
  }
}
