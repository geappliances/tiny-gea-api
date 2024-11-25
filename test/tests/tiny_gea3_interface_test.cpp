/*!
 * @file
 * @brief
 */

extern "C" {
#include <string.h>
#include "tiny_gea3_interface.h"
#include "tiny_gea_constants.h"
#include "tiny_gea_packet.h"
#include "tiny_utils.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_uart_double.hpp"
#include "tiny_utils.h"

TEST_GROUP(tiny_gea3_interface)
{
  enum {
    address = 0xAD,

    send_buffer_size = 10,
    receive_buffer_size = 9,
    send_queue_size = 20
  };

  tiny_gea3_interface_t self;
  tiny_uart_double_t uart;
  tiny_event_subscription_t receive_subscription;
  uint8_t send_buffer[send_buffer_size];
  uint8_t receive_buffer[receive_buffer_size];
  uint8_t send_queue[send_queue_size];

  void setup()
  {
    mock().strictOrder();

    given_that_the_interface_has_been_initialized();
  }

  void given_that_the_interface_has_been_initialized()
  {
    memset(&self, 0xA5, sizeof(self));

    tiny_uart_double_init(&uart);

    tiny_gea3_interface_init(
      &self,
      &uart.interface,
      address,
      send_buffer,
      sizeof(send_buffer),
      receive_buffer,
      sizeof(receive_buffer),
      send_queue,
      sizeof(send_queue),
      false);

    tiny_event_subscription_init(&receive_subscription, NULL, packet_received);

    tiny_event_subscribe(tiny_gea3_interface_on_receive(&self.interface), &receive_subscription);

    tiny_uart_double_configure_automatic_send_complete(&uart, true);
  }

  void given_that_the_interface_is_ignoring_destination_addresses()
  {
    tiny_gea3_interface_init(
      &self,
      &uart.interface,
      address,
      send_buffer,
      sizeof(send_buffer),
      receive_buffer,
      sizeof(receive_buffer),
      send_queue,
      sizeof(send_queue),
      true);

    tiny_event_subscribe(tiny_gea3_interface_on_receive(&self.interface), &receive_subscription);
  }

  static void packet_received(void*, const void* _args)
  {
    reinterpret(args, _args, const tiny_gea3_interface_on_receive_args_t*);
    mock()
      .actualCall("packet_received")
      .withParameter("source", args->packet->source)
      .withParameter("destination", args->packet->destination)
      .withMemoryBufferParameter("payload", args->packet->payload, args->packet->payload_length);
  }

  void byte_should_be_sent(uint8_t byte)
  {
    mock().expectOneCall("send").onObject(&uart).withParameter("byte", byte);
  }

#define should_send_bytes_via_uart(_bytes...) \
  do {                                        \
    uint8_t bytes[] = { _bytes };             \
    _should_send_bytes(bytes, sizeof(bytes)); \
  } while(0)
  void _should_send_bytes(const uint8_t* bytes, uint16_t byteCount)
  {
    for(uint16_t i = 0; i < byteCount; i++) {
      byte_should_be_sent(bytes[i]);
    }
  }

  void when_byte_is_received(uint8_t byte)
  {
    tiny_uart_double_trigger_receive(&uart, byte);
  }

#define after_bytes_are_received_via_uart(_bytes...)          \
  do {                                                        \
    uint8_t bytes[] = { _bytes };                             \
    _after_bytes_are_received_via_uart(bytes, sizeof(bytes)); \
  } while(0)
  void _after_bytes_are_received_via_uart(const uint8_t* bytes, uint16_t byteCount)
  {
    for(uint16_t i = 0; i < byteCount; i++) {
      when_byte_is_received(bytes[i]);
    }
  }

  void packet_should_be_received(const tiny_gea_packet_t* packet)
  {
    mock()
      .expectOneCall("packet_received")
      .withParameter("source", packet->source)
      .withParameter("destination", packet->destination)
      .withMemoryBufferParameter("payload", packet->payload, packet->payload_length);
  }

  static void send_callback(void* context, tiny_gea_packet_t* packet)
  {
    reinterpret(sourcePacket, context, const tiny_gea_packet_t*);
    packet->source = sourcePacket->source;
    memcpy(packet->payload, sourcePacket->payload, sourcePacket->payload_length);
  }

  void when_packet_is_sent(tiny_gea_packet_t * packet)
  {
    tiny_gea3_interface_send(&self.interface, packet->destination, packet->payload_length, send_callback, packet);
  }

  void when_packet_is_forwarded(tiny_gea_packet_t * packet)
  {
    tiny_gea3_interface_forward(&self.interface, packet->destination, packet->payload_length, send_callback, packet);
  }

  void packet_should_fail_to_send(tiny_gea_packet_t * packet)
  {
    CHECK_FALSE(tiny_gea3_interface_send(&self.interface, packet->destination, packet->payload_length, send_callback, packet));
  }

  void given_that_a_packet_has_been_sent()
  {
    should_send_bytes_via_uart(
      tiny_gea_stx,
      0x45, // dst
      0x07, // len
      address, // src
      0x7D, // crc
      0x39,
      tiny_gea_etx);

    tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
    packet->destination = 0x45;
    when_packet_is_sent(packet);
  }

  void given_that_automatic_send_complete_is(bool enabled)
  {
    tiny_uart_double_configure_automatic_send_complete(&uart, enabled);
  }

  void given_that_the_queue_is_full()
  {
    given_that_automatic_send_complete_is(false);

    should_send_bytes_via_uart(tiny_gea_stx);

    tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
    packet->destination = 0x45;
    packet->payload[0] = 0xD5;

    uint8_t packet_size = tiny_gea_packet_overhead + 1;
    uint8_t queue_size_in_packets = send_queue_size / packet_size;

    for(int i = 0; i < queue_size_in_packets - 1; i++) {
      tiny_gea3_interface_send(&self.interface, packet->destination, packet->payload_length, send_callback, packet);
    }
  }

  void after_send_completes()
  {
    tiny_uart_double_trigger_send_complete(&uart);
  }

  void after_the_interface_is_run()
  {
    tiny_gea3_interface_run(&self);
  }

  void nothing_should_happen()
  {
  }
};

TEST(tiny_gea3_interface, should_send_a_packet_with_no_payload)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x07, // len
    address, // src
    0x7D, // crc
    0x39,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_forward_a_packet_without_changing_source_address)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x07, // len
    address + 1, // src
    0x4D, // crc
    0x5A,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  packet->source = address + 1;
  when_packet_is_forwarded(packet);
}

TEST(tiny_gea3_interface, should_send_a_packet_with_a_payload)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x08, // len
    address, // src
    0xD5, // payload
    0x21, // crc
    0xD3,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_send_a_packet_with_max_payload_given_send_buffer_size)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x0E, // len
    address, // src
    0x00, // payload
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x12, // crc
    0xD5,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 7);
  packet->destination = 0x45;
  packet->payload[0] = 0x00;
  packet->payload[1] = 0x01;
  packet->payload[2] = 0x02;
  packet->payload[3] = 0x03;
  packet->payload[4] = 0x04;
  packet->payload[5] = 0x05;
  packet->payload[6] = 0x06;
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_not_send_a_packet_that_is_too_large_for_the_send_buffer)
{
  tiny_gea_STATIC_ALLOC_PACKET(packet, 8);

  nothing_should_happen();
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_escape_data_bytes_when_sending)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x08, // len
    address, // src
    0xE0, // escape
    0xE1, // payload
    0x57, // crc
    0x04,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xE1;
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_escape_crc_lsb_when_sending)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x08, // len
    address, // src
    0xA0, // payload
    0x0F, // crc
    0xE0,
    0xE1,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xA0;
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_escape_crc_msb_when_sending)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x08, // len
    address, // src
    0xC8, // payload
    0xE0, // crc
    0xE2,
    0x4F,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xC8;
  when_packet_is_sent(packet);
}

TEST(tiny_gea3_interface, should_queue_sent_packets)
{
  given_that_automatic_send_complete_is(false);

  should_send_bytes_via_uart(tiny_gea_stx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;
  when_packet_is_sent(packet);
  when_packet_is_sent(packet);

  given_that_automatic_send_complete_is(true);

  should_send_bytes_via_uart(
    0x45, // dst
    0x08, // len
    address, // src
    0xD5, // payload
    0x21, // crc
    0xD3,
    tiny_gea_etx,
    tiny_gea_stx,
    0x45, // dst
    0x08, // len
    address, // src
    0xD5, // payload
    0x21, // crc
    0xD3,
    tiny_gea_etx);

  after_send_completes();
}

TEST(tiny_gea3_interface, should_report_failure_to_enqueue)
{
  given_that_the_queue_is_full();

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;

  packet_should_fail_to_send(packet);
}

TEST(tiny_gea3_interface, should_receive_a_packet_with_no_payload)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = address;
  packet->source = 0x45;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_not_receive_a_packet_with_no_stx)
{
  after_bytes_are_received_via_uart(
    address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_a_packet_with_a_payload)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_a_packet_with_maximum_payload)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x0B, // len
    0x45, // src
    0x01, // payload
    0x02,
    0x03,
    0x04,
    0x94, // crc
    0x48,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 4);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0x01;
  packet->payload[1] = 0x02;
  packet->payload[2] = 0x03;
  packet->payload[3] = 0x04;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_drop_packets_with_payloads_that_are_too_large)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x0C, // len
    0x45, // src
    0x01, // payload
    0x02,
    0x03,
    0x04,
    0x05,
    0x51, // crc
    0x4B,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_a_packet_with_escapes)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x0B, // len
    0x45, // src
    tiny_gea_esc, // payload
    tiny_gea_esc,
    tiny_gea_esc,
    tiny_gea_ack,
    tiny_gea_esc,
    tiny_gea_stx,
    tiny_gea_esc,
    tiny_gea_etx,
    0x31, // crc
    0x3D,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 4);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = tiny_gea_esc;
  packet->payload[1] = tiny_gea_ack;
  packet->payload[2] = tiny_gea_stx;
  packet->payload[3] = tiny_gea_etx;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_broadcast_packets)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    0xFF, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEC, // crc
    0x5E,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0xFF;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_drop_packets_addressed_to_other_nodes)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address + 1, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEF, // crc
    0xD1,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_packets_with_any_address_when_ignoring_destination)
{
  given_that_the_interface_is_ignoring_destination_addresses();

  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address + 1, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEF, // crc
    0xD1,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address + 1;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_multiple_packets)
{
  {
    after_bytes_are_received_via_uart(
      tiny_gea_stx,
      address, // dst
      0x07, // len
      0x45, // src
      0x08, // crc
      0x8F,
      tiny_gea_etx);

    tiny_gea_STATIC_ALLOC_PACKET(packet1, 0);
    packet1->destination = address;
    packet1->source = 0x45;
    packet_should_be_received(packet1);
    after_the_interface_is_run();
  }

  {
    after_bytes_are_received_via_uart(
      tiny_gea_stx,
      address, // dst
      0x08, // len
      0x45, // src
      0xBF, // payload
      0x74, // crc
      0x0D,
      tiny_gea_etx);

    tiny_gea_STATIC_ALLOC_PACKET(packet2, 1);
    packet2->destination = address;
    packet2->source = 0x45;
    packet2->payload[0] = 0xBF;
    packet_should_be_received(packet2);
    after_the_interface_is_run();
  }
}

TEST(tiny_gea3_interface, should_drop_packets_with_invalid_crcs)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xDE, // crc
    0xAD,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_drop_packets_with_invalid_length)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x09, // len
    0x45, // src
    0xBF, // payload
    0xEA, // crc
    0x9C,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_drop_packets_received_before_publishing_a_previously_received_packet)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    tiny_gea_etx);

  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    0xFF, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEC, // crc
    0x5E,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_receive_a_packet_after_a_previous_packet_is_aborted)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    0xAB,
    0xCD,
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_drop_bytes_received_prior_to_stx)
{
  after_bytes_are_received_via_uart(
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea3_interface, should_not_publish_received_packets_prior_to_receiving_etx)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D);

  nothing_should_happen();
  after_the_interface_is_run();

  after_bytes_are_received_via_uart(tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}
