/*!
 * @file
 * @brief
 *
 * Copyright GE Appliances - Confidential - All rights reserved.
 */

extern "C" {
#include <string.h>
#include "tiny_gea3_constants.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea3_packet.h"
#include "tiny_utils.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_uart_double.hpp"
#include "tiny_utils.h"

enum {
  Address = 0xAD,
  SendBufferSize = 10,
  ReceiveBufferSize = 9,
  IdleCooldownMsec = 10 + (Address & 0x1F),
};

TEST_GROUP(TinyGea2Interface_SingleWire)
{
  tiny_gea2_interface_single_wire_t instance;
  tiny_uart_test_double_t uart;
  tiny_event_subscription_t receiveSubscription;
  uint8_t sendBuffer[SendBufferSize];
  uint8_t receiveBuffer[ReceiveBufferSize];
  tiny_time timeSource;
  TinyEvent_Synchronous_t msecInterrupt;

  void setup()
  {
    ASSUME_UNINITIALIZED(instance);

    TinyEvent_Synchronous_Init(&msecInterrupt);

    TinyUart_TestDouble_Init(&uart);
    TinyTimeSource_TestDouble_Init(&timeSource);

    TinyGea2Interface_SingleWire_Init(
      &instance,
      &uart.interface,
      &timeSource.interface,
      &msecInterrupt.interface,
      receiveBuffer,
      ReceiveBufferSize,
      sendBuffer,
      SendBufferSize,
      Address,
      false);

    TinyEventSubscription_Init(&receiveSubscription, NULL, PacketReceived);
    TinyEvent_Subscribe(TinyGea2Interface_GetOnReceiveEvent(&instance.interface), &receiveSubscription);
  }

  void GivenThatIgnoreDestinationAddressIsEnabled()
  {
    TinyGea2Interface_SingleWire_Init(
      &instance,
      &uart.interface,
      &timeSource.interface,
      &msecInterrupt.interface,
      receiveBuffer,
      ReceiveBufferSize,
      sendBuffer,
      SendBufferSize,
      Address,
      true);

    TinyEvent_Subscribe(TinyGea2Interface_GetOnReceiveEvent(&instance.interface), &receiveSubscription);
  }

  void GivenThatADiagnosticsEventSubscriptionIsActive()
  {
    static TinyEventSubscription_t diagnosticsSubscription;
    TinyEventSubscription_Init(
      &diagnosticsSubscription, NULL, +[](void*, const void* _args) {
        auto args = reinterpret_cast<const TinyGea2InterfaceOnDiagnosticsEventArgs_t*>(_args);
        mock().actualCall("DiagnosticsEvent").withParameter("type", args->type);
      });
    TinyEvent_Subscribe(TinyGea2Interface_GetOnDiagnosticsEvent(&instance.interface), &diagnosticsSubscription);
  }

  void GivenThatRetriesHaveBeenSetTo(uint8_t retries)
  {
    TinyGea2Interface_SingleWire_SetRetries(&instance, retries);
  }

#define ShouldRaiseDiagnosticsEvent(_type) \
  _ShouldRaiseDiagnosticsEvent(TinyGea2InterfaceDiagnosticsEventType_##_type)
  void _ShouldRaiseDiagnosticsEvent(TinyGea2InterfaceDiagnosticsEventType_t type)
  {
    mock().expectOneCall("DiagnosticsEvent").withParameter("type", type);
  }

  static void PacketReceived(void*, const void* _args)
  {
    REINTERPRET(args, _args, const TinyGea2InterfaceOnReceiveArgs_t*);
    mock().actualCall("PacketReceived").withParameterOfType("Gea2Packet_t", "packet", args->packet);
  }

  void WhenByteIsReceived(uint8_t byte)
  {
    TinyUart_TestDouble_TriggerReceive(&uart, byte);
  }

#define ShouldSendBytesViaUart(_bytes...)   \
  do {                                      \
    uint8_t bytes[] = { _bytes };           \
    _ShouldSendBytes(bytes, sizeof(bytes)); \
  } while(0)
  void _ShouldSendBytes(const uint8_t* bytes, uint16_t byteCount)
  {
    for(uint16_t i = 0; i < byteCount; i++) {
      ByteShouldBeSent(bytes[i]);
    }
  }

#define AfterBytesAreReceivedViaUart(_bytes...)          \
  do {                                                   \
    uint8_t bytes[] = { _bytes };                        \
    _AfterBytesAreReceivedViaUart(bytes, sizeof(bytes)); \
  } while(0)

  void _AfterBytesAreReceivedViaUart(const uint8_t* bytes, uint16_t byteCount)
  {
    for(uint16_t i = 0; i < byteCount; i++) {
      WhenByteIsReceived(bytes[i]);
    }
  }

  void PacketShouldBeReceived(const Gea2Packet_t* packet)
  {
    mock().expectOneCall("PacketReceived").withParameterOfType("Gea2Packet_t", "packet", packet);
  }

  void ByteShouldBeSent(uint8_t byte)
  {
    mock().expectOneCall("Send").onObject(&uart).withParameter("byte", byte);
  }

  void AckShouldBeSent()
  {
    mock().expectOneCall("Send").onObject(&uart).withParameter("byte", Gea2Ack);
  }

  void AfterTheInterfaceIsRun()
  {
    TinyGea2Interface_SingleWire_Run(&instance);
  }

  void NothingShouldHappen()
  {
  }

  void After(TinyTimeSourceTickCount_t ticks)
  {
    for(uint32_t i = 0; i < ticks; i++) {
      TinyTimeSource_TestDouble_TickOnce(&timeSource);
      AfterMsecInterruptFires();
    }
  }

  void GivenTheModuleIsInCooldownAfterReceivingAMessage()
  {
    mock().disable();
    AfterBytesAreReceivedViaUart(
      Gea2Stx,
      Address, // dst
      0x08, // len
      0x45, // src
      0xBF, // payload
      0x74, // crc
      0x0D,
      Gea2Etx);

    AfterTheInterfaceIsRun();
    mock().enable();
  }

  static void SendCallback(void* context, Gea2Packet_t* packet)
  {
    REINTERPRET(sourcePacket, context, const Gea2Packet_t*);
    packet->source = sourcePacket->source;
    memcpy(packet->payload, sourcePacket->payload, sourcePacket->payloadLength);
  }

  void WhenPacketIsSent(Gea2Packet_t * packet)
  {
    TinyGea2Interface_Send(&instance.interface, packet->destination, packet->payloadLength, SendCallback, packet);
    AfterMsecInterruptFires();
  }

  void GivenTheUartTestDoubleIsEchoing()
  {
    TinyUart_TestDouble_EnableEcho(&uart);
  }

  void WhenPacketIsForwarded(Gea2Packet_t * packet)
  {
    TinyGea2Interface_Forward(&instance.interface, packet->destination, packet->payloadLength, SendCallback, packet);
    AfterMsecInterruptFires();
  }

  void ShouldNotBeSending()
  {
    CHECK_FALSE(TinyGea2Interface_Sending(&instance.interface));
  }

  void ShouldBeSending()
  {
    CHECK_TRUE(TinyGea2Interface_Sending(&instance.interface));
  }

  void GivenThatASendIsInProgress()
  {
    ShouldSendBytesViaUart(Gea2Stx);

    STATIC_ALLOC_GEA2PACKET(packet, 1);
    packet->destination = 0x45;
    packet->payload[0] = 0xC8;
    WhenPacketIsSent(packet);
  }

  void GivenThatAPacketHasBeenSent()
  {
    GivenTheUartTestDoubleIsEchoing();

    ShouldSendBytesViaUart(
      Gea2Stx,
      0x45, // dst
      0x07, // len
      Address, // src
      0x7D, // crc
      0x39,
      Gea2Etx);

    STATIC_ALLOC_GEA2PACKET(packet, 0);
    packet->destination = 0x45;
    WhenPacketIsSent(packet);
  }

  void ThePacketShouldBeResent()
  {
    ShouldSendBytesViaUart(
      Gea2Stx,
      0x45, // dst
      0x07, // len
      Address, // src
      0x7D, // crc
      0x39,
      Gea2Etx);
  }

  void GivenThatABroadcastPacketHasBeenSent()
  {
    GivenTheUartTestDoubleIsEchoing();

    ShouldSendBytesViaUart(
      Gea2Stx,
      0xFF, // dst
      0x07, // len
      Address, // src
      0x44, // crc
      0x07,
      Gea2Etx);

    STATIC_ALLOC_GEA2PACKET(packet, 0);
    packet->destination = 0xFF;
    WhenPacketIsSent(packet);
  }

  void GivenTheModuleIsInIdleCooldown()
  {
    GivenTheUartTestDoubleIsEchoing();

    ShouldSendBytesViaUart(
      Gea2Stx,
      0x45, // dst
      0x07, // len
      Address, // src
      0x7D, // crc
      0x39,
      Gea2Etx);

    STATIC_ALLOC_GEA2PACKET(packet, 0);
    packet->destination = 0x45;
    WhenPacketIsSent(packet);

    AfterBytesAreReceivedViaUart(Gea2Ack);
  }

  void ShouldBeAbleToSendAMessageAfterIdleCooldown()
  {
    GivenTheUartTestDoubleIsEchoing();

    ShouldSendBytesViaUart(
      Gea2Stx,
      0x45, // dst
      0x07, // len
      Address, // src
      0x7D, // crc
      0x39,
      Gea2Etx);

    STATIC_ALLOC_GEA2PACKET(packet, 0);
    packet->destination = 0x45;
    WhenPacketIsSent(packet);

    After(IdleCooldownMsec);
  }

  void ShouldBeAbleToSendAMessageAfterCoolisionCooldown()
  {
    GivenTheUartTestDoubleIsEchoing();

    STATIC_ALLOC_GEA2PACKET(packet, 0);
    packet->destination = 0x45;
    WhenPacketIsSent(packet);

    ShouldSendBytesViaUart(
      Gea2Stx,
      0x45, // dst
      0x07, // len
      Address, // src
      0x7D, // crc
      0x39,
      Gea2Etx);

    After(CollisionTimeoutMsec());
  }

  void GivenTheModuleIsInCollisionCooldown()
  {
    ShouldSendBytesViaUart(Gea2Stx);
    STATIC_ALLOC_GEA2PACKET(packet, 0);
    packet->destination = 0x45;
    WhenPacketIsSent(packet);
    ShouldBeSending();

    AfterBytesAreReceivedViaUart(Gea2Stx - 1);
  }

  TinyTimeSourceTickCount_t CollisionTimeoutMsec()
  {
    return 43 + (Address & 0x1F) + ((timeSource._private.ticks ^ Address) & 0x1F);
  }

  void AfterMsecInterruptFires()
  {
    TinyEvent_Synchronous_Publish(&msecInterrupt, NULL);
  }
};

TEST(TinyGea2Interface_SingleWire, ShouldReceiveAPacketWithNoPayloadAndSendAnAck)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = Address;
  packet->source = 0x45;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveAPacketWithAPayload)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveAPacketWithMaximumPayload)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x0B, // len
    0x45, // src
    0x01, // payload
    0x02,
    0x03,
    0x04,
    0x94, // crc
    0x48,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 4);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = 0x01;
  packet->payload[1] = 0x02;
  packet->payload[2] = 0x03;
  packet->payload[3] = 0x04;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldRaisePacketReceivedDiagnosticsEventWhenAPacketIsReceived)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldRaiseDiagnosticsEvent(PacketReceived);
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    Gea2Etx);
}

TEST(TinyGea2Interface_SingleWire, ShouldDropPacketsWithPayloadsThatAreTooLarge)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldRaiseDiagnosticsEvent(ReceivedPacketDroppedBecauseOfInvalidLength);
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x0C, // len
    0x45, // src
    0x01, // payload
    0x02,
    0x03,
    0x04,
    0x05,
    0x51, // crc
    0x4B,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveAPacketWithEscapes)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x0B, // len
    0x45, // src
    Gea2Esc, // payload
    Gea2Esc,
    Gea2Esc,
    Gea2Ack,
    Gea2Esc,
    Gea2Stx,
    Gea2Esc,
    Gea2Etx,
    0x31, // crc
    0x3D,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 4);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = Gea2Esc;
  packet->payload[1] = Gea2Ack;
  packet->payload[2] = Gea2Stx;
  packet->payload[3] = Gea2Etx;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveBroadcastPackets)
{
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    0xFF, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEC, // crc
    0x5E,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0xFF;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveProductLineSpecificBroadcastPackets)
{
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    0xF3, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xA3, // crc
    0x6C,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0xF3;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldDropPacketsAddressedToOtherNodes)
{
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address + 1, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEF, // crc
    0xD1,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveMultiplePackets)
{
  {
    AckShouldBeSent();
    AfterBytesAreReceivedViaUart(
      Gea2Stx,
      Address, // dst
      0x07, // len
      0x45, // src
      0x08, // crc
      0x8F,
      Gea2Etx);

    STATIC_ALLOC_GEA2PACKET(packet1, 0);
    packet1->destination = Address;
    packet1->source = 0x45;
    PacketShouldBeReceived(packet1);
    AfterTheInterfaceIsRun();
  }

  {
    AckShouldBeSent();
    AfterBytesAreReceivedViaUart(
      Gea2Stx,
      Address, // dst
      0x08, // len
      0x45, // src
      0xBF, // payload
      0x74, // crc
      0x0D,
      Gea2Etx);

    STATIC_ALLOC_GEA2PACKET(packet2, 1);
    packet2->destination = Address;
    packet2->source = 0x45;
    packet2->payload[0] = 0xBF;
    PacketShouldBeReceived(packet2);
    AfterTheInterfaceIsRun();
  }
}

TEST(TinyGea2Interface_SingleWire, ShouldDropPacketsWithInvalidCrcs)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldRaiseDiagnosticsEvent(ReceivedPacketDroppedBecauseOfInvalidCrc);
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xDE, // crc
    0xAD,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldDropPacketsWithInvalidLength)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldRaiseDiagnosticsEvent(ReceivedPacketDroppedBecauseOfInvalidLength);
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x09, // len
    0x45, // src
    0xBF, // payload
    0xEA, // crc
    0x9C,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldDropPacketsThatAreTooSmall)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldRaiseDiagnosticsEvent(ReceivedPacketDroppedBecauseOfInvalidLength);
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x06, // len
    0x3C, // crc
    0xD4,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldDropPacketsReceivedBeforePublishingAPreviouslyReceivedPacket)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    Gea2Etx);

  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    0xFF, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEC, // crc
    0x5E,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldReceiveAPacketAfterAPreviousPacketIsAborted)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    0xAB,
    0xCD,
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldDropBytesReceivedPriorToStx)
{
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldNotPublishReceivedPacketsPriorToReceivingEtxReceivedBeforeTheInterbyteTimeout)
{
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();

  After(Gea2InterbyteTimeoutMsec - 1);
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = Address;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldRejectPacketsThatViolateTheInterbyteTimeout)
{
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();

  After(Gea2InterbyteTimeoutMsec);

  NothingShouldHappen();
  AfterBytesAreReceivedViaUart(Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldRejectPacketsThatViolateTheInterbyteTimeoutAfterStx)
{
  AfterBytesAreReceivedViaUart(Gea2Stx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();

  After(Gea2InterbyteTimeoutMsec);

  NothingShouldHappen();
  AfterBytesAreReceivedViaUart(
    Address, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0x74, // crc
    0x0D,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldNotReceiveAPacketInIdleIfThePacketDoesNotStartWithStx)
{
  NothingShouldHappen();
  AfterBytesAreReceivedViaUart(
    0x01, // Passes as Stx
    Address, // dst
    0x07, // len
    0xBF, // src
    0x46, // crc
    0xDA,
    Gea2Etx);
}

TEST(TinyGea2Interface_SingleWire, ShouldNotReceiveAPacketInIdleCooldownIfThePacketDoesNotStartWithStx)
{
  GivenTheModuleIsInCooldownAfterReceivingAMessage();

  NothingShouldHappen();
  AfterBytesAreReceivedViaUart(
    0x01, // Passes as Stx
    Address, // dst
    0x07, // len
    0xBF, // src
    0x46, // crc
    0xDA,
    Gea2Etx);
}

TEST(TinyGea2Interface_SingleWire, ShouldSendAPacketWithNoPayload)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x07, // len
    Address, // src
    0x7D, // crc
    0x39,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldSendAPacketWithAPayload)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x08, // len
    Address, // src
    0xD5, // payload
    0x21, // crc
    0xD3,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldSendAPacketWithMaxPayloadGivenSendBufferSize)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x0E, // len
    Address, // src
    0x00, // payload
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x12, // crc
    0xD5,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 7);
  packet->destination = 0x45;
  packet->payload[0] = 0x00;
  packet->payload[1] = 0x01;
  packet->payload[2] = 0x02;
  packet->payload[3] = 0x03;
  packet->payload[4] = 0x04;
  packet->payload[5] = 0x05;
  packet->payload[6] = 0x06;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldRaiseAPacketSentEventWhenAPacketIsSent)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x08, // len
    Address, // src
    0xD5, // payload
    0x21, // crc
    0xD3,
    Gea2Etx);
  ShouldRaiseDiagnosticsEvent(PacketSent);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldNotSendAPacketThatIsTooLargeForTheSendBuffer)
{
  STATIC_ALLOC_GEA2PACKET(packet, 8);

  GivenThatADiagnosticsEventSubscriptionIsActive();
  ShouldRaiseDiagnosticsEvent(SentPacketDroppedBecauseItWasLargerThanTheSendBuffer);
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldEscapeDataBytesWhenSending)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x08, // len
    Address, // src
    0xE0, // escape
    0xE1, // payload
    0x57, // crc
    0x04,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xE1;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldEscapeCrcLsbWhenSending)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x08, // len
    Address, // src
    0xA0, // payload
    0x0F, // crc
    0xE0,
    0xE1,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xA0;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldEscapeCrcMsbWhenSending)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x08, // len
    Address, // src
    0xC8, // payload
    0xE0, // crc
    0xE2,
    0x4F,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = 0x45;
  packet->payload[0] = 0xC8;
  WhenPacketIsSent(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldAllowPacketsToBeForwarded)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x08, // len
    0x32, // src
    0xD5, // payload
    0x29, // crc
    0x06,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->source = 0x32;
  packet->destination = 0x45;
  packet->payload[0] = 0xD5;
  WhenPacketIsForwarded(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldForwardAPacketWithMaxPayloadGivenSendBufferSize)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x0E, // len
    Address, // src
    0x00, // payload
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x12, // crc
    0xD5,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 7);
  packet->source = Address;
  packet->destination = 0x45;
  packet->payload[0] = 0x00;
  packet->payload[1] = 0x01;
  packet->payload[2] = 0x02;
  packet->payload[3] = 0x03;
  packet->payload[4] = 0x04;
  packet->payload[5] = 0x05;
  packet->payload[6] = 0x06;
  WhenPacketIsForwarded(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldNotForwardPacketsThatAreTooLargeToBeBuffered)
{
  STATIC_ALLOC_GEA2PACKET(packet, 8);

  GivenThatADiagnosticsEventSubscriptionIsActive();
  ShouldRaiseDiagnosticsEvent(SentPacketDroppedBecauseItWasLargerThanTheSendBuffer);
  WhenPacketIsForwarded(packet);
}

TEST(TinyGea2Interface_SingleWire, ShouldNotBeSendingAfterInitialization)
{
  ShouldNotBeSending();
}

TEST(TinyGea2Interface_SingleWire, ShouldIndicateWhenSending)
{
  GivenThatASendIsInProgress();
  ShouldBeSending();
}

TEST(TinyGea2Interface_SingleWire, ShouldNotBeSendingAfterSendIsCompleteForABroadcastMessage)
{
  GivenThatABroadcastPacketHasBeenSent();
  ShouldNotBeSending();
}

TEST(TinyGea2Interface_SingleWire, ShouldBeAbleToSendBackToBackBroadcastsWithoutAnAck)
{
  GivenThatABroadcastPacketHasBeenSent();
  ShouldBeAbleToSendAMessageAfterIdleCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldNotBeSendingAfterSendIsCompleteAndAnAckHasBeenReceived)
{
  GivenThatAPacketHasBeenSent();

  AfterBytesAreReceivedViaUart(Gea2Ack);
  ShouldNotBeSending();
}

TEST(TinyGea2Interface_SingleWire, ShouldWaitUntilTheIdleCoolDownTimeHasExpiredBeforeSendingAPacket)
{
  GivenTheUartTestDoubleIsEchoing();
  GivenTheModuleIsInCooldownAfterReceivingAMessage();
  ShouldNotBeSending();

  NothingShouldHappen();
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);
  ShouldBeSending();

  ShouldBeAbleToSendAMessageAfterIdleCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldRetrySendingWhenTheReflectionTimeoutViolationOccursAndStopAfterRetriesAreExhausted)
{
  ShouldSendBytesViaUart(Gea2Stx);
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);
  ShouldBeSending();

  NothingShouldHappen();
  After(GEA2_REFLECTION_TIMEOUT_MSEC - 1);

  NothingShouldHappen();
  After(1);
  ShouldBeSending();

  NothingShouldHappen();
  After(IdleCooldownMsec - 1);

  ShouldSendBytesViaUart(Gea2Stx);
  After(1);
  ShouldBeSending();

  NothingShouldHappen();
  After(GEA2_REFLECTION_TIMEOUT_MSEC + IdleCooldownMsec - 1);

  ShouldSendBytesViaUart(Gea2Stx);
  After(1);
  ShouldBeSending();

  NothingShouldHappen();
  After(GEA2_REFLECTION_TIMEOUT_MSEC - 1);
  ShouldBeSending();

  After(1);
  ShouldNotBeSending();

  ShouldBeAbleToSendAMessageAfterIdleCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldRaiseAReflectionTimedOutDiagnosticsEventWhenAReflectionTimeoutRetrySendingWhenTheReflectionTimeoutViolationOccursAndStopAfterRetriesAreExhausted)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldSendBytesViaUart(Gea2Stx);
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);

  ShouldRaiseDiagnosticsEvent(SingleWireReflectionTimedOut);
  After(GEA2_REFLECTION_TIMEOUT_MSEC);
}

TEST(TinyGea2Interface_SingleWire, ShouldRetrySendingWhenACollisionOccursAndStopAfterRetriesAreExhausted)
{
  ShouldSendBytesViaUart(Gea2Stx);
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);
  ShouldBeSending();

  AfterBytesAreReceivedViaUart(Gea2Stx - 1);
  ShouldBeSending();

  NothingShouldHappen();
  After(CollisionTimeoutMsec() - 1);

  ShouldSendBytesViaUart(Gea2Stx);
  After(1);
  ShouldBeSending();

  AfterBytesAreReceivedViaUart(Gea2Stx - 1);
  ShouldBeSending();

  NothingShouldHappen();
  After(CollisionTimeoutMsec() - 1);

  ShouldSendBytesViaUart(Gea2Stx);
  After(1);
  ShouldBeSending();

  AfterBytesAreReceivedViaUart(Gea2Stx - 1);
  ShouldNotBeSending();

  ShouldBeAbleToSendAMessageAfterCoolisionCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldRetrySendingWhenACollisionOccursAndStopAfterRetriesAreExhaustedWithACustomRetryCount)
{
  GivenThatRetriesHaveBeenSetTo(1);

  ShouldSendBytesViaUart(Gea2Stx);
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);
  ShouldBeSending();

  AfterBytesAreReceivedViaUart(Gea2Stx - 1);
  ShouldBeSending();

  NothingShouldHappen();
  After(CollisionTimeoutMsec() - 1);

  ShouldSendBytesViaUart(Gea2Stx);
  After(1);
  ShouldBeSending();

  AfterBytesAreReceivedViaUart(Gea2Stx - 1);
  ShouldNotBeSending();

  ShouldBeAbleToSendAMessageAfterCoolisionCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldRaiseACollisionDetectedDiagnosticsEventWhenACollisionOccurs)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldSendBytesViaUart(Gea2Stx);
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);
  ShouldBeSending();

  ShouldRaiseDiagnosticsEvent(SingleWireCollisionDetected);
  AfterBytesAreReceivedViaUart(Gea2Stx - 1);
}

TEST(TinyGea2Interface_SingleWire, ShouldStopSendingWhenAnUnexpectedByteIsReceivedWhileWaitingForAnAck)
{
  GivenThatAPacketHasBeenSent();

  AfterBytesAreReceivedViaUart(Gea2Ack - 1);
  ShouldBeSending();

  NothingShouldHappen();
  After(CollisionTimeoutMsec() - 1);

  ThePacketShouldBeResent();
  After(1);

  AfterBytesAreReceivedViaUart(Gea2Ack - 1);
  ShouldBeSending();

  NothingShouldHappen();
  After(CollisionTimeoutMsec() - 1);

  ThePacketShouldBeResent();
  After(1);

  AfterBytesAreReceivedViaUart(Gea2Ack - 1);
  ShouldNotBeSending();

  ShouldBeAbleToSendAMessageAfterCoolisionCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldIgnoreSendRequestsWhenAlreadySending)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldSendBytesViaUart(Gea2Stx);
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);

  ShouldRaiseDiagnosticsEvent(SentPacketDroppedBecauseASendWasInProgress);
  STATIC_ALLOC_GEA2PACKET(differentPacket, 0);
  packet->destination = 0x80;
  WhenPacketIsSent(differentPacket);

  ShouldSendBytesViaUart(0x45);
  AfterBytesAreReceivedViaUart(Gea2Stx);
}

TEST(TinyGea2Interface_SingleWire, ShouldRetryAMessageIfNoAckIsReceived)
{
  GivenTheUartTestDoubleIsEchoing();

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x07, // len
    Address, // src
    0x7D, // crc
    0x39,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);

  NothingShouldHappen();
  After(Gea2AckTimeoutMsec);
  After(CollisionTimeoutMsec() - 1);

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x07, // len
    Address, // src
    0x7D, // crc
    0x39,
    Gea2Etx);
  After(1);

  NothingShouldHappen();
  After(Gea2AckTimeoutMsec);
  After(CollisionTimeoutMsec() - 1);

  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x07, // len
    Address, // src
    0x7D, // crc
    0x39,
    Gea2Etx);
  After(1);

  NothingShouldHappen();
  After(Gea2AckTimeoutMsec - 1);
  ShouldBeSending();

  After(1);
  ShouldNotBeSending();

  ShouldBeAbleToSendAMessageAfterCoolisionCooldown();
}

TEST(TinyGea2Interface_SingleWire, ShouldSuccessfullyReceiveAPacketWhileInCollisionCooldown)
{
  GivenTheModuleIsInCollisionCooldown();

  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = Address;
  packet->source = 0x45;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldNotReceiveAPacketWhileInCollisionCooldownThatDoesNotStartWithStx)
{
  GivenTheModuleIsInCollisionCooldown();

  AfterBytesAreReceivedViaUart(
    Address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    Gea2Etx);

  NothingShouldHappen();
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldRestartIdleTimeoutWhenByteTrafficOccurs)
{
  GivenTheModuleIsInIdleCooldown();

  NothingShouldHappen();
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);

  NothingShouldHappen();
  After(IdleCooldownMsec - 1);
  AfterBytesAreReceivedViaUart(Gea2Stx + 1);

  NothingShouldHappen();
  After(1);
}

TEST(TinyGea2Interface_SingleWire, ShouldNotStartReceivingAPacketWhileAReceivedPacketIsReady)
{
  GivenThatADiagnosticsEventSubscriptionIsActive();

  ShouldRaiseDiagnosticsEvent(PacketReceived);
  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x07, // len
    0x45, // src
    0x08, // crc
    0x8F,
    Gea2Etx);

  After(IdleCooldownMsec);

  ShouldRaiseDiagnosticsEvent(ReceivedByteDroppedBecauseAPacketWasPendingPublication);
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address, // dst
    0x07, // len
    0x05, // src
    0x40, // crc
    0x4B,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = Address;
  packet->source = 0x45;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}

TEST(TinyGea2Interface_SingleWire, ShouldHandleFailureToSendDuringAnEscape)
{
  ShouldSendBytesViaUart(
    Gea2Stx,
    0xE0);

  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0xE1;
  WhenPacketIsSent(packet);

  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    0x00);

  After(CollisionTimeoutMsec() - 1);

  GivenTheUartTestDoubleIsEchoing();
  ShouldSendBytesViaUart(
    Gea2Stx,
    0xE0, // escape
    0xE1, // dst
    0x07, // len
    Address, // src
    0x1C, // crc
    0x65,
    Gea2Etx);
  After(1);
}

TEST(TinyGea2Interface_SingleWire, ShouldEnterIdleCooldownWhenANonStxByteIsReceivedInIdle)
{
  AfterBytesAreReceivedViaUart(Gea2Stx - 1);

  NothingShouldHappen();
  STATIC_ALLOC_GEA2PACKET(packet, 0);
  packet->destination = 0x45;
  WhenPacketIsSent(packet);

  NothingShouldHappen();
  After(IdleCooldownMsec - 1);

  GivenTheUartTestDoubleIsEchoing();
  ShouldSendBytesViaUart(
    Gea2Stx,
    0x45, // dst
    0x07, // len
    Address, // src
    0x7D, // crc
    0x39,
    Gea2Etx);
  After(1);
}

TEST(TinyGea2Interface_SingleWire, ShouldReceivePacketsAddressedToOtherNodesWhenIgnoreDestinationAddressIsEnabled)
{
  GivenThatIgnoreDestinationAddressIsEnabled();

  AckShouldBeSent();
  AfterBytesAreReceivedViaUart(
    Gea2Stx,
    Address + 1, // dst
    0x08, // len
    0x45, // src
    0xBF, // payload
    0xEF, // crc
    0xD1,
    Gea2Etx);

  STATIC_ALLOC_GEA2PACKET(packet, 1);
  packet->destination = Address + 1;
  packet->source = 0x45;
  packet->payload[0] = 0xBF;
  PacketShouldBeReceived(packet);
  AfterTheInterfaceIsRun();
}
