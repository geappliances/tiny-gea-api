/*!
 * @file
 * @brief
 */

extern "C" {
#include "tiny_gea2_erd_api.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_utils.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_timer_group_double.hpp"
#include "tiny_gea_interface_double.hpp"

enum {
  send_retries = 2,
  client_address = 0xA5,
  request_retries = 3,
  request_timeout = 500
};

#define request_id(_x) _x
#define address(_x) _x
#define erd(_x) _x
#define context(_x) _x
#define successful(_x) _x

#define and_then
#define and_

static const tiny_gea2_erd_client_configuration_t configuration = {
  .request_timeout = request_timeout,
  .request_retries = request_retries
};

static tiny_gea2_erd_client_request_id_t last_request_id;
static size_t expected_data_size;

TEST_GROUP(tiny_gea2_erd_client)
{
  tiny_gea2_erd_client_t self;

  tiny_event_subscription_t activitySubscription;
  tiny_event_subscription_t request_again_on_request_complete_or_failedSubscription;
  tiny_timer_group_double_t timer_group;
  tiny_gea_interface_double_t gea2_interface;
  uint8_t queue_buffer[25];

  static void on_activity(void*, const void* _args)
  {
    reinterpret(args, _args, const tiny_gea2_erd_client_on_activity_args_t*);

    switch(args->type) {
      case tiny_gea2_erd_client_activity_type_read_completed:
        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("read_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->read_completed.request_id)
            .withParameter("erd", args->read_completed.erd)
            .withParameter("u8_data", *(const uint8_t*)args->read_completed.data)
            .withParameter("data_size", args->read_completed.data_size);
        }
        else if(expected_data_size == sizeof(uint16_t)) {
          mock()
            .actualCall("read_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->read_completed.request_id)
            .withParameter("erd", args->read_completed.erd)
            .withParameter("u16_data", (*(const uint8_t*)args->read_completed.data << 8) + *((const uint8_t*)args->read_completed.data + 1))
            .withParameter("data_size", args->read_completed.data_size);
        }
        break;

      case tiny_gea2_erd_client_activity_type_read_failed:
        mock()
          .actualCall("read_failed")
          .withParameter("address", args->address)
          .withParameter("request_id", args->read_failed.request_id)
          .withParameter("erd", args->read_failed.erd)
          .withParameter("reason", args->read_failed.reason);
        break;

      case tiny_gea2_erd_client_activity_type_write_completed:
        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("write_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_completed.request_id)
            .withParameter("erd", args->write_completed.erd)
            .withParameter("u8_data", *(const uint8_t*)args->write_completed.data)
            .withParameter("data_size", args->write_completed.data_size);
        }
        else if(expected_data_size == sizeof(uint16_t)) {
          mock()
            .actualCall("write_completed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_completed.request_id)
            .withParameter("erd", args->write_completed.erd)
            .withParameter("u16_data", (*(const uint8_t*)args->write_completed.data << 8) + *((const uint8_t*)args->write_completed.data + 1))
            .withParameter("data_size", args->write_completed.data_size);
        }
        break;

      case tiny_gea2_erd_client_activity_type_write_failed:
        if(expected_data_size == sizeof(uint8_t)) {
          mock()
            .actualCall("write_failed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_failed.request_id)
            .withParameter("erd", args->write_failed.erd)
            .withParameter("u8_data", *(const uint8_t*)args->write_completed.data)
            .withParameter("data_size", args->write_completed.data_size)
            .withParameter("reason", args->write_failed.reason);
        }
        else if(expected_data_size == sizeof(uint16_t)) {
          mock()
            .actualCall("write_failed")
            .withParameter("address", args->address)
            .withParameter("request_id", args->write_failed.request_id)
            .withParameter("erd", args->write_failed.erd)
            .withParameter("u16_data", (*(const uint8_t*)args->write_completed.data << 8) + *((const uint8_t*)args->write_completed.data + 1))
            .withParameter("data_size", args->write_completed.data_size)
            .withParameter("reason", args->write_failed.reason);
        }
        break;
    }
  }

  static void request_again_on_request_complete_or_failed(void* context, const void* _args)
  {
    reinterpret(self, context, i_tiny_gea2_erd_client_t*);
    reinterpret(args, _args, const tiny_gea2_erd_client_on_activity_args_t*);

    switch(args->type) {
      case tiny_gea2_erd_client_activity_type_read_completed:
        tiny_gea2_erd_client_read(self, &last_request_id, args->address, args->read_completed.erd);
        break;

      case tiny_gea2_erd_client_activity_type_read_failed:
        tiny_gea2_erd_client_read(self, &last_request_id, args->address, args->read_failed.erd);
        break;

      case tiny_gea2_erd_client_activity_type_write_completed:
        tiny_gea2_erd_client_write(self, &last_request_id, args->address, args->write_completed.erd, args->write_completed.data, args->write_completed.data_size);
        break;

      case tiny_gea2_erd_client_activity_type_write_failed:
        tiny_gea2_erd_client_write(self, &last_request_id, args->address, args->write_failed.erd, args->write_failed.data, args->write_failed.data_size);
        break;
    }
  }

  void setup()
  {
    tiny_gea_interface_double_init(&gea2_interface, client_address);
    tiny_timer_group_double_init(&timer_group);

    memset(&self, 0xA5, sizeof(self));

    tiny_gea2_erd_client_init(
      &self,
      &timer_group.timer_group,
      &gea2_interface.interface,
      queue_buffer,
      sizeof(queue_buffer),
      &configuration);

    tiny_event_subscription_init(&activitySubscription, NULL, on_activity);
    tiny_event_subscribe(tiny_gea2_erd_client_on_activity(&self.interface), &activitySubscription);

    tiny_event_subscription_init(&request_again_on_request_complete_or_failedSubscription, &self, request_again_on_request_complete_or_failed);
  }

  void given_that_the_client_will_request_again_on_complete_or_failed()
  {
    tiny_event_subscribe(tiny_gea2_erd_client_on_activity(&self.interface), &request_again_on_request_complete_or_failedSubscription);
  }

  void should_be_sent(const tiny_gea_packet_t* request)
  {
    mock()
      .expectOneCall("send")
      .onObject(&gea2_interface)
      .withParameter("source", request->source)
      .withParameter("destination", request->destination)
      .withMemoryBufferParameter("payload", request->payload, request->payload_length);
  }

#define a_read_request_should_be_sent(address, erd)               \
  do {                                                            \
    tiny_gea_STATIC_ALLOC_PACKET(request, 4);                     \
    request->source = client_address;                             \
    request->destination = address;                               \
    request->payload[0] = tiny_gea2_erd_api_command_read_request; \
    request->payload[1] = 1;                                      \
    request->payload[2] = erd >> 8;                               \
    request->payload[3] = erd & 0xFF;                             \
    should_be_sent(request);                                      \
  } while(0)

#define a_write_request_should_be_sent(address, erd, data)           \
  do {                                                               \
    if(sizeof(data) == 1) {                                          \
      tiny_gea_STATIC_ALLOC_PACKET(request, 6);                      \
      request->source = client_address;                              \
      request->destination = address;                                \
      request->payload[0] = tiny_gea2_erd_api_command_write_request; \
      request->payload[1] = 1;                                       \
      request->payload[2] = erd >> 8;                                \
      request->payload[3] = erd & 0xFF;                              \
      request->payload[4] = sizeof(data);                            \
      request->payload[5] = data & 0xFF;                             \
      should_be_sent(request);                                       \
    }                                                                \
    else {                                                           \
      tiny_gea_STATIC_ALLOC_PACKET(request, 7);                      \
      request->source = client_address;                              \
      request->destination = address;                                \
      request->payload[0] = tiny_gea2_erd_api_command_write_request; \
      request->payload[1] = 1;                                       \
      request->payload[2] = erd >> 8;                                \
      request->payload[3] = erd & 0xFF;                              \
      request->payload[4] = sizeof(data);                            \
      request->payload[5] = data >> 8;                               \
      request->payload[6] = data & 0xFF;                             \
      should_be_sent(request);                                       \
    }                                                                \
  } while(0)

  void after_a_read_response_is_received(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 6);
    packet->source = address;
    packet->destination = address;
    packet->payload[0] = tiny_gea2_erd_api_command_read_response;
    packet->payload[1] = 1;
    packet->payload[2] = erd >> 8;
    packet->payload[3] = erd & 0xFF;
    packet->payload[4] = sizeof(data);
    packet->payload[5] = data;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_read_response_for_no_erds_is_received(uint8_t address)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 2);
    packet->source = address;
    packet->destination = address;
    packet->payload[0] = tiny_gea2_erd_api_command_read_response;
    packet->payload[1] = 0;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_read_response_for_multiple_erds_is_received(uint8_t address, tiny_erd_t erd1, uint8_t data1, tiny_erd_t erd2, uint8_t data2)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 10);
    packet->source = address;
    packet->destination = address;
    packet->payload[0] = tiny_gea2_erd_api_command_read_response;
    packet->payload[1] = 2;
    packet->payload[2] = erd1 >> 8;
    packet->payload[3] = erd1 & 0xFF;
    packet->payload[4] = sizeof(data1);
    packet->payload[5] = data1;
    packet->payload[6] = erd2 >> 8;
    packet->payload[7] = erd2 & 0xFF;
    packet->payload[8] = sizeof(data2);
    packet->payload[9] = data2;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_read_response_is_received(uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 7);
    packet->source = address;
    packet->destination = client_address;
    packet->payload[0] = tiny_gea2_erd_api_command_read_response;
    packet->payload[1] = 1;
    packet->payload[2] = erd >> 8;
    packet->payload[3] = erd & 0xFF;
    packet->payload[4] = sizeof(data);
    packet->payload[5] = data >> 8;
    packet->payload[6] = data & 0xFF;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_write_response_is_received(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 4);
    packet->source = address;
    packet->destination = client_address;
    packet->payload[0] = tiny_gea2_erd_api_command_write_response;
    packet->payload[1] = 1;
    packet->payload[2] = erd >> 8;
    packet->payload[3] = erd & 0xFF;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_write_response_for_no_erds_is_received(uint8_t address)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 2);
    packet->source = address;
    packet->destination = client_address;
    packet->payload[0] = tiny_gea2_erd_api_command_write_response;
    packet->payload[1] = 0;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_write_response_for_multiple_erds_is_received(uint8_t address, tiny_erd_t erd1, tiny_erd_t erd2)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 6);
    packet->source = address;
    packet->destination = client_address;
    packet->payload[0] = tiny_gea2_erd_api_command_write_response;
    packet->payload[1] = 2;
    packet->payload[2] = erd1 >> 8;
    packet->payload[3] = erd1 & 0xFF;
    packet->payload[4] = erd2 >> 8;
    packet->payload[5] = erd2 & 0xFF;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_malformed_write_response_is_received(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 5);
    packet->source = address;
    packet->destination = client_address;
    packet->payload[0] = tiny_gea2_erd_api_command_write_response;
    packet->payload[1] = 1;
    packet->payload[2] = erd >> 8;
    packet->payload[3] = erd & 0xFF;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after_a_malformed_read_response_is_received(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    tiny_gea_STACK_ALLOC_PACKET(packet, 7);
    packet->source = address;
    packet->destination = client_address;
    packet->payload[0] = tiny_gea2_erd_api_command_read_response;
    packet->payload[1] = 1;
    packet->payload[2] = erd >> 8;
    packet->payload[3] = erd & 0xFF;
    packet->payload[4] = sizeof(data);
    packet->payload[5] = data;

    tiny_gea_interface_double_trigger_receive(&gea2_interface, packet);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void after_a_read_is_requested(uint8_t address, tiny_erd_t erd)
  {
    bool success = tiny_gea2_erd_client_read(&self.interface, &last_request_id, address, erd);
    CHECK(success);
  }

  void should_fail_to_queue_a_read_request(uint8_t address, tiny_erd_t erd)
  {
    CHECK_FALSE(tiny_gea2_erd_client_read(&self.interface, &last_request_id, address, erd));
  }

  void after_a_write_is_requested(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    bool success = tiny_gea2_erd_client_write(&self.interface, &last_request_id, address, erd, &data, sizeof(data));
    CHECK(success);
  }

  void should_fail_to_queue_a_write_request(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    CHECK_FALSE(tiny_gea2_erd_client_write(&self.interface, &last_request_id, address, erd, &data, sizeof(data)));
  }

  void after_a_write_is_requested(uint8_t address, tiny_erd_t erd, uint16_t data)
  {
    uint16_t big_endian_data = data << 8 | data >> 8;
    bool success = tiny_gea2_erd_client_write(&self.interface, &last_request_id, address, erd, &big_endian_data, sizeof(big_endian_data));
    CHECK(success);
  }

  void should_publish_read_completed(uint8_t address, tiny_erd_t erd, uint8_t data)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("read_completed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8_data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_read_completed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_gea2_erd_client_request_id_t request_id)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("read_completed")
      .withParameter("address", address)
      .withParameter("request_id", request_id)
      .withParameter("erd", erd)
      .withParameter("u8_data", data)
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
      .withParameter("u16_data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_read_failed(uint8_t address, tiny_erd_t erd, tiny_gea2_erd_client_read_failure_reason_t reason)
  {
    mock()
      .expectOneCall("read_failed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_read_failed(uint8_t address, tiny_erd_t erd, tiny_gea2_erd_client_request_id_t request_id, tiny_gea2_erd_client_read_failure_reason_t reason)
  {
    mock()
      .expectOneCall("read_failed")
      .withParameter("address", address)
      .withParameter("request_id", request_id)
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
      .withParameter("u8_data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_write_completed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_gea2_erd_client_request_id_t request_id)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_completed")
      .withParameter("address", address)
      .withParameter("request_id", request_id)
      .withParameter("erd", erd)
      .withParameter("u8_data", data)
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
      .withParameter("u16_data", data)
      .withParameter("data_size", sizeof(data))
      .ignoreOtherParameters();
  }

  void should_publish_write_failed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_gea2_erd_client_write_failure_reason_t reason)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_failed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u8_data", data)
      .withParameter("data_size", sizeof(data))
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_write_failed(uint8_t address, tiny_erd_t erd, uint8_t data, tiny_gea2_erd_client_request_id_t request_id, tiny_gea2_erd_client_write_failure_reason_t reason)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_failed")
      .withParameter("address", address)
      .withParameter("request_id", request_id)
      .withParameter("erd", erd)
      .withParameter("u8_data", data)
      .withParameter("data_size", sizeof(data))
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void should_publish_write_failed(uint8_t address, tiny_erd_t erd, uint16_t data, tiny_gea2_erd_client_write_failure_reason_t reason)
  {
    expected_data_size = sizeof(data);

    mock()
      .expectOneCall("write_failed")
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withParameter("u16_data", data)
      .withParameter("data_size", sizeof(data))
      .withParameter("reason", reason)
      .ignoreOtherParameters();
  }

  void with_an_expected_request_id(tiny_gea2_erd_client_request_id_t expected)
  {
    CHECK_EQUAL(expected, last_request_id);
  }

  void nothing_should_happen()
  {
  }
};

TEST(tiny_gea2_erd_client, should_read)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  a_read_request_should_be_sent(address(0x23), erd(0x5678));
  after_a_read_is_requested(address(0x23), erd(0x5678));
  and_then should_publish_read_completed(address(0x23), erd(0x5678), (uint16_t)1234);
  after_a_read_response_is_received(address(0x23), erd(0x5678), (uint16_t)1234);
}

TEST(tiny_gea2_erd_client, should_allow_a_read_to_be_completed_with_any_address_if_the_destination_is_a_broadcast_address)
{
  a_read_request_should_be_sent(address(0xFF), erd(0x1234));
  after_a_read_is_requested(address(0xFF), erd(0x1234));
  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_gea2_erd_client, should_not_complete_a_read_with_the_wrong_type_address_erd_or_erd_count)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  nothing_should_happen();
  after_a_write_response_is_received(address(0x54), erd(0x1234));
  after_a_read_response_is_received(address(0x55), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(address(0x54), erd(0x1235), (uint8_t)123);
  after_a_read_response_for_no_erds_is_received(address(0x54));
  after_a_read_response_for_multiple_erds_is_received(address(0x54), erd(0x1235), (uint8_t)123, erd(0x1236), (uint8_t)124);
}

TEST(tiny_gea2_erd_client, should_write)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  and_then should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(address(0x54), erd(0x1234));

  a_write_request_should_be_sent(address(0x23), erd(0x5678), (uint16_t)1234);
  after_a_write_is_requested(address(0x23), erd(0x5678), (uint16_t)1234);
  and_then should_publish_write_completed(address(0x23), erd(0x5678), (uint16_t)1234);
  after_a_write_response_is_received(address(0x23), erd(0x5678));
}

TEST(tiny_gea2_erd_client, should_allow_a_write_to_be_completed_with_any_address_if_the_destination_is_a_broadcast_address)
{
  a_write_request_should_be_sent(address(0xFF), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0xFF), erd(0x1234), (uint8_t)123);
  and_then should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(address(0x54), erd(0x1234));
}

TEST(tiny_gea2_erd_client, should_not_complete_a_write_with_the_wrong_type_address_erd_or_erd_count)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  nothing_should_happen();
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(address(0x55), erd(0x1234));
  after_a_write_response_is_received(address(0x54), erd(0x1235));
  after_a_write_response_for_no_erds_is_received(address(0x54));
  after_a_write_response_for_multiple_erds_is_received(address(0x54), erd(0x1235), erd(0x1236));
}

TEST(tiny_gea2_erd_client, should_queue_requests)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  after_a_read_is_requested(address(0x54), erd(0x4321));

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_ a_write_request_should_be_sent(address(0x56), erd(0x5678), (uint8_t)21);
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_write_completed(address(0x56), erd(0x5678), (uint8_t)21);
  and_ a_read_request_should_be_sent(address(0x54), erd(0x4321));
  after_a_write_response_is_received(address(0x56), erd(0x5678));
}

TEST(tiny_gea2_erd_client, should_indicate_when_requests_cannot_be_queued)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  after_a_read_is_requested(address(0x54), erd(0x4321));

  should_fail_to_queue_a_read_request(address(0x75), erd(0x1234));
  should_fail_to_queue_a_write_request(address(0x75), erd(0x5678), (uint8_t)21);
}

TEST(tiny_gea2_erd_client, should_retry_failed_read_requests)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  for(uint8_t i = 0; i < request_retries; i++) {
    nothing_should_happen();
    after(request_timeout - 1);

    a_read_request_should_be_sent(address(0x54), erd(0x1234));
    after(1);
  }

  nothing_should_happen();
  after(request_timeout - 1);

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_gea2_erd_client_read_failure_reason_retries_exhausted);
  after(1);

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_gea2_erd_client, should_retry_failed_write_requests)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  for(uint8_t i = 0; i < request_retries; i++) {
    nothing_should_happen();
    after(request_timeout - 1);

    a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
    after(1);
  }

  nothing_should_happen();
  after(request_timeout - 1);

  should_publish_write_failed(address(0x54), erd(0x1234), (uint8_t)123, tiny_gea2_erd_client_write_failure_reason_retries_exhausted);
  after(1);

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_gea2_erd_client, should_not_retry_successful_requests)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  nothing_should_happen();
  after(request_timeout * 5);
}

TEST(tiny_gea2_erd_client, should_continue_to_the_next_request_after_a_failed_request)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x64), erd(0x0001));

  for(uint8_t i = 0; i < request_retries; i++) {
    a_read_request_should_be_sent(address(0x54), erd(0x1234));
    after(request_timeout);
  }

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_gea2_erd_client_read_failure_reason_retries_exhausted);
  a_read_request_should_be_sent(address(0x64), erd(0x0001));
  after(request_timeout);
}

TEST(tiny_gea2_erd_client, should_reject_malformed_read_requests)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  nothing_should_happen();
  after_a_malformed_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_gea2_erd_client, should_reject_malformed_write_requests)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  nothing_should_happen();
  after_a_malformed_write_response_is_received(address(0x54), erd(0x1234));
}

TEST(tiny_gea2_erd_client, should_ignore_duplicate_read_requests_that_are_back_to_back)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_gea2_erd_client, should_ignore_duplicate_read_requests_that_are_separated_by_another_read)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x5678));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_read_request_should_be_sent(address(0x54), erd(0x5678));
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_read_completed(address(0x54), erd(0x5678), (uint8_t)73);
  after_a_read_response_is_received(address(0x54), erd(0x5678), (uint8_t)73);
}

TEST(tiny_gea2_erd_client, should_not_ignore_duplicate_read_requests_that_are_separated_by_a_write)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  after_a_write_is_requested(address(0x54), erd(0x5678), (uint8_t)7);
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_write_request_should_be_sent(address(0x54), erd(0x5678), (uint8_t)7);
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_write_completed(address(0x54), erd(0x5678), (uint8_t)7);
  and_ a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_write_response_is_received(address(0x54), erd(0x5678));
}

TEST(tiny_gea2_erd_client, should_ignore_duplicate_write_requests_that_are_back_to_back)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  and_then should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(address(0x54), erd(0x1234));
}

TEST(tiny_gea2_erd_client, should_not_ignore_duplicate_write_requests_if_it_would_change_the_values_written)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x5678), (uint8_t)7);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_write_request_should_be_sent(address(0x54), erd(0x5678), (uint8_t)7);
  after_a_write_response_is_received(address(0x54), erd(0x1234));

  should_publish_write_completed(address(0x54), erd(0x5678), (uint8_t)7);
  and_then a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(address(0x54), erd(0x5678));
}

TEST(tiny_gea2_erd_client, should_not_ignore_duplicate_write_requests_if_theres_a_read_between_the_duplicate_writes)
{
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_is_requested(address(0x54), erd(0x5678));
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  and_then a_read_request_should_be_sent(address(0x54), erd(0x5678));
  after_a_write_response_is_received(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x5678), (uint8_t)7);
  and_then a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_read_response_is_received(address(0x54), erd(0x5678), (uint8_t)7);
}

TEST(tiny_gea2_erd_client, should_ignore_responses_when_there_are_no_active_requests)
{
  nothing_should_happen();
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_gea2_erd_client, should_allow_a_new_read_request_in_read_request_complete_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123);
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);
}

TEST(tiny_gea2_erd_client, should_allow_a_new_read_request_in_read_request_failed_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));

  for(uint8_t i = 0; i < request_retries; i++) {
    a_read_request_should_be_sent(address(0x54), erd(0x1234));
    after(request_timeout);
  }

  should_publish_read_failed(address(0x54), erd(0x1234), tiny_gea2_erd_client_read_failure_reason_retries_exhausted);
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after(request_timeout);
}

TEST(tiny_gea2_erd_client, should_allow_a_new_write_request_in_write_request_complete_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  should_publish_write_completed(address(0x54), erd(0x1234), (uint8_t)123);
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_response_is_received(address(0x54), erd(0x1234));
}

TEST(tiny_gea2_erd_client, should_allow_a_new_write_request_in_write_request_failed_callback)
{
  given_that_the_client_will_request_again_on_complete_or_failed();

  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after_a_write_is_requested(address(0x54), erd(0x1234), (uint8_t)123);

  for(uint8_t i = 0; i < request_retries; i++) {
    a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
    after(request_timeout);
  }

  should_publish_write_failed(address(0x54), erd(0x1234), (uint8_t)123, tiny_gea2_erd_client_write_failure_reason_retries_exhausted);
  a_write_request_should_be_sent(address(0x54), erd(0x1234), (uint8_t)123);
  after(request_timeout);
}

TEST(tiny_gea2_erd_client, should_provide_request_ids_for_read_requests)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  with_an_expected_request_id(0);

  after_a_read_is_requested(address(0x56), erd(0x5678));
  with_an_expected_request_id(1);

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123, request_id(0));
  a_read_request_should_be_sent(address(0x56), erd(0x5678));
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  after_a_read_is_requested(address(0x56), erd(0xABCD));
  with_an_expected_request_id(2);

  and_then should_publish_read_completed(address(0x56), erd(0x5678), (uint8_t)21, request_id(1));
  a_read_request_should_be_sent(address(0x56), erd(0xABCD));
  after_a_read_response_is_received(address(0x56), erd(0x5678), (uint8_t)21);

  for(uint8_t i = 0; i < request_retries; i++) {
    a_read_request_should_be_sent(address(0x56), erd(0xABCD));
    after(request_timeout);
  }

  and_then should_publish_read_failed(address(0x56), erd(0xABCD), request_id(2), tiny_gea2_erd_client_read_failure_reason_retries_exhausted);
  after(request_timeout);
}

TEST(tiny_gea2_erd_client, should_provide_the_same_request_id_for_duplicate_read_requests)
{
  a_read_request_should_be_sent(address(0x54), erd(0x1234));
  after_a_read_is_requested(address(0x54), erd(0x1234));
  with_an_expected_request_id(0);

  after_a_read_is_requested(address(0x56), erd(0x5678));
  with_an_expected_request_id(1);

  after_a_read_is_requested(address(0x54), erd(0x1234));
  with_an_expected_request_id(0);

  and_then should_publish_read_completed(address(0x54), erd(0x1234), (uint8_t)123, request_id(0));
  a_read_request_should_be_sent(address(0x56), erd(0x5678));
  after_a_read_response_is_received(address(0x54), erd(0x1234), (uint8_t)123);

  after_a_read_is_requested(address(0x56), erd(0xABCD));
  with_an_expected_request_id(2);

  after_a_read_is_requested(address(0x56), erd(0x5678));
  with_an_expected_request_id(1);
}

TEST(tiny_gea2_erd_client, should_provide_request_ids_for_write_requests)
{
  a_write_request_should_be_sent(address(0x56), erd(0xABCD), (uint8_t)42);
  after_a_write_is_requested(address(0x56), erd(0xABCD), (uint8_t)42);
  with_an_expected_request_id(0);

  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  with_an_expected_request_id(1);

  and_then should_publish_write_completed(address(0x56), erd(0xABCD), (uint8_t)42, request_id(0));
  a_write_request_should_be_sent(address(0x56), erd(0x5678), (uint8_t)21);
  after_a_write_response_is_received(address(0x56), erd(0xABCD));

  after_a_write_is_requested(address(0x56), erd(0x1234), (uint8_t)7);
  with_an_expected_request_id(2);

  for(uint8_t i = 0; i < request_retries; i++) {
    a_write_request_should_be_sent(address(0x56), erd(0x5678), (uint8_t)21);
    after(request_timeout);
  }

  and_then should_publish_write_failed(address(0x56), erd(0x5678), (uint8_t)21, request_id(1), tiny_gea2_erd_client_write_failure_reason_retries_exhausted);
  a_write_request_should_be_sent(address(0x56), erd(0x1234), (uint8_t)7);
  after(request_timeout);

  and_then should_publish_write_completed(address(0x56), erd(0x1234), (uint8_t)7, request_id(2));
  after_a_write_response_is_received(address(0x56), erd(0x1234));
}

TEST(tiny_gea2_erd_client, should_provide_the_same_request_id_for_duplicate_write_requests)
{
  a_write_request_should_be_sent(address(0x56), erd(0xABCD), (uint8_t)42);
  after_a_write_is_requested(address(0x56), erd(0xABCD), (uint8_t)42);
  with_an_expected_request_id(0);

  after_a_write_is_requested(address(0x56), erd(0xABCD), (uint8_t)42);
  with_an_expected_request_id(0);

  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  with_an_expected_request_id(1);

  after_a_write_is_requested(address(0x56), erd(0x5678), (uint8_t)21);
  with_an_expected_request_id(1);

  and_then should_publish_write_completed(address(0x56), erd(0xABCD), (uint8_t)42, request_id(0));
  a_write_request_should_be_sent(address(0x56), erd(0x5678), (uint8_t)21);
  after_a_write_response_is_received(address(0x56), erd(0xABCD));

  after_a_write_is_requested(address(0x56), erd(0x1234), (uint8_t)7);
  with_an_expected_request_id(2);
}
