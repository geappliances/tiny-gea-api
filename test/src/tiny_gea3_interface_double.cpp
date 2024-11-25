/*!
 * @file
 * @brief
 */

#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_interface_double.hpp"
#include "tiny_utils.h"

static bool send(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea3_interface_send_callback_t callback,
  void* context)
{
  reinterpret(self, _self, tiny_gea3_interface_double_t*);
  self->packet.destination = destination;
  self->packet.payload_length = payload_length;
  callback(context, &self->packet);
  self->packet.source = self->address;

  mock()
    .actualCall("send")
    .onObject(self)
    .withParameter("source", self->packet.source)
    .withParameter("destination", self->packet.destination)
    .withMemoryBufferParameter("payload", self->packet.payload, payload_length);

  return true;
}

static bool forward(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea3_interface_send_callback_t callback,
  void* context)
{
  reinterpret(self, _self, tiny_gea3_interface_double_t*);
  self->packet.destination = destination;
  self->packet.payload_length = payload_length;
  callback(context, &self->packet);

  mock()
    .actualCall("forward")
    .onObject(self)
    .withParameter("source", self->packet.source)
    .withParameter("destination", self->packet.destination)
    .withMemoryBufferParameter("payload", self->packet.payload, payload_length);

  return true;
}

static i_tiny_event_t* on_receive(i_tiny_gea_interface_t* _self)
{
  reinterpret(self, _self, tiny_gea3_interface_double_t*);
  return &self->on_receive.interface;
}

static const i_tiny_gea_interface_api_t api = { send, forward, on_receive };

void tiny_gea3_interface_double_init(tiny_gea3_interface_double_t* self, uint8_t address)
{
  self->interface.api = &api;
  self->address = address;
  tiny_event_init(&self->on_receive);
}

void tiny_gea3_interface_double_trigger_receive(
  tiny_gea3_interface_double_t* self,
  const tiny_gea_packet_t* packet)
{
  tiny_gea3_interface_on_receive_args_t args = { packet };
  tiny_event_publish(&self->on_receive, &args);
}
