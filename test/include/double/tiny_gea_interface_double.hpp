/*!
 * @file
 * @brief
 */

#ifndef tiny_gea_interface_double_hpp
#define tiny_gea_interface_double_hpp

extern "C" {
#include <stdint.h>
#include "i_tiny_gea_interface.h"
#include "tiny_event.h"
};

typedef struct {
  i_tiny_gea_interface_t interface;

  uint8_t address;
  tiny_event_t on_receive;
  union {
    uint8_t send_buffer[UINT8_MAX];
    tiny_gea_packet_t packet;
  };
} tiny_gea_interface_double_t;

/*!
 * Initialize a GEA3 interface test double.
 */
void tiny_gea_interface_double_init(
  tiny_gea_interface_double_t* self,
  uint8_t address);

/*!
 * Raise an on receive event as if a packet had been received.
 */
void tiny_gea_interface_double_trigger_receive(
  tiny_gea_interface_double_t* self,
  const tiny_gea_packet_t* packet);

#endif
