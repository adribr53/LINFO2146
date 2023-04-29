#include "contiki.h"
#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"

#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include "dev/button-sensor.h"

#include <stdio.h> /* For printf() */

#include <stdlib.h>
#include <string.h>
#include <radio.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "Sensor node");
AUTOSTART_PROCESSES(&nullnet_example_process);
/*---------------------------------------------------------------------------*/

#define MAX_PAYLOAD_LENGTH (uint8_t) 42

typedef enum {
  SENSOR_NODE = 0,
  COORDINATOR_NODE = 1,
  BORDER_NODE = 2,
  UNDEFINED_NODE = 3
} node_type;

typedef enum {
  DISCOVERY_TYPE = 0,
  MESSAGE_TYPE = 1,
} packet_type;

typedef struct packet {
  packet_type type;
  node_type snd;
  uint8_t payload[MAX_PAYLOAD_LENGTH];
} packet_t;

#define OWN_TYPE SENSOR_NODE;
static linkaddr_t parent;
static node_type parent_type = UNDEFINED_NODE;
// static linkaddr_t* child_nodes;
radio_value_t  parent_strength;
struct etimer parent_last_update;
static packet_t to_send;

radio_value_t get_strength() {
  return 42;
}

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (!linkaddr_cmp(src, &linkaddr_node_addr) && len == sizeof(packet_t)) {
    packet_t pkt;
    memcpy(&pkt, data, sizeof(packet_t));
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    if (pkt.type == DISCOVERY_TYPE) {
      LOG_INFO("Discovery\n");
      if (pkt.snd == COORDINATOR_NODE) {
          LOG_INFO("Discovery + coordinator\n");
          if (parent_type != COORDINATOR_NODE) {
            // old parent : unkown or sensor
            memcpy(&parent, src, sizeof(linkaddr_t));
            LOG_INFO("New parent :");
            LOG_INFO_LLADDR(&parent);
            LOG_INFO("\n");
            parent_type = COORDINATOR_NODE;
            parent_strength = get_strength();
            // TODO: parent_last_update = now()
          } else {
              // compare strengt
              radio_value_t new_strength = get_strength();
              if (new_strength > parent_strength) {
                parent = *src; // TODO: working ?
                parent_strength = new_strength;
                // TODO: parent_last_update = now()
              }
          }
      } else if (pkt.snd == SENSOR_NODE) {
          LOG_INFO("Discovery + sensor\n");
          if (parent_type != COORDINATOR_NODE) {
              if (parent_type != UNDEFINED_NODE) {
                  // already have a parent
                  radio_value_t new_strength = get_strength();
                  if (new_strength > parent_strength) {
                    parent = *src; // TODO: working ?
                    parent_strength = new_strength;
                    // TODO: parent_last_update = now()
                  }
              } else {
                  LOG_INFO("Discovery + sensor : new parent\n");
                  memcpy(&parent, src, sizeof(linkaddr_t));
                  LOG_INFO("New parent :");
                  LOG_INFO_LLADDR(&parent);
                  LOG_INFO("\n");
                  parent_type = SENSOR_NODE;
                  parent_strength = get_strength();
                  // TODO: parent_last_update = now()
              }
          } // else discard
      } // else border or bug
    } else if (pkt.type == MESSAGE_TYPE) {
      LOG_INFO("Message\n");
      if (parent_type != UNDEFINED_NODE) { // security check
        LOG_INFO("Fowarding to ");
        LOG_INFO_LLADDR(&parent);
        LOG_INFO("\n");
        memcpy(&to_send, &pkt, sizeof(packet_t));
        NETSTACK_NETWORK.output(&parent);
      }
    }
  }
}

PROCESS_THREAD(nullnet_example_process, ev, data) {
  //static struct etimer timer;

  PROCESS_BEGIN();
  SENSORS_ACTIVATE(button_sensor);
  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&to_send;
  nullnet_len = sizeof(packet_t);
  nullnet_set_input_callback(input_callback);

  printf("Starting process\n");
  LOG_INFO("Type : Sensor\n");
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL((ev == sensors_event) && (data == &button_sensor));
    // send_discovery();
    to_send.snd = OWN_TYPE;
    if (parent_type == UNDEFINED_NODE) {
      LOG_INFO("Sending discovery broadcast\n");
      to_send.type = DISCOVERY_TYPE;
      NETSTACK_NETWORK.output(NULL);
    } else {
      LOG_INFO("Sending message to");
      LOG_INFO_LLADDR(&parent);
      LOG_INFO("\n");
      to_send.type = MESSAGE_TYPE;
      NETSTACK_NETWORK.output(&parent);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
