/*!
 * @file
 * @brief Types for working with the GEA2 ERD API.
 */

#ifndef tiny_gea2_erd_api_h
#define tiny_gea2_erd_api_h

#include <stdint.h>

enum {
  tiny_gea2_erd_api_command_read_request = 0xF0,
  tiny_gea2_erd_api_command_read_response = 0xF0,
  tiny_gea2_erd_api_command_write_request = 0xF1,
  tiny_gea2_erd_api_command_write_response = 0xF1
};
typedef uint8_t tiny_gea3_erd_api_command_t;

typedef struct
{
  uint8_t command;
  uint8_t erd_count;
  uint8_t erd_msb;
  uint8_t erd_lsb;
} tiny_gea2_erd_api_read_request_payload_t;

typedef struct
{
  uint8_t command;
  uint8_t erd_count;
  uint8_t erd_msb;
  uint8_t erd_lsb;
  uint8_t data_size;
} tiny_gea2_erd_api_read_response_payload_header_t;

typedef struct
{
  tiny_gea2_erd_api_read_response_payload_header_t header;
  uint8_t data[1];
} tiny_gea2_erd_api_read_response_payload_t;

typedef struct
{
  uint8_t command;
  uint8_t erd_count;
  uint8_t erd_msb;
  uint8_t erd_lsb;
  uint8_t data_size;
} tiny_gea2_erd_api_write_request_payload_header_t;

typedef struct
{
  tiny_gea2_erd_api_write_request_payload_header_t header;
  uint8_t data[1];
} tiny_gea2_erd_api_write_request_payload_t;

typedef struct
{
  uint8_t command;
  uint8_t erd_count;
  uint8_t erd_msb;
  uint8_t erd_lsb;
} tiny_gea2_erd_api_write_response_payload_t;

#endif
