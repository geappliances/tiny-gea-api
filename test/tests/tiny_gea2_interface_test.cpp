/*!
 * @file
 * @brief
 */

extern "C" {
#include <string.h>
#include "tiny_gea2_interface.h"
#include "tiny_gea_constants.h"
#include "tiny_gea_packet.h"
#include "tiny_timer.h"
#include "tiny_utils.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_time_source_double.hpp"
#include "double/tiny_uart_double.hpp"
#include "tiny_utils.h"

enum {
  address = 0xAD,
  send_buffer_size = 10,
  receive_buffer_size = 9,
  idle_cooldown_msec = 10 + (address & 0x1F),
  gea2_reflection_timeout_msec = 6,
  tiny_gea_ack_timeout_msec = 8,
  gea2_broadcast_mask = 1,
  default_retries = 2,
  gea2_packet_transmission_overhead = 3,
  gea2_interbyte_timeout_msec = 6,

};

TEST_GROUP(tiny_gea2_interface)
{
  tiny_gea2_interface_t instance;
  tiny_uart_double_t uart;
  tiny_event_subscription_t receiveSubscription;
  uint8_t send_buffer[send_buffer_size];
  uint8_t receive_buffer[receive_buffer_size];
  tiny_time_source_double_t time_source;
  tiny_event_t msec_interrupt;

  void setup()
  {
    tiny_event_init(&msec_interrupt);

    tiny_uart_double_init(&uart);
    tiny_time_source_double_init(&time_source);

    tiny_gea2_interface_init(
      &instance,
      &uart.interface,
      &time_source.interface,
      &msec_interrupt.interface,
      receive_buffer,
      receive_buffer_size,
      send_buffer,
      send_buffer_size,
      address,
      false,
      default_retries);

    tiny_event_subscription_init(&receiveSubscription, NULL, packet_received);
    tiny_event_subscribe(tiny_gea_interface_on_receive(&instance.interface), &receiveSubscription);
  }

  void given_that_ignore_destination_address_is_enabled()
  {
    tiny_gea2_interface_init(
      &instance,
      &uart.interface,
      &time_source.interface,
      &msec_interrupt.interface,
      receive_buffer,
      receive_buffer_size,
      send_buffer,
      send_buffer_size,
      address,
      true,
      default_retries);

    tiny_event_subscribe(tiny_gea_interface_on_receive(&instance.interface), &receiveSubscription);
  }

  void given_that_retries_have_been_set_to(uint8_t retries)
  {
    tiny_gea2_interface_init(
      &instance,
      &uart.interface,
      &time_source.interface,
      &msec_interrupt.interface,
      receive_buffer,
      receive_buffer_size,
      send_buffer,
      send_buffer_size,
      address,
      false,
      retries);
  }

  static void packet_received(void*, const void* _args)
  {
    reinterpret(args, _args, const tiny_gea_interface_on_receive_args_t*);
    mock()
      .actualCall("packet_received")
      .withParameter("source", args->packet->source)
      .withParameter("destination", args->packet->destination)
      .withMemoryBufferParameter("payload", args->packet->payload, args->packet->payload_length);
  }

  void when_byte_is_received(uint8_t byte)
  {
    tiny_uart_double_trigger_receive(&uart, byte);
  }

#define should_send_bytes_via_uart(_bytes...) \
  do {                                        \
    uint8_t bytes[] = { _bytes };             \
    _should_send_bytes(bytes, sizeof(bytes)); \
  } while(0)
  void _should_send_bytes(const uint8_t* bytes, uint16_t byte_count)
  {
    for(uint16_t i = 0; i < byte_count; i++) {
      byte_should_be_sent(bytes[i]);
    }
  }

#define after_bytes_are_received_via_uart(_bytes...)          \
  do {                                                        \
    uint8_t bytes[] = { _bytes };                             \
    _after_bytes_are_received_via_uart(bytes, sizeof(bytes)); \
  } while(0)

  void _after_bytes_are_received_via_uart(const uint8_t* bytes, uint16_t byte_count)
  {
    for(uint16_t i = 0; i < byte_count; i++) {
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

  void byte_should_be_sent(uint8_t byte)
  {
    mock().expectOneCall("send").onObject(&uart).withParameter("byte", byte);
  }

  void ack_should_be_sent()
  {
    mock().expectOneCall("send").onObject(&uart).withParameter("byte", tiny_gea_ack);
  }

  void after_the_interface_is_run()
  {
    tiny_gea2_interface_run(&instance);
  }

  void nothing_should_happen()
  {
  }

  void after(tiny_time_source_ticks_t ticks)
  {
    for(uint32_t i = 0; i < ticks; i++) {
      tiny_time_source_double_tick(&time_source, 1);
      after_msec_interrupt_fires();
    }
  }

  void given_the_module_is_in_cooldown_after_receiving_a_message()
  {
    mock().disable();
    after_bytes_are_received_via_uart(
      tiny_gea_stx,
      address, // dst
      0x08, // len
      0x45, // src
      0xBF, // payload
      0x74, // crc
      0x0D,
      tiny_gea_etx);

    after_the_interface_is_run();
    mock().enable();
  }

  static void send_callback(void* context, tiny_gea_packet_t* packet)
  {
    reinterpret(source_packet, context, const tiny_gea_packet_t*);
    packet->source = source_packet->source;
    memcpy(packet->payload, source_packet->payload, source_packet->payload_length);
  }

  void when_packet_is_sent(tiny_gea_packet_t * packet)
  {
    tiny_gea_interface_send(&instance.interface, packet->destination, packet->payload_length, packet, send_callback);
    after_msec_interrupt_fires();
  }

  void when_packet_is_forwarded(tiny_gea_packet_t * packet)
  {
    tiny_gea_interface_forward(&instance.interface, packet->destination, packet->payload_length, packet, send_callback);
    after_msec_interrupt_fires();
  }

  void given_that_a_send_is_in_progress()
  {
    should_send_bytes_via_uart(tiny_gea_stx);

    tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
    packet->destination = 0x45;
    packet->payload[0] = 0xC8;
    when_packet_is_sent(packet);
  }

  void gjven_that_a_packet_has_been_sent()
  {
    given_uart_echoing_is_enabled();

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

  void the_packet_should_be_resent()
  {
    should_send_bytes_via_uart(
      tiny_gea_stx,
      0x45, // dst
      0x07, // len
      address, // src
      0x7D, // crc
      0x39,
      tiny_gea_etx);
  }

  void given_that_a_broadcast_packet_has_been_sent()
  {
    given_uart_echoing_is_enabled();
    should_send_bytes_via_uart(
      tiny_gea_stx,
      0xFF, // dst
      0x07, // len
      address, // src
      0x44, // crc
      0x07,
      tiny_gea_etx);

    tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
    packet->destination = 0xFF;
    when_packet_is_sent(packet);
  }

  void given_uart_echoing_is_enabled()
  {
    tiny_uart_double_enable_echo(&uart);
  }

  void given_the_module_is_in_idle_cooldown()
  {
    given_uart_echoing_is_enabled();
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

    after_bytes_are_received_via_uart(tiny_gea_ack);
  }

  void should_be_able_to_send_a_message_after_idle_cooldown()
  {
    given_uart_echoing_is_enabled();
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

    after(idle_cooldown_msec);
  }

  void should_be_able_to_send_a_message_after_collision_cooldown()
  {
    given_uart_echoing_is_enabled();
    tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
    packet->destination = 0x45;
    when_packet_is_sent(packet);

    should_send_bytes_via_uart(
      tiny_gea_stx,
      0x45, // dst
      0x07, // len
      address, // src
      0x7D, // crc
      0x39,
      tiny_gea_etx);

    after(collision_timeout_msec());
  }

  void given_the_module_is_in_collision_cooldown()
  {
    should_send_bytes_via_uart(tiny_gea_stx);
    tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
    packet->destination = 0x45;
    when_packet_is_sent(packet);

    after_bytes_are_received_via_uart(tiny_gea_stx - 1);
  }

  tiny_time_source_ticks_t collision_timeout_msec()
  {
    return 43 + (address & 0x1F) + ((time_source.ticks ^ address) & 0x1F);
  }

  void after_msec_interrupt_fires()
  {
    tiny_event_publish(&msec_interrupt, NULL);
  }
};

TEST(tiny_gea2_interface, should_receive_a_packet_with_no_payload_and_send_an_ack)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_receive_a_packet_with_a_payload)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_receive_a_packet_with_maximum_payload)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_raise_packet_received_diagnostics_event_when_a_packet_is_received)
{
  ack_should_be_sent();
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    tiny_gea_etx);
}

TEST(tiny_gea2_interface, should_drop_packets_with_payloads_that_are_too_large)
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

TEST(tiny_gea2_interface, should_receive_a_packet_with_escapes)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_receive_broadcast_packets)
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

TEST(tiny_gea2_interface, should_receive_product_line_specific_broadcast_packets)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    0xF3, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xA3, // crc
    0x6C,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = 0xF3;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea2_interface, should_drop_packets_addressed_to_other_nodes)
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

TEST(tiny_gea2_interface, should_receive_multiple_packets)
{
  {
    ack_should_be_sent();
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
    ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_drop_packets_with_invalid_crcs)
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

TEST(tiny_gea2_interface, should_drop_packets_with_invalid_length)
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

TEST(tiny_gea2_interface, should_drop_packets_that_are_too_small)
{
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x06, // len
    0x3C, // crc
    0xD4,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea2_interface, should_drop_packets_received_before_publishing_a_previously_received_packet)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_receive_a_packet_after_a_previous_packet_is_aborted)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_drop_bytes_received_prior_to_stx)
{
  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_not_publish_received_packets_prior_to_receiving_etx_received_before_the_interbyte_timeout)
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

  after(gea2_interbyte_timeout_msec - 1);
  ack_should_be_sent();
  after_bytes_are_received_via_uart(tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->destination = address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea2_interface, should_reject_packets_that_violate_the_interbyte_timeout)
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

  after(gea2_interbyte_timeout_msec);

  nothing_should_happen();
  after_bytes_are_received_via_uart(tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea2_interface, should_reject_packets_that_violate_the_interbyte_timeout_after_stx)
{
  after_bytes_are_received_via_uart(tiny_gea_stx);

  nothing_should_happen();
  after_the_interface_is_run();

  after(gea2_interbyte_timeout_msec);

  nothing_should_happen();
  after_bytes_are_received_via_uart(
    address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    tiny_gea_etx);

  nothing_should_happen();
  after_the_interface_is_run();
}

TEST(tiny_gea2_interface, should_not_receive_a_packet_in_idle_if_the_packet_does_not_start_with_stx)
{
  nothing_should_happen();
  after_bytes_are_received_via_uart(
    0x01, // Passes as Stx
    address, // dst
    0x07, // len
    0xBF, // src
    0x46, // crc
    0xDA,
    tiny_gea_etx);
}

TEST(tiny_gea2_interface, should_not_receive_a_packet_in_idle_cooldown_if_the_packet_does_not_start_with_stx)
{
  given_the_module_is_in_cooldown_after_receiving_a_message();

  nothing_should_happen();
  after_bytes_are_received_via_uart(
    0x01, // Passes as Stx
    address, // dst
    0x07, // len
    0xBF, // src
    0x46, // crc
    0xDA,
    tiny_gea_etx);
}

TEST(tiny_gea2_interface, should_send_a_packet_with_no_payload)
{
  given_uart_echoing_is_enabled();
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

TEST(tiny_gea2_interface, should_send_a_packet_with_a_payload)
{
  given_uart_echoing_is_enabled();

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

TEST(tiny_gea2_interface, should_send_a_packet_with_max_payload_given_send_buffer_size)
{
  given_uart_echoing_is_enabled();

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

TEST(tiny_gea2_interface, should_raise_a_packet_sent_event_when_a_packet_is_sent)
{
  given_uart_echoing_is_enabled();

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

TEST(tiny_gea2_interface, should_not_send_a_packet_that_is_too_large_for_the_send_buffer)
{
  given_uart_echoing_is_enabled();

  tiny_gea_STATIC_ALLOC_PACKET(packet, 8);

  when_packet_is_sent(packet);
}

TEST(tiny_gea2_interface, should_escape_data_bytes_when_sending)
{
  given_uart_echoing_is_enabled();

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

TEST(tiny_gea2_interface, should_escape_crc_lsb_when_sending)
{
  given_uart_echoing_is_enabled();

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

TEST(tiny_gea2_interface, should_escape_crc_msb_when_sending)
{
  given_uart_echoing_is_enabled();

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

TEST(tiny_gea2_interface, should_allow_packets_to_be_forwarded)
{
  given_uart_echoing_is_enabled();

  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x08, // len
    0x32, // src
    0xD5, // payload
    0x29, // crc
    0x06,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 1);
  packet->source = 0x32;
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;
  when_packet_is_forwarded(packet);
}

TEST(tiny_gea2_interface, should_forward_a_packet_with_max_payload_given_send_buffer_size)
{
  given_uart_echoing_is_enabled();

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
  packet->source = address;
  packet->destination = 0x45;
  packet->payload[0] = 0x00;
  packet->payload[1] = 0x01;
  packet->payload[2] = 0x02;
  packet->payload[3] = 0x03;
  packet->payload[4] = 0x04;
  packet->payload[5] = 0x05;
  packet->payload[6] = 0x06;
  when_packet_is_forwarded(packet);
}

TEST(tiny_gea2_interface, should_not_forward_packets_that_are_too_large_to_be_buffered)
{
  given_uart_echoing_is_enabled();

  tiny_gea_STATIC_ALLOC_PACKET(packet, 8);

  when_packet_is_forwarded(packet);
}

TEST(tiny_gea2_interface, should_be_able_to_send_back_broadcasts_without_an_ack)
{
  given_uart_echoing_is_enabled();

  given_that_a_broadcast_packet_has_been_sent();
  should_be_able_to_send_a_message_after_idle_cooldown();
}

TEST(tiny_gea2_interface, should_wait_until_the_idle_cool_down_time_has_expired_before_sending_a_packet)
{
  given_uart_echoing_is_enabled();

  given_the_module_is_in_cooldown_after_receiving_a_message();

  nothing_should_happen();
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  should_be_able_to_send_a_message_after_idle_cooldown();
}

TEST(tiny_gea2_interface, should_retry_sending_when_the_reflection_timeout_violation_occurs_and_stop_after_retries_are_exhausted)
{
  should_send_bytes_via_uart(tiny_gea_stx);
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  nothing_should_happen();
  after(gea2_reflection_timeout_msec - 1);

  nothing_should_happen();
  after(1);

  nothing_should_happen();
  after(idle_cooldown_msec - 1);

  should_send_bytes_via_uart(tiny_gea_stx);
  after(1);

  nothing_should_happen();
  after(gea2_reflection_timeout_msec + idle_cooldown_msec - 1);

  should_send_bytes_via_uart(tiny_gea_stx);
  after(1);

  nothing_should_happen();
  after(gea2_reflection_timeout_msec - 1);

  after(1);

  should_be_able_to_send_a_message_after_idle_cooldown();
}

TEST(tiny_gea2_interface, should_raise_reflection_timed_out_diagnostics_event_when_a_reflection_timeout_retry_sending_when_the_reflection_timeout_violation_occurs_and_stop_after_retrries_are_exhausted)
{
  should_send_bytes_via_uart(tiny_gea_stx);
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  after(gea2_reflection_timeout_msec);
}

TEST(tiny_gea2_interface, should_retry_sending_when_a_collision_occurs_and_stop_after_retries_are_exhausted)
{
  should_send_bytes_via_uart(tiny_gea_stx);
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  after_bytes_are_received_via_uart(tiny_gea_stx - 1);

  nothing_should_happen();
  after(collision_timeout_msec() - 1);

  should_send_bytes_via_uart(tiny_gea_stx);
  after(1);

  after_bytes_are_received_via_uart(tiny_gea_stx - 1);

  nothing_should_happen();
  after(collision_timeout_msec() - 1);

  should_send_bytes_via_uart(tiny_gea_stx);
  after(1);

  after_bytes_are_received_via_uart(tiny_gea_stx - 1);

  should_be_able_to_send_a_message_after_collision_cooldown();
}

TEST(tiny_gea2_interface, should_retry_sending_when_a_collision_occurs_and_stop_after_retries_are_exhausted_with_a_custom_retry_count)
{
  given_that_retries_have_been_set_to(1);

  should_send_bytes_via_uart(tiny_gea_stx);
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  after_bytes_are_received_via_uart(tiny_gea_stx - 1);

  nothing_should_happen();
  after(collision_timeout_msec() - 1);

  should_send_bytes_via_uart(tiny_gea_stx);
  after(1);

  after_bytes_are_received_via_uart(tiny_gea_stx - 1);

  should_be_able_to_send_a_message_after_collision_cooldown();
}

TEST(tiny_gea2_interface, should_stop_sending_when_an_unexpected_byte_is_received_while_waiting_for_an_ack)
{
  gjven_that_a_packet_has_been_sent();

  after_bytes_are_received_via_uart(tiny_gea_ack - 1);

  nothing_should_happen();
  after(collision_timeout_msec() - 1);

  the_packet_should_be_resent();
  after(1);

  after_bytes_are_received_via_uart(tiny_gea_ack - 1);

  nothing_should_happen();
  after(collision_timeout_msec() - 1);

  the_packet_should_be_resent();
  after(1);

  after_bytes_are_received_via_uart(tiny_gea_ack - 1);

  should_be_able_to_send_a_message_after_collision_cooldown();
}

TEST(tiny_gea2_interface, should_ignore_send_requests_when_already_sending)
{
  should_send_bytes_via_uart(tiny_gea_stx);
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  tiny_gea_STATIC_ALLOC_PACKET(differentPacket, 0);
  packet->destination = 0x80;
  when_packet_is_sent(differentPacket);

  should_send_bytes_via_uart(0x45);
  after_bytes_are_received_via_uart(tiny_gea_stx);
}

TEST(tiny_gea2_interface, should_retry_a_message_if_no_ack_is_received)
{
  given_uart_echoing_is_enabled();
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

  nothing_should_happen();
  after(tiny_gea_ack_timeout_msec);
  after(collision_timeout_msec() - 1);

  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x07, // len
    address, // src
    0x7D, // crc
    0x39,
    tiny_gea_etx);
  after(1);

  nothing_should_happen();
  after(tiny_gea_ack_timeout_msec);
  after(collision_timeout_msec() - 1);

  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x07, // len
    address, // src
    0x7D, // crc
    0x39,
    tiny_gea_etx);
  after(1);

  nothing_should_happen();
  after(tiny_gea_ack_timeout_msec - 1);

  after(1);

  should_be_able_to_send_a_message_after_collision_cooldown();
}

TEST(tiny_gea2_interface, should_successfully_receive_a_packet_while_in_collision_cooldown)
{
  given_the_module_is_in_collision_cooldown();

  ack_should_be_sent();
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

TEST(tiny_gea2_interface, should_not_receive_a_packet_while_in_collision_cooldown_that_does_not_start_with_stx)
{
  given_the_module_is_in_collision_cooldown();

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

TEST(tiny_gea2_interface, should_restart_idle_timeout_when_byte_traffic_occurs)
{
  given_uart_echoing_is_enabled();

  given_the_module_is_in_idle_cooldown();

  nothing_should_happen();
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  nothing_should_happen();
  after(idle_cooldown_msec - 1);
  after_bytes_are_received_via_uart(tiny_gea_stx + 1);

  nothing_should_happen();
  after(1);
}

TEST(tiny_gea2_interface, should_not_start_receiving_a_packet_while_a_received_packet_is_ready)
{
  given_uart_echoing_is_enabled();

  ack_should_be_sent();
  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    tiny_gea_etx);

  after(idle_cooldown_msec);

  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    address, // dst
    0x07, // len
    0x05, // src
    0x40, // crc
    0x4B,
    tiny_gea_etx);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = address;
  packet->source = 0x45;
  packet_should_be_received(packet);
  after_the_interface_is_run();
}

TEST(tiny_gea2_interface, should_handle_a_failure_to_send_during_an_escape)
{
  should_send_bytes_via_uart(
    tiny_gea_stx,
    0xE0);

  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0xE1;
  when_packet_is_sent(packet);

  after_bytes_are_received_via_uart(
    tiny_gea_stx,
    0x00);

  after(collision_timeout_msec() - 1);

  given_uart_echoing_is_enabled();

  should_send_bytes_via_uart(
    tiny_gea_stx,
    0xE0, // escape
    0xE1, // dst
    0x07, // len
    address, // src
    0x1C, // crc
    0x65,
    tiny_gea_etx);
  after(1);
}

TEST(tiny_gea2_interface, should_enter_idle_cooldown_when_a_non_stx_byte_is_received_in_idle)
{
  given_uart_echoing_is_enabled();

  after_bytes_are_received_via_uart(tiny_gea_stx - 1);

  nothing_should_happen();
  tiny_gea_STATIC_ALLOC_PACKET(packet, 0);
  packet->destination = 0x45;
  when_packet_is_sent(packet);

  nothing_should_happen();
  after(idle_cooldown_msec - 1);

  should_send_bytes_via_uart(
    tiny_gea_stx,
    0x45, // dst
    0x07, // len
    address, // src
    0x7D, // crc
    0x39,
    tiny_gea_etx);
  after(1);
}

TEST(tiny_gea2_interface, should_receive_packets_addressed_to_other_nodes_when_ignore_destination_address_is_enabled)
{
  given_uart_echoing_is_enabled();

  given_that_ignore_destination_address_is_enabled();

  ack_should_be_sent();
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
