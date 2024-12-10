/*!
 * @file
 * @brief
 */

#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_erd_client_double.hpp"

static bool read(
  i_tiny_gea3_erd_client_t* self,
  tiny_gea3_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd)
{
  return mock()
    .actualCall("read")
    .onObject(self)
    .withOutputParameter("request_id", request_id)
    .withParameter("address", address)
    .withParameter("erd", erd)
    .returnBoolValueOrDefault(true);
}

static bool write(
  i_tiny_gea3_erd_client_t* self,
  tiny_gea3_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd,
  const void* data,
  uint8_t dataSize)
{
  return mock()
    .actualCall("write")
    .onObject(self)
    .withOutputParameter("request_id", request_id)
    .withParameter("address", address)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", reinterpret_cast<const unsigned char*>(data), dataSize)
    .returnBoolValueOrDefault(true);
}

static bool subscribe(i_tiny_gea3_erd_client_t* self, uint8_t address)
{
  return mock()
    .actualCall("subscribe")
    .onObject(self)
    .withParameter("address", address)
    .returnBoolValueOrDefault(true);
}

static bool retain_subscription(i_tiny_gea3_erd_client_t* self, uint8_t address)
{
  return mock()
    .actualCall("retain_subscription")
    .onObject(self)
    .withParameter("address", address)
    .returnBoolValueOrDefault(true);
}

static i_tiny_event_t* on_activity(i_tiny_gea3_erd_client_t* _self)
{
  auto self = reinterpret_cast<tiny_gea3_erd_client_double_t*>(_self);
  return &self->on_activity.interface;
}

static const i_tiny_gea3_erd_client_api_t api = {
  read,
  write,
  subscribe,
  retain_subscription,
  on_activity
};

void tiny_gea3_erd_client_double_init(tiny_gea3_erd_client_double_t* self)
{
  self->interface.api = &api;
  tiny_event_init(&self->on_activity);
}

void tiny_gea3_erd_client_double_trigger_activity_event(tiny_gea3_erd_client_double_t* self, const tiny_gea3_erd_client_on_activity_args_t* args)
{
  tiny_event_publish(&self->on_activity, args);
}
