/*!
 * @file
 * @brief
 */

#include <string.h>
#include "tiny_gea2_erd_api.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_gea_constants.h"
#include "tiny_stack_allocator.h"
#include "tiny_utils.h"

enum {
  request_type_read,
  request_type_write,
  request_type_invalid
};
typedef uint8_t request_type_t;

typedef struct
{
  request_type_t type;
} request_t;

// These request types need to have no padding _or_ we need to memset them to 0
// since we memcmp the requests to detect duplicates

typedef struct
{
  request_type_t type;
  uint8_t address;
  tiny_erd_t erd;
} read_request_t;

typedef struct
{
  request_type_t type;
  uint8_t address;
  tiny_erd_t erd;
  uint8_t data_size;
  uint8_t data[1];
} write_request_t;

enum {
  request_retries = 2,
  send_retries = 2,
  write_request_overhead = 5
};

typedef bool (*requests_conflict_predicate_t)(const request_t* new_request, const request_t* queued_request);

typedef tiny_gea2_erd_client_t self_t;

typedef struct {
  read_request_t* request;
} read_request_worker_context_t;

static void send_read_request_worker(void* _context, tiny_gea_packet_t* packet)
{
  read_request_worker_context_t* context = _context;
  read_request_t* request = context->request;
  reinterpret(read_request_payload, packet->payload, tiny_gea2_erd_api_read_request_payload_t*);

  read_request_payload->command = tiny_gea2_erd_api_command_read_request;
  read_request_payload->erd_count = 1;
  read_request_payload->erd_msb = request->erd >> 8;
  read_request_payload->erd_lsb = request->erd & 0xFF;
}

static void send_read_request(self_t* self)
{
  read_request_t request;
  uint16_t size;

  tiny_queue_peek(&self->request_queue, &request, &size, 0);

  read_request_worker_context_t context = { &request };

  tiny_gea_interface_send(
    self->gea2_interface,
    request.address,
    sizeof(tiny_gea2_erd_api_read_request_payload_t),
    &context,
    send_read_request_worker);
}

static void send_write_request_worker(void* context, tiny_gea_packet_t* packet)
{
  self_t* self = context;
  reinterpret(payload, packet->payload, tiny_gea2_erd_api_write_request_payload_t*);

  write_request_t request;
  tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

  uint16_t size;
  tiny_queue_peek(&self->request_queue, packet->payload, &size, 0);

  packet->destination = request.address;
  payload->header.command = tiny_gea2_erd_api_command_write_request;
  payload->header.erd_count = 1;
  payload->header.erd_msb = request.erd >> 8;
  payload->header.erd_lsb = request.erd & 0xFF;
  payload->header.data_size = request.data_size;
}

static void send_write_request(self_t* self)
{
  write_request_t request;
  tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

  tiny_gea_interface_send(
    self->gea2_interface,
    request.address,
    sizeof(tiny_gea2_erd_api_write_request_payload_header_t) + request.data_size,
    self,
    send_write_request_worker);
}

static void resend_request(self_t* self);

static void request_timed_out(void* context)
{
  reinterpret(self, context, self_t*);
  resend_request(self);
}

static void arm_request_timeout(self_t* self)
{
  tiny_timer_start(
    self->timer_group,
    &self->request_retry_timer,
    self->configuration->request_timeout,
    self,
    request_timed_out);
}

static void disarm_request_timeout(self_t* self)
{
  tiny_timer_stop(
    self->timer_group,
    &self->request_retry_timer);
}

static bool request_pending(self_t* self)
{
  return tiny_queue_count(&self->request_queue) > 0;
}

static request_type_t request_type(self_t* self)
{
  if(request_pending(self)) {
    request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);
    return request.type;
  }
  else {
    return request_type_invalid;
  }
}

static void send_request(self_t* self)
{
  switch(request_type(self)) {
    case request_type_read:
      send_read_request(self);
      break;

    case request_type_write:
      send_write_request(self);
      break;
  }

  arm_request_timeout(self);
}

static void send_request_if_not_busy(self_t* self)
{
  if(!self->busy && request_pending(self)) {
    self->busy = true;
    self->remaining_retries = self->configuration->request_retries;
    send_request(self);
  }
}

static void finish_request(self_t* self)
{
  tiny_queue_discard(&self->request_queue);
  disarm_request_timeout(self);
  self->busy = false;
  self->request_id++;
  send_request_if_not_busy(self);
}

static void handle_read_failure(self_t* self)
{
  read_request_t request;
  uint16_t size;
  tiny_queue_peek(&self->request_queue, &request, &size, 0);

  tiny_gea2_erd_client_on_activity_args_t args;
  args.address = request.address;
  args.type = tiny_gea2_erd_client_activity_type_read_failed;
  args.read_failed.request_id = self->request_id;
  args.read_failed.erd = request.erd;
  args.read_failed.reason = tiny_gea2_erd_client_read_failure_reason_retries_exhausted;

  finish_request(self);

  tiny_event_publish(&self->on_activity, &args);
}

static void handle_write_failure_worker(void* _context, void* allocated_block)
{
  self_t* self = _context;
  reinterpret(request, allocated_block, write_request_t*);

  uint16_t size;
  tiny_queue_peek(&self->request_queue, request, &size, 0);

  tiny_gea2_erd_client_on_activity_args_t args;
  args.address = request->address;
  args.type = tiny_gea2_erd_client_activity_type_write_failed;
  args.write_failed.request_id = self->request_id;
  args.write_failed.erd = request->erd;
  args.write_failed.data = request->data;
  args.write_failed.data_size = request->data_size;
  args.write_failed.reason = tiny_gea2_erd_client_read_failure_reason_retries_exhausted;

  finish_request(self);

  tiny_event_publish(&self->on_activity, &args);
}

static void handle_write_failure(self_t* self)
{
  write_request_t request;
  tiny_queue_peek_partial(&self->request_queue, &request, offsetof(write_request_t, data), 0);

  tiny_stack_allocator_allocate_aligned(request.data_size + offsetof(write_request_t, data), self, handle_write_failure_worker);
}

static void fail_request(self_t* self)
{
  switch(request_type(self)) {
    case request_type_read:
      handle_read_failure(self);
      break;

    case request_type_write:
      handle_write_failure(self);
      break;
  }
}

static void resend_request(self_t* self)
{
  if(self->remaining_retries > 0) {
    self->remaining_retries--;
    send_request(self);
  }
  else {
    fail_request(self);
  }
}

static void handle_read_response_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  if(request_type(self) == request_type_read) {
    read_request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

    if(packet->payload_length >= sizeof(tiny_gea2_erd_api_read_response_payload_t)) {
      reinterpret(payload, packet->payload, const tiny_gea2_erd_api_read_response_payload_t*);
      tiny_erd_t erd = (payload->header.erd_msb << 8) + payload->header.erd_lsb;

      if((payload->header.erd_count == 1) &&
        ((request.address == packet->source) || (request.address == tiny_gea_broadcast_address)) &&
        (request.erd == erd)) {
        tiny_gea2_erd_client_on_activity_args_t args;
        args.address = packet->source;
        args.type = tiny_gea2_erd_client_activity_type_read_completed;
        args.read_completed.request_id = self->request_id;
        args.read_completed.erd = erd;
        args.read_completed.data_size = payload->header.data_size;
        args.read_completed.data = payload->data;

        finish_request(self);

        tiny_event_publish(&self->on_activity, &args);
      }
    }
  }
}

typedef struct
{
  self_t* self;
  uint8_t client_address;
} handle_write_response_packet_context_t;

static void handle_write_response_packet_worker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, handle_write_response_packet_context_t*);
  reinterpret(request, allocated_block, write_request_t*);

  uint16_t size;
  tiny_queue_peek(&context->self->request_queue, request, &size, 0);

  tiny_gea2_erd_client_on_activity_args_t args;
  args.address = context->client_address;
  args.type = tiny_gea2_erd_client_activity_type_write_completed;
  args.write_completed.request_id = context->self->request_id;
  args.write_completed.erd = request->erd;
  args.write_completed.data = request->data;
  args.write_completed.data_size = request->data_size;

  finish_request(context->self);

  tiny_event_publish(&context->self->on_activity, &args);
}

static void handle_write_response_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  if(request_type(self) == request_type_write) {
    write_request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, offsetof(write_request_t, data), 0);

    if(packet->payload_length == sizeof(tiny_gea2_erd_api_write_response_payload_t)) {
      reinterpret(payload, packet->payload, const tiny_gea2_erd_api_write_response_payload_t*);
      tiny_erd_t erd = (payload->erd_msb << 8) + payload->erd_lsb;

      if((payload->erd_count == 1) &&
        ((request.address == packet->source) || (request.address == tiny_gea_broadcast_address)) &&
        (request.erd == erd)) {
        handle_write_response_packet_context_t context = { self, .client_address = packet->source };
        tiny_stack_allocator_allocate_aligned(request.data_size + offsetof(write_request_t, data), &context, handle_write_response_packet_worker);
      }
    }
  }
}

static bool valid_read_response(const tiny_gea_packet_t* packet)
{
  if(packet->payload_length < sizeof(tiny_gea2_erd_api_read_response_payload_header_t)) {
    return false;
  }

  reinterpret(payload, packet->payload, const tiny_gea2_erd_api_read_response_payload_t*);

  return (payload->header.erd_count == 1) &&
    (packet->payload_length == sizeof(tiny_gea2_erd_api_read_response_payload_header_t) + payload->header.data_size);
}

static bool valid_write_response(const tiny_gea_packet_t* packet)
{
  if(packet->payload_length != sizeof(tiny_gea2_erd_api_write_response_payload_t)) {
    return false;
  }

  reinterpret(payload, packet->payload, const tiny_gea2_erd_api_write_response_payload_t*);

  return payload->erd_count == 1;
}

static void packet_received(void* _self, const void* _args)
{
  self_t* self = _self;
  const tiny_gea_interface_on_receive_args_t* args = _args;

  switch(args->packet->payload[0]) {
    case tiny_gea2_erd_api_command_read_response:
      if(valid_read_response(args->packet)) {
        handle_read_response_packet(self, args->packet);
      }
      break;

    case tiny_gea2_erd_api_command_write_response:
      if(valid_write_response(args->packet)) {
        handle_write_response_packet(self, args->packet);
      }
      break;
  }
}

typedef struct
{
  self_t* self;
  const void* request;
  requests_conflict_predicate_t requests_conflict_predicate;
  uint16_t i;
  uint16_t requestSize;
  bool request_already_queued;
  bool request_conflict_found;
} enqueue_request_if_unique_context_t;

static void enqueue_request_if_unique_worker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, enqueue_request_if_unique_context_t*);

  uint16_t size;
  tiny_queue_peek(&context->self->request_queue, allocated_block, &size, context->i);

  context->request_already_queued =
    (context->requestSize == size) &&
    (memcmp(context->request, allocated_block, size) == 0);

  if(context->requests_conflict_predicate) {
    context->request_conflict_found = context->requests_conflict_predicate(context->request, allocated_block);
  }
}

static bool enqueue_request_if_unique(
  self_t* self,
  const void* request,
  uint16_t requestSize,
  uint16_t* index,
  requests_conflict_predicate_t requests_conflict_predicate)
{
  uint16_t count = tiny_queue_count(&self->request_queue);

  for(uint16_t counter = count; counter > 0; counter--) {
    uint16_t i = counter - 1;

    uint16_t element_size;
    tiny_queue_peek_size(&self->request_queue, &element_size, i);

    enqueue_request_if_unique_context_t context;
    context.self = self;
    context.request = request;
    context.requestSize = requestSize;
    context.requests_conflict_predicate = requests_conflict_predicate;
    context.request_conflict_found = false;
    context.i = i;

    tiny_stack_allocator_allocate_aligned(element_size, &context, enqueue_request_if_unique_worker);

    if(context.request_already_queued) {
      *index = i;
      return true;
    }

    if(context.request_conflict_found) {
      break;
    }
  }

  *index = count;
  return tiny_queue_enqueue(&self->request_queue, request, requestSize);
}

static bool read_request_conflicts(const request_t* new_request, const request_t* queued_request)
{
  (void)new_request;

  switch(queued_request->type) {
    case request_type_write:
      return true;

    default:
      return false;
  }
}

static bool read(i_tiny_gea2_erd_client_t* _self, tiny_gea2_erd_client_request_id_t* request_id, uint8_t address, tiny_erd_t erd)
{
  reinterpret(self, _self, self_t*);

  uint16_t index;
  read_request_t request;
  request.type = request_type_read;
  request.address = address;
  request.erd = erd;
  bool request_added_or_already_queued = enqueue_request_if_unique(
    self,
    &request,
    sizeof(request),
    &index,
    read_request_conflicts);

  *request_id = index + self->request_id;

  send_request_if_not_busy(self);

  return request_added_or_already_queued;
}

typedef struct
{
  self_t* self;
  const void* data;
  tiny_erd_t erd;
  uint16_t index;
  tiny_gea2_erd_client_request_id_t request_id;
  uint8_t address;
  uint8_t data_size;
  bool request_added_or_already_queued;
} write_context_t;

static bool write_request_conflicts(const request_t* new_request, const request_t* queued_request)
{
  (void)new_request;

  switch(queued_request->type) {
    case request_type_write:
    case request_type_read:
      return true;

    default:
      return false;
  }
}

static void write_worker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, write_context_t*);
  reinterpret(request, allocated_block, write_request_t*);

  request->type = request_type_write;
  request->address = context->address;
  request->erd = context->erd;
  request->data_size = context->data_size;
  memcpy(request->data, context->data, context->data_size);
  context->request_added_or_already_queued = enqueue_request_if_unique(
    context->self,
    request,
    offsetof(write_request_t, data) + context->data_size,
    &context->index,
    write_request_conflicts);

  context->request_id = context->index + context->self->request_id;

  send_request_if_not_busy(context->self);
}

static bool write(i_tiny_gea2_erd_client_t* _self, tiny_gea2_erd_client_request_id_t* request_id, uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
{
  reinterpret(self, _self, self_t*);

  write_context_t context;
  context.self = self;
  context.address = address;
  context.erd = erd;
  context.data = data;
  context.data_size = data_size;

  tiny_stack_allocator_allocate_aligned(offsetof(write_request_t, data) + data_size, &context, write_worker);

  *request_id = context.request_id;

  return context.request_added_or_already_queued;
}

static i_tiny_event_t* on_activity(i_tiny_gea2_erd_client_t* _self)
{
  reinterpret(self, _self, self_t*);
  return &self->on_activity.interface;
}

static const i_tiny_gea2_erd_client_api_t api = { read, write, on_activity };

void tiny_gea2_erd_client_init(
  self_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea_interface_t* gea2_interface,
  uint8_t* queue_buffer,
  size_t queue_buffer_size,
  const tiny_gea2_erd_client_configuration_t* configuration)
{
  self->interface.api = &api;

  self->busy = false;
  self->gea2_interface = gea2_interface;
  self->configuration = configuration;
  self->timer_group = timer_group;
  self->request_id = 0;

  tiny_queue_init(&self->request_queue, queue_buffer, queue_buffer_size);

  tiny_event_init(&self->on_activity);

  tiny_event_subscription_init(&self->packet_received, self, packet_received);
  tiny_event_subscribe(tiny_gea_interface_on_receive(gea2_interface), &self->packet_received);
}
