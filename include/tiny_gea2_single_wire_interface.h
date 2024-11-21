/*!
 * @file
 * @brief
 *
 * This component is interrupt-aware and handles byte transmit/receive in the
 * interrupt context. Publication of messages is done via a background task in
 * tiny_gea2_interface_single_wire_run() so the application does not have to do
 * anything special.
 *
 * Additionally, this component does not do any queueing of packets. If a send
 * is in progress and another message is sent, then the currently sending message
 * is discarded. In order to prevent this, clients can check whether the interface
 * is currently sending and wait before attempting to send a packet.
 *
 * If a message is received, all messages received after will be dropped until
 * tiny_gea2_interface_single_wire_run() is called.
 * Copyright GE Appliances - Confidential - All rights reserved.
 */

#ifndef TINYGEA2INTERFACE_SINGLEWIRE_H
#define TINYGEA2INTERFACE_SINGLEWIRE_H

#include "hal/i_tiny_uart.h"
#include "i_tiny_gea3_interface.h"
#include "i_tiny_time_source.h"
#include "tiny_crc16.h"
#include "tiny_event.h"
#include "tiny_fsm.h"
#include "tiny_timer.h"

typedef struct
{
  uint8_t type;
} tiny_gea2_interface_diagnostics_args_t;

typedef struct
{
  i_tiny_gea3_interface_t interface;

  struct
  {
    tiny_fsm_t fsm;
    tiny_event_t onReceive;
    tiny_event_t onDiagnosticsEvent;
    tiny_event_subscription_t msecInterruptSubscription;
    tiny_event_subscription_t byteReceivedSubscription;
    i_tiny_uart_t* uart;
    tiny_timer_t timer;
    uint8_t address;
    bool ignoreDestinationAddress;
    uint8_t retries;
    tiny_timer_group_t timerGroup;

    struct
    {
      uint8_t* buffer;
      uint8_t bufferSize;
      uint8_t state;
      uint8_t offset;
      uint16_t crc;
      bool escaped;
      volatile bool active;
      volatile bool packetQueuedInBackground;
      uint8_t expectedReflection;
      uint8_t retries;
    } send;

    struct
    {
      uint8_t* buffer;
      uint16_t crc;
      uint8_t bufferSize;
      uint8_t count;
      bool escaped;
      volatile bool packetReady;
    } receive;
  } _private;
} tiny_gea2_interface_single_wire_t;

/*!
 * @param instance
 * @param uart
 * @param timeSource
 * @param msecInterrupt Used to run timing in the interrupt context. Must not pre-empt or be pre-empted by
 *   UART interrupts.
 * @param receiveBuffer
 * @param receiveBufferSize
 * @param sendBuffer
 * @param sendBufferSize
 * @param address
 * @param ignoreDestinationAddress Receives all valid packets when this is enabled to allow for routing or sniffing.
 */
void tiny_gea2_interface_single_wire_init(
  tiny_gea2_interface_single_wire_t* instance,
  i_tiny_uart_t* uart,
  i_tiny_time_source_t* timeSource,
  i_tiny_event_t* msecInterrupt,
  uint8_t* receiveBuffer,
  uint8_t receiveBufferSize,
  uint8_t* sendBuffer,
  uint8_t sendBufferSize,
  uint8_t address,
  bool ignoreDestinationAddress);

/*!
 * Will emit received packets. Run this in the background context.
 * @param instance
 */
void tiny_gea2_interface_single_wire_run(tiny_gea2_interface_single_wire_t* instance);

/*!
 * @param instance
 * @param retries
 */
void tiny_gea2_interface_single_wire_set_retries(tiny_gea2_interface_single_wire_t* instance, uint8_t retries);

#endif
