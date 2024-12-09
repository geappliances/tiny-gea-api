/*!
 * @file
 * @brief
 */

#ifndef tiny_erd_client_double_hpp
#define tiny_erd_client_double_hpp

extern "C" {
#include "i_tiny_erd_client.h"
#include "tiny_event.h"
}

typedef struct {
  i_tiny_erd_client_t interface;
  tiny_event_t on_activity;
} tiny_erd_client_double_t;

/*!
 * Initialize an ERD client double.
 */
void tiny_erd_client_double_init(
  tiny_erd_client_double_t* self);

/*!
 * Trigger publication via the on_activity event.
 */
void tiny_erd_client_double_trigger_activity_event(
  tiny_erd_client_double_t* self,
  const tiny_erd_client_on_activity_args_t* args);

#endif
