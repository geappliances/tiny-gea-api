/*!
 * @file
 * @brief
 */

#include <stdbool.h>
#include "tiny_crc16.h"
#include "tiny_fsm.h"
#include "tiny_gea3_constants.h"
#include "tiny_gea3_interface.h"
#include "tiny_utils.h"

typedef tiny_gea2_interface_single_wire_t self_t;

// Send packet should match tiny_gea3_packet_t, but stores data_length (per spec) instead of payload_length
// (used application convenience)
typedef struct {
  uint8_t destination;
  uint8_t data_length;
  uint8_t source;
  uint8_t data[1];
} send_packet_t;

enum {
  signal_byte_received = tiny_fsm_signal_user_start,
  signal_inter_byte_timeout,
  signal_send_ready,
  signal_idle_cooldown_timeout,
  signal_reflection_timeout,
  signal_collision_idle_timeout,
  signal_ack_timeout
};

enum {
  send_packet_header_size = offsetof(send_packet_t, data),
  data_length_bytes_not_included_in_data = tiny_gea3_packet_transmission_overhead - tiny_gea3_packet_overhead,
  crc_size = sizeof(uint16_t),
  packet_bytes_not_included_in_payload = crc_size + offsetof(tiny_gea3_packet_t, payload),
  unbuffered_bytes = 2 // STX, ETX
};

enum {
  send_state_data,
  send_state_crc_msb,
  send_state_crc_lsb,
  send_state_etx
};

static instance_t* interface_from_fsm(tiny_fsm_t* fsm)
{
  return container_of(instance_t, _private.fsm, fsm);
}

#define needs_escape(_byte) ((_byte & 0xFC) == tiny_gea3_esc)

#define is_broadcast_address(_address) ((GEA2_BCAST_MASK & _address) == GEA2_BCAST_MASK)

static void publish_diagnostics_event(Instance_t* instance, tiny_gea2 type)
{
  TinyGea2InterfaceOnDiagnosticsEventArgs_t args = { .type = type };
  tiny_event_publish(&instance->_private.onDiagnosticsEvent, &args);
}

static void state_idle(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  instance_t* instance = interface_from_fsm(fsm);
  switch(signal) {
    case tiny_fsm_signal_entry:
    case signal_send_ready:
      if(instance->_private.send.active) {
        tiny_fsm_transition(fsm, state_send);
      }
      break;

    case signal_byte_received: {
      reinterpret(byte, data, const uint8_t*);

      if(instance->_private.receive.packetReady) {
        publish_diagnostics_event(instance, TinyGea2InterfaceDiagnosticsEventType_ReceivedByteDroppedBecauseAPacketWasPendingPublication);
      }

      if(*byte == Gea2Stx && !instance->_private.receive.packetReady) {
        tiny_fsm_transition(fsm, State_Receive);
      }
      else {
        tiny_fsm_transition(fsm, State_IdleCooldown);
      }
    } break;
  }
}

static bool determine_byte_to_send_considering_escapes(Instance_t* instance, uint8_t byte, uint8_t* byteToSend)
{
  if(!instance->_private.send.escaped && needs_escape(byte)) {
    instance->_private.send.escaped = true;
    *byteToSend = Gea2Esc;
  }
  else {
    instance->_private.send.escaped = false;
    *byteToSend = byte;
  }

  return !instance->_private.send.escaped;
}

static void send_next_byte(Instance_t* instance)
{
  uint8_t byteToSend = 0;

  tiny_timer_module_start(
    &instance->_private.timerModule,
    &instance->_private.timer,
    GEA2_REFLECTION_TIMEOUT_MSEC,
    reflection_timeout,
    instance);

  switch(instance->_private.send.state) {
    case send_state_stx:
      byteToSend = Gea2Stx;
      instance->_private.send.state = send_state_data;
      break;

    case send_state_data:
      if(DetermineByteToSendConsideringEscapes(instance, instance->_private.send.buffer[instance->_private.send.offset], &byteToSend)) {
        reinterpret(sendPacket, instance->_private.send.buffer, const send_packet_t*);
        instance->_private.send.offset++;

        if(instance->_private.send.offset >= sendPacket->dataLength - DataLengthBytesNotIncludedInData) {
          instance->_private.send.state = send_state_crc_msb;
        }
      }
      break;

    case send_state_crc_msb:
      byteToSend = instance->_private.send.crc >> 8;
      if(DetermineByteToSendConsideringEscapes(instance, byteToSend, &byteToSend)) {
        instance->_private.send.state = send_state_crc_lsb;
      }
      break;

    case send_state_crc_lsb:
      byteToSend = instance->_private.send.crc;
      if(DetermineByteToSendConsideringEscapes(instance, byteToSend, &byteToSend)) {
        instance->_private.send.state = send_state_etx;
      }
      break;

    case send_state_etx:
      byteToSend = Gea2Etx;
      instance->_private.send.state = send_state_done;
      break;
  }

  instance->_private.send.expectedReflection = byteToSend;
  TinyUart_Send(instance->_private.uart, byteToSend);
}

static void handle_send_failure(Instance_t* instance)
{
  if(instance->_private.send.retries > 0) {
    instance->_private.send.retries--;
  }
  else {
    instance->_private.send.active = false;
  }

  tiny_fsm_transition(&instance->_private.fsm, State_CollisionCooldown);
}

static void reflection_timeout(void* context, TinyTimerModule_t* timerModule)
{
  (void)timerModule;

  Instance_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, Signal_reflection_timeout, NULL);
}

static void state_send(TinyFsm_t* fsm, const TinyFsmSignal_t signal, const void* data)
{
  Instance_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      instance->_private.send.state = send_state_stx;
      instance->_private.send.offset = 0;
      instance->_private.send.escaped = false;

      send_next_byte(instance);
      break;

    case signal_byte_received: {
      reinterpret(byte, data, const uint8_t*);
      if(*byte == instance->_private.send.expectedReflection) {
        if(instance->_private.send.state == send_state_done) {
          PublishDiagnosticsEvent(instance, TinyGea2InterfaceDiagnosticsEventType_PacketSent);

          send_packet_t* sendPacket = (send_packet_t*)instance->_private.send.buffer;

          if(is_broadcast_address(sendPacket->destination)) {
            instance->_private.send.active = false;
            tiny_fsm_transition(fsm, State_IdleCooldown);
          }
          else {
            tiny_fsm_transition(fsm, State_WaitForAck);
          }
        }
        else {
          send_next_byte(instance);
        }
      }
      else {
        PublishDiagnosticsEvent(instance, TinyGea2InterfaceDiagnosticsEventType_SingleWireCollisionDetected);
        HandleSendFailure(instance);
      }
    } break;

    case Signal_reflection_timeout:
      PublishDiagnosticsEvent(instance, TinyGea2InterfaceDiagnosticsEventType_SingleWireReflectionTimedOut);
      HandleSendFailure(instance);
      tiny_fsm_transition(fsm, State_IdleCooldown);
      break;
  }
}

static void handle_success(Instance_t* instance)
{
  instance->_private.send.active = false;
  tiny_fsm_transition(&instance->_private.fsm, State_IdleCooldown);
}

static void ack_timeout(void* context, TinyTimerModule_t* timerModule)
{
  (void)timerModule;

  Instance_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, Signal_AckTimeout, NULL);
}

static void start_ack_timeout_timer(Instance_t* instance)
{
  tiny_timer_module_start(
    &instance->_private.timerModule,
    &instance->_private.timer,
    Gea2AckTimeoutMsec,
    AckTimeout,
    instance);
}
static void state_wait_for_ack(TinyFsm_t* fsm, const TinyFsmSignal_t signal, const void* data)
{
  Instance_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      StartAckTimeoutTimer(instance);
      break;

    case signal_byte_received: {
      reinterpret(byte, data, const uint8_t*);
      if(*byte == Gea2Ack) {
        HandleSuccess(instance);
      }
      else {
        HandleSendFailure(instance);
      }
    } break;

    case Signal_AckTimeout:
      HandleSendFailure(instance);
      break;
  }
}

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
  reinterpret(packet, self->receive_buffer, tiny_gea3_packet_t*);
  return (packet->payload_length == self->receive_count + unbuffered_bytes);
}

static bool received_packet_is_addressed_to_me(self_t* self)
{
  reinterpret(packet, self->receive_buffer, tiny_gea3_packet_t*);
  return (packet->destination == self->address) || (packet->destination == tiny_gea3_broadcast_address);
}

////////// something

static void buffer_received_byte(self_t* self, uint8_t byte)
{
  if(self->receive_count == 0) {
    self->receive_crc = tiny_gea3_crc_seed;
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
  reinterpret(packet, self->receive_buffer, tiny_gea3_packet_t*);
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
    case tiny_gea3_esc:
      self->receive_escaped = true;
      break;

    case tiny_gea3_stx:
      self->receive_count = 0;
      self->stx_received = true;
      break;

    case tiny_gea3_etx:
      if(self->stx_received &&
        received_packet_has_minimum_valid_length(self) &&
        received_packet_has_valid_length(self) &&
        received_packet_has_valid_crc(self) &&
        received_packet_is_addressed_to_me(self)) {
        packet->payload_length -= tiny_gea3_packet_transmission_overhead;
        self->receive_packet_ready = true;
      }
      self->stx_received = false;
      break;

    default:
      buffer_received_byte(self, byte);
      break;
  }
}

static bool determine_byte_to_send_considering_escapes(self_t* self, uint8_t byte, uint8_t* byteToSend)
{
  if(!self->send_escaped && needs_escape(byte)) {
    self->send_escaped = true;
    *byteToSend = tiny_gea3_esc;
  }
  else {
    self->send_escaped = false;
    *byteToSend = byte;
  }

  return !self->send_escaped;
}

static void prepare_buffered_packet_for_transmission(self_t* self)
{
  reinterpret(sendPacket, self->send_buffer, send_packet_t*);
  sendPacket->data_length += tiny_gea3_packet_transmission_overhead;
  self->send_crc = tiny_crc16_block(tiny_gea3_crc_seed, (const uint8_t*)sendPacket, sendPacket->data_length - data_length_bytes_not_included_in_data);
  self->send_state = send_state_data;
  self->send_offset = 0;
}

static void byte_sent(void* context, const void* args)
{
  reinterpret(self, context, self_t*);
  (void)args;

  if(!self->send_in_progress) {
    if(tiny_queue_count(&self->send_queue) > 0) {
      uint16_t size;
      tiny_queue_dequeue(&self->send_queue, self->send_buffer, &size);
      prepare_buffered_packet_for_transmission(self);
      self->send_in_progress = true;
      tiny_uart_send(self->uart, tiny_gea3_stx);
    }
    return;
  }

  uint8_t byteToSend = 0;

  switch(self->send_state) {
    case send_state_data:
      if(determine_byte_to_send_considering_escapes(self, self->send_buffer[self->send_offset], &byteToSend)) {
        reinterpret(sendPacket, self->send_buffer, const send_packet_t*);
        self->send_offset++;

        if(self->send_offset >= sendPacket->data_length - data_length_bytes_not_included_in_data) {
          self->send_state = send_state_crc_msb;
        }
      }
      break;

    case send_state_crc_msb:
      byteToSend = self->send_crc >> 8;
      if(determine_byte_to_send_considering_escapes(self, byteToSend, &byteToSend)) {
        self->send_state = send_state_crc_lsb;
      }
      break;

    case send_state_crc_lsb:
      byteToSend = self->send_crc;
      if(determine_byte_to_send_considering_escapes(self, byteToSend, &byteToSend)) {
        self->send_state = send_state_etx;
      }
      break;

    case send_state_etx:
      self->send_in_progress = false;
      byteToSend = tiny_gea3_etx;
      break;
  }

  tiny_uart_send(self->uart, byteToSend);
}

static void populate_send_packet(
  self_t* self,
  tiny_gea3_packet_t* packet,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea3_interface_send_callback_t callback,
  void* context,
  bool setSourceAddress)
{
  packet->payload_length = payload_length;
  callback(context, (tiny_gea3_packet_t*)packet);
  if(setSourceAddress) {
    packet->source = self->address;
  }
  packet->destination = destination;
}

static void send_worker(
  i_tiny_gea3_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea3_interface_send_callback_t callback,
  void* context,
  bool setSourceAddress)
{
  reinterpret(self, _self, self_t*);

  if(payload_length + send_packet_header_size > self->send_buffer_size) {
    return;
  }

  if(self->send_in_progress) {
    uint8_t buffer[255];
    populate_send_packet(self, (tiny_gea3_packet_t*)buffer, destination, payload_length, callback, context, setSourceAddress);
    tiny_queue_enqueue(&self->send_queue, buffer, tiny_gea3_packet_overhead + payload_length);
  }
  else {
    reinterpret(sendPacket, self->send_buffer, tiny_gea3_packet_t*);
    populate_send_packet(self, sendPacket, destination, payload_length, callback, context, setSourceAddress);
    prepare_buffered_packet_for_transmission(self);
    self->send_in_progress = true;
    tiny_uart_send(self->uart, tiny_gea3_stx);
  }
}

static void send(
  i_tiny_gea3_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea3_interface_send_callback_t callback,
  void* context)
{
  send_worker(_self, destination, payload_length, callback, context, true);
}

static i_tiny_event_t* on_receive(i_tiny_gea3_interface_t* _self)
{
  reinterpret(self, _self, self_t*);
  return &self->on_receive.interface;
}

static const i_tiny_gea3_interface_api_t api = { send, on_receive };

void tiny_gea3_interface_init(
  tiny_gea3_interface_t* self,
  i_tiny_uart_t* uart,
  uint8_t address,
  uint8_t* send_buffer,
  uint8_t send_buffer_size,
  uint8_t* receive_buffer,
  uint8_t receive_buffer_size,
  uint8_t* send_queue_buffer,
  size_t send_queue_buffer_size)
{
  self->interface.api = &api;

  self->uart = uart;
  self->address = address;
  self->send_buffer = send_buffer;
  self->send_buffer_size = send_buffer_size;
  self->receive_buffer = receive_buffer;
  self->receive_buffer_size = receive_buffer_size;
  self->receive_escaped = false;
  self->send_in_progress = false;
  self->send_escaped = false;
  self->stx_received = false;
  self->receive_packet_ready = false;
  self->receive_count = 0;

  tiny_event_init(&self->on_receive);

  tiny_queue_init(&self->send_queue, send_queue_buffer, send_queue_buffer_size);

  tiny_event_subscription_init(&self->byte_received_subscfription, self, byte_received);
  tiny_event_subscription_init(&self->byte_sent_subscription, self, byte_sent);

  tiny_event_subscribe(tiny_uart_on_receive(uart), &self->byte_received_subscfription);
  tiny_event_subscribe(tiny_uart_on_send_complete(uart), &self->byte_sent_subscription);
}

void tiny_gea3_interface_run(self_t* self)
{
  if(self->receive_packet_ready) {
    tiny_gea3_interface_on_receive_args_t args;
    args.packet = (const tiny_gea3_packet_t*)self->receive_buffer;
    tiny_event_publish(&self->on_receive, &args);

    // Can only be cleared _after_ publication so that the buffer isn't reused
    self->receive_packet_ready = false;
  }
}
