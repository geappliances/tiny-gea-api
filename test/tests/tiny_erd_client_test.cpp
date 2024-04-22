/*!
 * @file
 * @brief
 */

extern "C" {
#include <string.h>
#include "tiny_erd_client.h"
#include "tiny_gea3_erd_api.h"
#include "tiny_utils.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_interface_double.hpp"
#include "double/tiny_timer_group_double.hpp"

enum {
  endpoint_address = 0xA5,
  request_retries = 3,
  request_timeout = 500
};

#define request_id(_x) _x
#define address(_x) _x
#define erd(_x) _x
#define context(_x) _x
#define retain(_x) _x
#define successful(_x) _x

#define and_then
#define and_

static const tiny_erd_client_configuration_t configuration = {
  .request_timeout = request_timeout,
  .request_retries = request_retries
};

static tiny_erd_client_request_id_t lastRequestId;
static size_t expected_data_size;

TEST_GROUP(tiny_erd_client)
{
  tiny_erd_client_t self;

  tiny_event_subscription_t activity_subscription;
  tiny_event_subscription_t request_again_on_request_complete_or_failed_subscription;
  tiny_timer_group_double_t timer_group;
  tiny_gea3_interface_double_t gea3_interface;
  uint8_t queue_buffer[25];

  static void on_activity(void*, const void* _args)
  {
    reinterpret(args, _args, const tiny_erd_client_on_activity_args_t*);

    switch(args->type) {
      case tiny_erd_client_activity_type_read_completed:
        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("read_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->read_completed.request_id)
            .withParameter("erd", args->read_completed.erd)
            .withParameter("u8Data", *(const uint8_t*)args->read_completed.data)
            .withParameter("data_size", args->read_completed.data_size);
        }
        else if(expected_data_size == sizeof(uint16_t)) {
          mock()
            .actualCall("read_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->read_completed.request_id)
            .withParameter("erd", args->read_completed.erd)
            .withParameter("u16Data", (*(const uint8_t*)args->read_completed.data << 8) + *((const uint8_t*)args->read_completed.data + 1))
            .withParameter("data_size", args->read_completed.data_size);
        }
        break;

      case tiny_erd_client_activity_type_read_failed:
        mock()
          .actualCall("read_failed")
          .withParameter("request_id", args->read_completed.request_id)
          .withParameter("address", args->address)
          .withParameter("erd", args->read_failed.erd)
          .withParameter("reason", args->read_failed.reason);
        break;

      case tiny_erd_client_activity_type_write_completed:
        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("write_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_completed.request_id)
            .withParameter("erd", args->write_completed.erd)
            .withParameter("u8Data", *(const uint8_t*)args->write_completed.data)
            .withParameter("data_size", args->write_completed.data_size);
        }
        else if(expected_data_size == sizeof(uint16_t)) {
          mock()
            .actualCall("write_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_completed.request_id)
            .withParameter("erd", args->write_completed.erd)
            .withParameter("u16Data", (*(const uint8_t*)args->write_completed.data << 8) + *((const uint8_t*)args->write_completed.data + 1))
            .withParameter("data_size", args->write_completed.data_size);
        }
        break;

      case tiny_erd_client_activity_type_write_failed:
        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("write_failed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_completed.request_id)
            .withParameter("erd", args->write_failed.erd)
            .withParameter("u8Data", *(const uint8_t*)args->write_completed.data)
            .withParameter("data_size", args->write_completed.data_size)
            .withParameter("reason", args->write_failed.reason);
        }
        else if(expected_data_size == sizeof(uint16_t)) {
          mock()
            .actualCall("write_failed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_completed.request_id)
            .withParameter("erd", args->write_failed.erd)
            .withParameter("u16Data", (*(const uint8_t*)args->write_completed.data << 8) + *((const uint8_t*)args->write_completed.data + 1))
            .withParameter("data_size", args->write_completed.data_size)
            .withParameter("reason", args->write_failed.reason);
        }
        break;

      case tiny_erd_client_activity_type_subscription_added_or_retained:
        mock()
          .actualCall("subscription_added_or_retained")
          .withParameter("address", args->address);
        break;

      case tiny_erd_client_activity_type_subscribe_failed:
        mock()
          .actualCall("subscription_failed")
          .withParameter("address", args->address);
        break;

      case tiny_erd_client_activity_type_subscription_publication_received:
        if(args->subscription_publication_received.erd == 0x8888) {
          expected_data_size = sizeof(uint8_t);
        }
        else if(args->subscription_publication_received.erd == 0x1616) {
          expected_data_size = sizeof(uint16_t);
        }

        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("subscription_publication_received")
            .withParameter("address", args->address)
            .withParameter("erd", args->subscription_publication_received.erd)
            .withParameter("u8Data", *(const uint8_t*)args->subscription_publication_received.data)
            .withParameter("data_size", args->subscription_publication_received.data_size);
        }
        else if(expected_data_size == sizeof(uint16_t) || args->subscription_publication_received.erd == 0x1616) {
          mock()
            .actualCall("subscription_publication_received")
            .withParameter("address", args->address)
            .withParameter("erd", args->subscription_publication_received.erd)
            .withParameter("u16Data", (*(const uint8_t*)args->subscription_publication_received.data << 8) + *((const uint8_t*)args->subscription_publication_received.data + 1))
            .withParameter("data_size", args->subscription_publication_received.data_size);
        }
        break;

      case tiny_erd_client_activity_type_subscription_host_came_online:
        mock()
          .actualCall("SubscriptionHostCameOnline")
          .withParameter("address", args->address);
        break;
    }
  }

  static void request_again_on_request_complete_or_failed(void* context, const void* _args)
  {
    reinterpret(self, context, i_tiny_erd_client_t*);
    reinterpret(args, _args, const tiny_erd_client_on_activity_args_t*);

    switch(args->type) {
      case tiny_erd_client_activity_type_read_completed:
        tiny_erd_client_read(self, &lastRequestId, args->address, args->read_completed.erd);
        break;

      case tiny_erd_client_activity_type_read_failed:
        tiny_erd_client_read(self, &lastRequestId, args->address, args->read_failed.erd);
        break;

      case tiny_erd_client_activity_type_write_completed:
        tiny_erd_client_write(self, &lastRequestId, args->address, args->write_completed.erd, args->write_completed.data, args->write_completed.data_size);
        break;

      case tiny_erd_client_activity_type_write_failed:
        tiny_erd_client_write(self, &lastRequestId, args->address, args->write_failed.erd, args->write_failed.data, args->write_failed.data_size);
        break;

      case tiny_erd_client_activity_type_subscription_added_or_retained:
        tiny_erd_client_subscribe(self, args->address);
        break;

      case tiny_erd_client_activity_type_subscribe_failed:
        tiny_erd_client_subscribe(self, args->address);
        break;
    }
  }

  void setup()
  {
    tiny_gea3_interface_double_init(&gea3_interface, endpoint_address);
    tiny_timer_group_double_init(&timer_group);

    memset(&self, 0xA5, sizeof(self));

    tiny_erd_client_init(
      &self,
      &timer_group.timer_group,
      &gea3_interface.interface,
      queue_buffer,
      sizeof(queue_buffer),
      &configuration);

    tiny_event_subscription_init(&activity_subscription, nullptr, on_activity);
    tiny_event_subscribe(tiny_erd_client_on_activity(&self.interface), &activity_subscription);

    tiny_event_subscription_init(&request_again_on_request_complete_or_failed_subscription, &self, request_again_on_request_complete_or_failed);
  }

  void given_that_the_client_will_request_again_on_complete_or_failed()
  {
    tiny_event_subscribe(tiny_erd_client_on_activity(&self.interface), &request_again_on_request_complete_or_failed_subscription);
  }

  void should_be_sent(const tiny_gea3_packet_t* request)
  {
    mock()
      .expectOneCall("send")
      .onObject(&gea3_interface)
      .withParameter("source", request->source)
      .withParameter("destination", request->destination)
      .withMemoryBufferParameter("payload", request->payload, request->payload_length);
  }

#define a_read_request_should_be_sent(request_id, address, erd)   \
  do {                                                            \
    tiny_gea3_STATIC_ALLOC_PACKET(request, 4);                    \
    request->source = endpoint_address;                           \
    request->destination = address;                               \
    request->payload[0] = tiny_gea3_erd_api_command_read_request; \
    request->payload[1] = request_id;                             \
    request->payload[2] = erd >> 8;                               \
    request->payload[3] = erd & 0xFF;                             \
    should_be_sent(request);                                      \
  } while(0)

#define a_write_request_should_be_sent(request_id, address, erd, data) \
  do {                                                                 \
    if(sizeof(data) == 1) {                                            \
      tiny_gea3_STATIC_ALLOC_PACKET(request, 6);                       \
      request->source = endpoint_address;                              \
      request->destination = address;                                  \
      request->payload[0] = tiny_gea3_erd_api_command_write_request;   \
      request->payload[1] = request_id;                                \
      request->payload[2] = erd >> 8;                                  \
      request->payload[3] = erd & 0xFF;                                \
      request->payload[4] = sizeof(data);                              \
      request->payload[5] = data & 0xFF;                               \
      should_be_sent(request);                                         \
    }                                                                  \
    else {                                                             \
      tiny_gea3_STATIC_ALLOC_PACKET(request, 7);                       \
      request->source = endpoint_address;                              \
      request->destination = address;                                  \
      request->payload[0] = tiny_gea3_erd_api_command_write_request;   \
      request->payload[1] = request_id;                                \
      request->payload[2] = erd >> 8;                                  \
      request->payload[3] = erd & 0xFF;                                \
      request->payload[4] = sizeof(data);                              \
      request->payload[5] = data >> 8;                                 \
      request->payload[6] = data & 0xFF;                               \
      should_be_sent(request);                                         \
    }                                                                  \
  } while(0)

#define a_subscribe_all_request_should_be_sent(request_id, address, retain)                                                                                          \
  do {                                                                                                                                                               \
    tiny_gea3_STATIC_ALLOC_PACKET(request, 3);                                                                                                                       \
    request->source = endpoint_address;                                                                                                                              \
    request->destination = address;                                                                                                                                  \
    request->payload[0] = tiny_gea3_erd_api_command_subscribe_all_request;                                                                                           \
    request->payload[1] = request_id;                                                                                                                                \
    request->payload[2] = retain ? tiny_gea3_erd_api_subscribe_all_request_type_retain_subscription : tiny_gea3_erd_api_subscribe_all_request_type_add_subscription; \
    should_be_sent(request);                                                                                                                                         \
  } while(0)

#define a_subscription_publication_acknowledgment_should_be_sent(request_id, address, context) \
  do {                                                                                         \
    tiny_gea3_STATIC_ALLOC_PACKET(request, 3);                                                 \
    request->source = endpoint_address;                                                        \
    request->destination = address;                                                            \
    request->payload[0] = tiny_gea3_erd_api_command_publication_acknowledgment;                \
    request->payload[1] = context;                                                             \
    request->payload[2] = request_id;                                                          \
    should_be_sent(request);                                                                   \
  } while(0)

  void after_a_read_response_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 7);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_read_response;
    packet->payload[1] = request_id;
    packet->payload[2] = tiny_gea3_erd_api_read_result_success;
    packet->payload[3] = erd >> 8;
    packet->payload[4] = erd & 0xFF;
    packet->payload[5] = sizeof(data);
    packet->payload[6] = data;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_read_response_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 8);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_read_response;
    packet->payload[1] = request_id;
    packet->payload[2] = tiny_gea3_erd_api_read_result_success;
    packet->payload[3] = erd >> 8;
    packet->payload[4] = erd & 0xFF;
    packet->payload[5] = sizeof(data);
    packet->payload[6] = data >> 8;
    packet->payload[7] = data & 0xFF;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_read_failure_response_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, tiny_erd_t erd, tiny_gea3_erd_api_read_result_t result)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 5);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_read_response;
    packet->payload[1] = request_id;
    packet->payload[2] = result;
    packet->payload[3] = erd >> 8;
    packet->payload[4] = erd & 0xFF;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_write_response_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, tiny_erd_t erd, tiny_gea3_erd_api_write_result_t result)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 5);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_write_response;
    packet->payload[1] = request_id;
    packet->payload[2] = result;
    packet->payload[3] = erd >> 8;
    packet->payload[4] = erd & 0xFF;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_malformed_write_response_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, tiny_erd_t erd, tiny_gea3_erd_api_write_result_t result)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 6);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_write_response;
    packet->payload[1] = request_id;
    packet->payload[2] = result;
    packet->payload[3] = erd >> 8;
    packet->payload[4] = erd & 0xFF;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_subscribe_all_response_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, bool successful)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 3);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_subscribe_all_response;
    packet->payload[1] = request_id;
    packet->payload[2] = successful ? tiny_gea3_erd_api_subscribe_all_result_success : tiny_gea3_erd_api_subscribe_all_result_no_available_subscriptions;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_subscription_publication_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, uint8_t context, tiny_erd_t erd, uint8_t data)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 8);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_publication;
    packet->payload[1] = context;
    packet->payload[2] = request_id;
    packet->payload[3] = 1;
    packet->payload[4] = erd >> 8;
    packet->payload[5] = erd & 0xFF;
    packet->payload[6] = sizeof(data);
    packet->payload[7] = data;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_subscription_publication_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, uint8_t context, tiny_erd_t erd, uint16_t data)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 9);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_publication;
    packet->payload[1] = context;
    packet->payload[2] = request_id;
    packet->payload[3] = 1;
    packet->payload[4] = erd >> 8;
    packet->payload[5] = erd & 0xFF;
    packet->payload[6] = sizeof(data);
    packet->payload[7] = data >> 8;
    packet->payload[8] = data & 0xFF;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_subscription_publication_is_received(tiny_gea3_erd_api_request_id_t request_id, uint8_t address, uint8_t context, tiny_erd_t erd1, uint8_t data1, tiny_erd_t erd2, uint16_t data2)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 13);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_publication;
    packet->payload[1] = context;
    packet->payload[2] = request_id;
    packet->payload[3] = 2;
    packet->payload[4] = erd1 >> 8;
    packet->payload[5] = erd1 & 0xFF;
    packet->payload[6] = sizeof(data1);
    packet->payload[7] = data1;
    packet->payload[8] = erd2 >> 8;
    packet->payload[9] = erd2 & 0xFF;
    packet->payload[10] = sizeof(data2);
    packet->payload[11] = data2 >> 8;
    packet->payload[12] = data2 & 0xFF;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after_a_subscription_host_startup_is_received(uint8_t address)
  {
    tiny_gea3_STACK_ALLOC_PACKET(packet, 1);
    packet->source = address;
    packet->destination = endpoint_address;
    packet->payload[0] = tiny_gea3_erd_api_command_subscription_host_startup;

    tiny_gea3_interface_double_trigger_receive(&gea3_interface, packet);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void after_a_read_is_requested(uint8_t address, tiny_erd_t erd)
  {
    bool success = tiny_erd_client_read(&self.interface, &lastRequestId, address, erd);
    CHECK(success);
  }

  void should_fail_to_queue_a_read_request(uint8_t address, tiny_erd_t erd)
  {
    CHECK_FALSE(tiny_erd_client_read(&self.interface, &lastRequestId, address, erd));
  }

  void after_a_write_is_requested(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    bool success = tiny_erd_client_write(&self.interface, &lastRequestId, address, erd, &data, sizeof(data));
    CHECK(success);
  }

  void should_fail_to_queue_a_write_request(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    CHECK_FALSE(tiny_erd_client_write(&self.interface, &lastRequestId, address, erd, &data, sizeof(data)));
  }

  void after_a_write_is_requested(uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    uint8_t big_endian_data[] = { (uint8_t)(data >> 8), (uint8_t)data };
    bool success = tiny_erd_client_write(&self.interface, &lastRequestId, address, erd, big_endian_data, sizeof(big_endian_data));
    CHECK(success);
  }

  void after_subscribe_is_requested(uint8_t address)
  {
    bool success = tiny_erd_client_subscribe(&self.interface, address);
    CHECK(success);
  }

  void should_fail_to_queue_a_subscribe_request(uint8_t address)
  {
    CHECK_FALSE(tiny_erd_client_subscribe(&self.interface, address));
  }

  void after_retain_subscription_is_requested(uint8_t address)
  {
    bool success = tiny_erd_client_retain_subscription(&self.interface, address);
    CHECK(success);
  }

  void should_fail_to_queue_a_retain_subscription_request(uint8_t address)
  {
    CHECK_FALSE(tiny_erd_client_retain_subscription(&self.interface, address));
  }

  void should_publish_read_completed(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("read_completed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_read_completed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_erd_client_request_id_t request_id)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("read_completed")
      .withParameter("request_id", request_id)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_read_completed(uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("read_completed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u16Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_read_failed(uint8_t address, tiny_erd_t erd, tiny_erd_client_read_failure_reason_t reason)
  {
    mock()
      .expectOneCall("read_failed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_read_failed(uint8_t address, tiny_erd_t erd, tiny_erd_client_request_id_t request_id, tiny_erd_client_read_failure_reason_t reason)
  {
    mock()
      .expectOneCall("read_failed")
      .withParameter("request_id", request_id)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_write_completed(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_completed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_write_completed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_erd_client_request_id_t request_id)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_completed")
      .withParameter("request_id", request_id)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_write_completed(uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_completed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u16Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_write_failed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_erd_client_write_failure_reason_t reason)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_failed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_write_failed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_erd_client_request_id_t request_id, tiny_erd_client_write_failure_reason_t reason)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_failed")
      .withParameter("request_id", request_id)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_write_failed(uint8_t address, tiny_erd_t erd, uint16_t data, tiny_erd_client_write_failure_reason_t reason)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_failed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u16Data", data)
      .withParameter("data_size", sizeof(data))
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_subscription_failed(uint8_t address)
  {
    mock()
      .expectOneCall("subscription_failed")
      .withParameter("address", address);
  }

  void should_publish_subscription_added_or_retained(uint8_t address)
  {
    mock()
      .expectOneCall("subscription_added_or_retained")
      .withParameter("address", address);
  }

  void should_publish_subscription_publication_received(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("subscription_publication_received")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_subscription_publication_received(uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("subscription_publication_received")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u16Data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_subscription_host_came_online(uint8_t address)
  {
    mock()
      .expectOneCall("SubscriptionHostCameOnline")
      .withParameter("address", address);
  }

  void with_an_expected_request_id(tiny_erd_client_request_id_t expected)
  {
    CHECK_EQUAL(expected, lastRequestId);
  }

  void nothing_should_happen()
  {
  }
};

TEST(tiny_erd_client, should_read)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);

  a_read_request_should_be_sent(request_id(1), address(0x23), erd(0x5678));
  after_a_read_is_requested(address(0x23), erd(0x5678));
  and_then should_publish_read_completed(address(0x23), erd(0x5678), (uint16_t)1234);
  after_a_read_response_is_received(request_id(1), address(0x23), erd(0x5678), (uint16_t)1234);
}

TEST(tiny_erd_client, should_allow_a_read_to_be_completed_with_any_address_if_the_destination_is_the_broadcast_address)
{
  a_read_request_should_be_sent(request_id(0), address(0xFF), erd(0x1234));
  after_a_read_is_requested(address(0xFF), erd(0x1234));
  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_erd_client, should_not_complete_a_read_with_the_wrong_type_address_request_id_erd_or_result_is_busy)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  nothing_should_happen();
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));
  after_a_read_response_is_received(request_id(1), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(request_id(0), address(0x55), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1235), (uint8_t)123);
  after_a_read_failure_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_read_result_busy);
}

TEST(tiny_erd_client, should_complete_read_with_failure_if_the_result_is_unsupported_erd)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_erd_client_read_failure_reason_not_supported);
  after_a_read_failure_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_read_result_unsupported_erd);
}

TEST(tiny_erd_client, should_write)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  and_then should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);

  a_write_request_should_be_sent(request_id(1), address(0x23), erd(0x5678), (uint16_t)1234);
  after_a_write_is_requested(address(0x23), erd(0x5678), (uint16_t)1234);
  and_then should_publish_write_completed(address(0x23), erd(0x5678), (uint16_t)1234);
  after_a_write_response_is_received(request_id(1), address(0x23), erd(0x5678), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_allow_a_write_to_be_completed_with_any_address_if_the_destination_is_the_broadcast_address)
{
  a_write_request_should_be_sent(request_id(0), address(0xFF), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0xFF), erd(0x1234), (uint8_t)123);
  and_then should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_not_complete_a_write_with_the_wrong_type_address_request_id_erd_or_result_is_busy)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  nothing_should_happen();
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(1), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);
  after_a_write_response_is_received(request_id(0), address(0x55), erd(0x1234), tiny_gea3_erd_api_write_result_success);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1235), tiny_gea3_erd_api_write_result_success);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_busy);
}

TEST(tiny_erd_client, should_complete_write_with_failure_if_the_result_is_incorrect_size)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_failed(address(0x54), erd(0x1234), (uint8_t)123, tiny_erd_client_write_failure_reason_incorrect_size);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_incorrect_size);
}

TEST(tiny_erd_client, should_complete_write_with_failure_if_the_result_is_unsupported_erd)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_failed(address(0x54), erd(0x1234), (uint8_t)123, tiny_erd_client_write_failure_reason_not_supported);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_unsupported_erd);
}

TEST(tiny_erd_client, should_subscribe)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));
  and_then should_publish_subscription_added_or_retained(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));
}

TEST(tiny_erd_client, should_fail_a_subscription_all_when_a_negative_response_is_received)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));

  should_publish_subscription_failed(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(false));
}

TEST(tiny_erd_client, should_retain_subscription)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(true));
  after_retain_subscription_is_requested(address(0x54));
  and_then should_publish_subscription_added_or_retained(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));
}

TEST(tiny_erd_client, should_not_complete_a_retain_subscription_with_the_wrong_type_address_or_request_id)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(true));
  after_retain_subscription_is_requested(address(0x54));

  nothing_should_happen();
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);
  after_a_subscribe_all_response_is_received(request_id(1), address(0x54), successful(true));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x55), successful(true));
}

TEST(tiny_erd_client, should_fail_a_retain_subscription_when_a_negative_response_is_received)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(true));
  after_retain_subscription_is_requested(address(0x54));

  should_publish_subscription_failed(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(false));
}

TEST(tiny_erd_client, should_acknowledge_publications)
{
  should_publish_subscription_publication_received(address(0x42), erd(0x1234), (uint8_t)5);
  a_subscription_publication_acknowledgment_should_be_sent(request_id(123), address(0x42), context(0xA5));
  after_a_subscription_publication_is_received(request_id(123), address(0x42), context(0xA5), erd(0x1234), (uint8_t)5);

  should_publish_subscription_publication_received(address(0x42), erd(0x1234), (uint16_t)4242);
  a_subscription_publication_acknowledgment_should_be_sent(request_id(123), address(0x42), context(0xA5));
  after_a_subscription_publication_is_received(request_id(123), address(0x42), context(0xA5), erd(0x1234), (uint16_t)4242);
}

TEST(tiny_erd_client, should_acknowledge_publications_with_multiple_erds)
{
  should_publish_subscription_publication_received(address(0x42), erd(0x8888), (uint8_t)5);
  should_publish_subscription_publication_received(address(0x42), erd(0x1616), (uint16_t)4242);
  a_subscription_publication_acknowledgment_should_be_sent(request_id(123), address(0x42), context(0xA5));
  after_a_subscription_publication_is_received(request_id(123), address(0x42), context(0xA5), erd(0x8888), (uint8_t)5, erd(0x1616), (uint16_t)4242);
}

TEST(tiny_erd_client, should_indicate_when_a_subscription_host_has_come_online)
{
  should_publish_subscription_host_came_online(address(0x42));
  after_a_subscription_host_startup_is_received(address(0x42));
}

TEST(tiny_erd_client, should_queue_requests)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  after_subscribe_is_requested(address(0x54));
  after_subscribe_is_requested(address(0x55));

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  a_write_request_should_be_sent(request_id(1), address(0x56), erd(0x5678), (uint8_t)21);
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_write_completed(address(0x56), erd(0x5678), (uint8_t)21);
  a_subscribe_all_request_should_be_sent(request_id(2), address(0x54), retain(false));
  after_a_write_response_is_received(request_id(1), address(0x56), erd(0x5678), tiny_gea3_erd_api_write_result_success);

  and_then should_publish_subscription_added_or_retained(address(0x54));
  a_subscribe_all_request_should_be_sent(request_id(3), address(0x55), retain(false));
  after_a_subscribe_all_response_is_received(request_id(2), address(0x54), successful(true));

  and_then should_publish_subscription_added_or_retained(address(0x55));
  after_a_subscribe_all_response_is_received(request_id(3), address(0x55), successful(true));
}

TEST(tiny_erd_client, should_indicate_when_requests_cannot_be_queued)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  after_subscribe_is_requested(address(0x54));
  after_subscribe_is_requested(address(0x55));

  should_fail_to_queue_a_subscribe_request(address(0x75));
  should_fail_to_queue_a_retain_subscription_request(address(0x75));
  should_fail_to_queue_a_read_request(address(0x75), erd(0x1234));
  should_fail_to_queue_a_write_request(address(0x75), erd(0x5678), (uint8_t)21);
}

TEST(tiny_erd_client, should_retry_failed_read_requests)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  for(uint8_t i = 0; i < request_retries; i++) {
    nothing_should_happen();
    after(request_timeout - 1);

    a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
    after(1);
  }

  nothing_should_happen();
  after(request_timeout - 1);

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_erd_client_read_failure_reason_retries_exhausted);
  after(1);

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_erd_client, should_retry_failed_write_requests)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  for(uint8_t i = 0; i < request_retries; i++) {
    nothing_should_happen();
    after(request_timeout - 1);

    a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
    after(1);
  }

  nothing_should_happen();
  after(request_timeout - 1);

  should_publish_write_failed(address(0x54), erd(0x1234), (uint8_t)123, tiny_erd_client_write_failure_reason_retries_exhausted);
  after(1);

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_erd_client, should_retry_failed_subscribe_requests)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));

  for(uint8_t i = 0; i < request_retries; i++) {
    nothing_should_happen();
    after(request_timeout - 1);

    a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
    after(1);
  }

  nothing_should_happen();
  after(request_timeout - 1);

  should_publish_subscription_failed(address(0x54));
  after(1);

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_erd_client, should_not_retry_successful_requests)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));
  and_then should_publish_subscription_added_or_retained(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_erd_client, should_continue_to_the_next_request_after_a_failed_request)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x64), erd(0x0001));

  for(uint8_t i = 0; i < request_retries; i++) {
    a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
    after(request_timeout);
  }

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_erd_client_read_failure_reason_retries_exhausted);
  a_read_request_should_be_sent(request_id(1), address(0x64), erd(0x0001));
  after(request_timeout);
}

TEST(tiny_erd_client, should_reject_malformed_requests)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  nothing_should_happen();
  after_a_malformed_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_ignore_duplicate_read_requests_that_are_back_to_back)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_erd_client, should_ignore_duplicate_read_requests_that_are_separated_by_another_read)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x5678));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_read_request_should_be_sent(request_id(1), address(0x54), erd(0x5678));
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_read_completed(address(0x54), erd(0x5678), (uint8_t)73);
  after_a_read_response_is_received(request_id(1), address(0x54), erd(0x5678), (uint8_t)73);
}

TEST(tiny_erd_client, should_ignore_duplicate_read_requests_that_are_separated_by_a_subscribe_request)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_subscribe_is_requested(address(0x27));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_subscribe_all_request_should_be_sent(request_id(1), address(0x27), retain(false));
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_erd_client, should_not_ignore_duplicate_read_requests_that_are_separated_by_a_write)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_write_is_requested(address(0x54), erd(0x5678), (uint8_t)7);
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_write_request_should_be_sent(request_id(1), address(0x54), erd(0x5678), (uint8_t)7);
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_write_completed(address(0x54), erd(0x5678), (uint8_t)7);
  and_ a_read_request_should_be_sent(request_id(2), address(0x54), erd(0x1234));
  after_a_write_response_is_received(request_id(1), address(0x54), erd(0x5678), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_ignore_duplicate_write_requests_that_are_back_to_back)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_ignore_duplicate_write_requests_that_are_separated_by_subscribe_requests)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_subscribe_is_requested(address(0x27));
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_subscribe_all_request_should_be_sent(request_id(1), address(0x27), retain(false));
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);

  and_then should_publish_subscription_added_or_retained(address(0x27));
  after_a_subscribe_all_response_is_received(request_id(1), address(0x27), successful(true));
}

TEST(tiny_erd_client, should_not_ignore_duplicate_write_requests_if_it_would_change_the_values_written)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x5678), (uint8_t)7);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_write_request_should_be_sent(request_id(1), address(0x54), erd(0x5678), (uint8_t)7);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);

  should_publish_write_completed(address(0x54), erd(0x5678), (uint8_t)7);
  and_then a_write_request_should_be_sent(request_id(2), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(1), address(0x54), erd(0x5678), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_not_ignore_duplicate_write_requests_if_theres_a_read_between_the_duplicate_writes)
{
  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_is_requested(address(0x54), erd(0x5678));
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_read_request_should_be_sent(request_id(1), address(0x54), erd(0x5678));
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);

  should_publish_read_completed(address(0x54), erd(0x5678), (uint8_t)7);
  and_then a_write_request_should_be_sent(request_id(2), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(request_id(1), address(0x54), erd(0x5678), (uint8_t)7);
}

TEST(tiny_erd_client, should_ignore_duplicate_subscribe_requests)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));
  after_subscribe_is_requested(address(0x54));

  and_then should_publish_subscription_added_or_retained(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));
}

TEST(tiny_erd_client, should_ignore_duplicate_retain_subscription_requests)
{
  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(true));
  after_retain_subscription_is_requested(address(0x54));
  after_retain_subscription_is_requested(address(0x54));

  and_then should_publish_subscription_added_or_retained(address(0x54));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));
}

TEST(tiny_erd_client, should_ignore_responses_when_there_are_no_active_requests)
{
  nothing_should_happen();
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_erd_client, should_allow_a_new_read_request_in_read_request_complete_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  a_read_request_should_be_sent(request_id(1), address(0x54), erd(0x1234));
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_erd_client, should_allow_a_new_read_request_in_read_request_failed_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_erd_client_read_failure_reason_not_supported);
  a_read_request_should_be_sent(request_id(1), address(0x54), erd(0x1234));
  after_a_read_failure_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_read_result_unsupported_erd);
}

TEST(tiny_erd_client, should_allow_a_new_write_request_in_write_request_complete_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  a_write_request_should_be_sent(request_id(1), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_allow_a_new_write_request_in_write_request_failed_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_write_request_should_be_sent(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_failed(address(0x54), erd(0x1234), (uint8_t)123, tiny_gea3_erd_api_write_result_incorrect_size);
  a_write_request_should_be_sent(request_id(1), address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(request_id(0), address(0x54), erd(0x1234), tiny_gea3_erd_api_write_result_incorrect_size);
}

TEST(tiny_erd_client, should_allow_a_new_subscribe_request_in_subscribe_complete_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));

  should_publish_subscription_added_or_retained(address(0x54));
  a_subscribe_all_request_should_be_sent(request_id(1), address(0x54), retain(false));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(true));
}

TEST(tiny_erd_client, should_allow_a_new_subscribe_request_in_subscribe_failed_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_subscribe_all_request_should_be_sent(request_id(0), address(0x54), retain(false));
  after_subscribe_is_requested(address(0x54));

  should_publish_subscription_failed(address(0x54));
  a_subscribe_all_request_should_be_sent(request_id(1), address(0x54), retain(false));
  after_a_subscribe_all_response_is_received(request_id(0), address(0x54), successful(false));
}

TEST(tiny_erd_client, should_provide_request_ids_for_read_requests)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  with_an_expected_request_id(0);

  after_a_read_is_requested(address(0x56), erd(0x5678));
  with_an_expected_request_id(1);

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123, request_id(0));
  a_read_request_should_be_sent(request_id(1), address(0x56), erd(0x5678));
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);

  after_a_read_is_requested(address(0x56), erd(0xABCD));
  with_an_expected_request_id(2);

  and_then should_publish_read_completed(address(0x56), erd(0x5678), (uint8_t)21, request_id(1));
  a_read_request_should_be_sent(request_id(2), address(0x56), erd(0xABCD));
  after_a_read_response_is_received(request_id(1), address(0x56), erd(0x5678), (uint8_t)21);

  and_then should_publish_read_failed(address(0x56), erd(0xABCD), request_id(2), tiny_erd_client_read_failure_reason_not_supported);
  after_a_read_failure_response_is_received(request_id(2), address(0x56), erd(0xABCD), tiny_gea3_erd_api_read_result_unsupported_erd);
}

TEST(tiny_erd_client, should_provide_the_same_request_id_for_duplicate_read_requests)
{
  a_read_request_should_be_sent(request_id(0), address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  with_an_expected_request_id(0);

  after_a_read_is_requested(address(0x56), erd(0x5678));
  with_an_expected_request_id(1);

  after_a_read_is_requested(address(0x54), erd(0x1234));
  with_an_expected_request_id(0);

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123, request_id(0));
  a_read_request_should_be_sent(request_id(1), address(0x56), erd(0x5678));
  after_a_read_response_is_received(request_id(0), address(0x54), erd(0x1234), (uint8_t)123);

  after_a_read_is_requested(address(0x56), erd(0xABCD));
  with_an_expected_request_id(2);

  after_a_read_is_requested(address(0x56), erd(0x5678));
  with_an_expected_request_id(1);
}

TEST(tiny_erd_client, should_provide_request_ids_for_write_requests)
{
  a_write_request_should_be_sent(request_id(0), address(0x56), erd(0xABCD), (uint8_t)42);
  after_a_write_is_requested(address(0x56), erd(0xABCD), (uint8_t)42);
  with_an_expected_request_id(0);

  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  with_an_expected_request_id(1);

  and_then should_publish_write_completed(address(0x56), erd(0xABCD), (uint8_t)42, request_id(0));
  a_write_request_should_be_sent(request_id(1), address(0x56), erd(0x5678), (uint8_t)21);
  after_a_write_response_is_received(request_id(0), address(0x56), erd(0xABCD), tiny_gea3_erd_api_write_result_success);

  after_a_write_is_requested(address(0x56), erd(0x1234), (uint8_t)7);
  with_an_expected_request_id(2);

  and_then should_publish_write_failed(address(0x56), erd(0x5678), (uint8_t)21, request_id(1), tiny_erd_client_write_failure_reason_incorrect_size);
  a_write_request_should_be_sent(request_id(2), address(0x56), erd(0x1234), (uint8_t)7);
  after_a_write_response_is_received(request_id(1), address(0x56), erd(0x5678), tiny_gea3_erd_api_write_result_incorrect_size);

  and_then should_publish_write_completed(address(0x56), erd(0x1234), (uint8_t)7, request_id(2));
  after_a_write_response_is_received(request_id(2), address(0x56), erd(0x1234), tiny_gea3_erd_api_write_result_success);
}

TEST(tiny_erd_client, should_provide_the_same_request_id_for_duplicate_write_requests)
{
  a_write_request_should_be_sent(request_id(0), address(0x56), erd(0xABCD), (uint8_t)42);
  after_a_write_is_requested(address(0x56), erd(0xABCD), (uint8_t)42);
  with_an_expected_request_id(0);

  after_a_write_is_requested(address(0x56), erd(0xABCD), (uint8_t)42);
  with_an_expected_request_id(0);

  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  with_an_expected_request_id(1);

  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  with_an_expected_request_id(1);

  and_then should_publish_write_completed(address(0x56), erd(0xABCD), (uint8_t)42, request_id(0));
  a_write_request_should_be_sent(request_id(1), address(0x56), erd(0x5678), (uint8_t)21);
  after_a_write_response_is_received(request_id(0), address(0x56), erd(0xABCD), tiny_gea3_erd_api_write_result_success);

  after_a_write_is_requested(address(0x56), erd(0x1234), (uint8_t)7);
  with_an_expected_request_id(2);
}
