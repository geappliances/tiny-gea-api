/*!
 * @file
 * @brief
 */

#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_interface_double.hpp"
#include "tiny_utils.h"

static void send(
  i_tiny_gea3_interface_t* _self,
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
}

static i_tiny_event_t* on_receive(i_tiny_gea3_interface_t* _self)
{
  reinterpret(self, _self, tiny_gea3_interface_double_t*);
  return &self->on_receive.interface;
}

static const i_tiny_gea3_interface_api_t api = { send, on_receive };

void tiny_gea3_interface_double_init(tiny_gea3_interface_double_t* self, uint8_t address)
{
  self->interface.api = &api;
  self->address = address;
  tiny_event_init(&self->on_receive);
}

void tiny_gea3_interface_double_trigger_receive(
  tiny_gea3_interface_double_t* self,
  const tiny_gea3_packet_t* packet)
{
  tiny_gea3_interface_on_receive_args_t args = { packet };
  tiny_event_publish(&self->on_receive, &args);
}
