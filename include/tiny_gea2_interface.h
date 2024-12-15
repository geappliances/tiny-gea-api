/*!
 * @file
 * @brief
 *
 * This component is interrupt-aware and safely handles byte transmit/receive in
 * an interrupt context. Publication of messages is done via a background task in
 * tiny_gea2_interface_run() so the application does not have to do anything
 * special to maintain context safety when receiving packets.
 *
 * If a message is received, all messages received after will be dropped until
 * tiny_gea2_interface_run() is called.
 *
 * Note: This module requires an interrupt event. This "interrupt" is an event
 * that needs to happen in the same context as the `on_receive`.
 */

#ifndef tiny_gea2_interface_h
#define tiny_gea2_interface_h

#include "hal/i_tiny_uart.h"
#include "i_tiny_gea_interface.h"
#include "i_tiny_time_source.h"
#include "tiny_crc16.h"
#include "tiny_event.h"
#include "tiny_fsm.h"
#include "tiny_queue.h"
#include "tiny_timer.h"

typedef struct
{
  i_tiny_gea_interface_t interface;

  tiny_fsm_t fsm;
  tiny_event_t on_receive;
  tiny_event_t on_diagnostics_event;
  tiny_event_subscription_t msec_interrupt_subscription;
  tiny_event_subscription_t byte_received_subscription;
  i_tiny_uart_t* uart;
  tiny_timer_t timer;
  uint8_t address;
  bool ignore_destination_address;
  uint8_t retries;
  tiny_timer_group_t timer_group;

  struct
  {
    tiny_queue_t queue;
    uint8_t state;
    uint8_t offset;
    uint16_t crc;
    bool escaped;
    volatile bool in_progress; // Set and cleared by the non-ISR, read by the ISR
    volatile bool completed; // Set by ISR, cleared by non-ISR
    volatile bool packet_queued_in_background; // Set by ISR, cleared by non-ISR
    uint8_t expected_reflection;
    uint8_t retries;
    uint8_t data_length;
  } send;

  struct
  {
    uint8_t* buffer;
    uint16_t crc;
    uint8_t buffer_size;
    uint8_t count;
    bool escaped;
    volatile bool packet_ready;
  } receive;
} tiny_gea2_interface_t;

/*!
 * Initialize a GEA2 interface.
 */
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
  uint8_t retries);

/*!
 * Run the interface and publish received packets.
 */
void tiny_gea2_interface_run(tiny_gea2_interface_t* self);

#endif
