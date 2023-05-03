#include "contiki.h"
#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"

#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include "dev/button-sensor.h"

#include <stdio.h> /* For printf() */

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <radio.h>
#include <arch/dev/radio/cc2420/cc2420.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "Sensor node");
AUTOSTART_PROCESSES(&nullnet_example_process);
/*---------------------------------------------------------------------------*/

#define MAX_PAYLOAD_LENGTH (uint8_t) 42
#define PERIOD (18 * CLOCK_SECOND)
#define DURATION (1 * CLOCK_SECOND)

typedef enum
{
  SENSOR_NODE = 0,
  COORDINATOR_NODE = 1,
  BORDER_NODE = 2,
  UNDEFINED_NODE = 3
} node_type;

typedef enum
{
  DISCOVERY_TYPE = 0,
  MESSAGE_TYPE = 1,
  SYNCHRO_TYPE = 2
} packet_type;

typedef struct packet
{
  node_type node : 2;
  packet_type msg : 2;
  unsigned payload : 12;
  clock_time_t clock : 32;
} packet_t;

#define BROADCAST NULL
#define OWN_TYPE SENSOR_NODE
static linkaddr_t parent;
static node_type parent_type = UNDEFINED_NODE;
static uint8_t parent_ok = 0;
// static linkaddr_t* child_nodes;
radio_value_t  parent_strength;
struct etimer parent_last_update;
static packet_t to_send;

// static unsigned received_clock = 0;

void send_pkt(node_type node, packet_type type, unsigned payload, clock_time_t clock_v, linkaddr_t *dest) {
  to_send.node = node;
  to_send.msg = type;
  to_send.payload = payload;
  to_send.clock = clock_v;    
  memcpy(nullnet_buf, &to_send, sizeof(to_send));
  nullnet_len = sizeof(to_send);        
  NETSTACK_NETWORK.output(dest); 
}

static const struct radio_driver *radio = &cc2420_driver;

radio_value_t get_strength() {
  radio_value_t strength = 0;
  radio_result_t res = radio->get_value(RADIO_PARAM_LAST_RSSI, &strength);
  if (res != RADIO_RESULT_OK) {
    return INT_MIN;
  } else {
    return strength;
  }
}

int is_unicast(const linkaddr_t *dest) {
  return linkaddr_cmp(dest, &linkaddr_node_addr);
}

int is_parent(const linkaddr_t *addr) {
  return linkaddr_cmp(&parent, addr);
}

uint16_t get_sensor_count() {return 1 + (rand() % 5);}

static uint8_t must_respond = 0;

#define MAX_CHILDREN 16
static linkaddr_t children[MAX_CHILDREN]; // TODO resize if necessary
static uint16_t total_count = 0;
static uint8_t number_of_children = 0;

// Variable for 
uint8_t child_has_repond = 0;
uint8_t current_child = 0;
uint8_t starting_child = 0;
clock_time_t child_interval;
clock_time_t must_repond_before;

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (!linkaddr_cmp(src, &linkaddr_node_addr) && len == sizeof(packet_t)) {
    packet_t pkt;
    memcpy(&pkt, data, sizeof(packet_t));
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    if (is_unicast(dest)) {
      switch (pkt.msg) {
        case DISCOVERY_TYPE:
          LOG_INFO("Discovery\n");
          if (pkt.node == COORDINATOR_NODE) {
              // parent candidate
              if (parent_type != COORDINATOR_NODE) {
                // old parent : unkown or sensor
                memcpy(&parent, src, sizeof(linkaddr_t));
                parent_type = COORDINATOR_NODE;
                parent_strength = get_strength();
                LOG_INFO("New parent : coordinator => ");
                LOG_INFO_LLADDR(&parent);
                LOG_INFO("\n");
                // TODO: parent_last_update = now()
              } else {
                  // compare strengt
                  radio_value_t new_strength = get_strength();
                  if (new_strength > parent_strength) {
                    LOG_INFO("New parent : coordinator with higher signal strength :");
                    LOG_INFO_LLADDR(&parent);
                    LOG_INFO("\n");
                    memcpy(&parent, src, sizeof(linkaddr_t));
                    parent_strength = new_strength;
                    // TODO: parent_last_update = now()
                  }
              }
          } else if (pkt.node == SENSOR_NODE) {
            if (parent_ok) {
              // child discovery
              printf("New child\n");
              children[number_of_children] = *src;
              number_of_children++;
            } else {
              // parent candidate
              LOG_INFO("Discovery + sensor\n");
              if (parent_type != COORDINATOR_NODE) {
                  if (parent_type != UNDEFINED_NODE) {
                      // already have a parent
                      radio_value_t new_strength = get_strength();
                      if (new_strength > parent_strength) {
                        memcpy(&parent, src, sizeof(linkaddr_t));
                        parent_strength = new_strength;
                        LOG_INFO("New parent : sensor with higher signal strength :");
                        LOG_INFO_LLADDR(&parent);
                        LOG_INFO("\n");
                        // TODO: parent_last_update = now()
                      }
                  } else {
                      // LOG_INFO("Discovery + sensor : new parent\n");
                      memcpy(&parent, src, sizeof(linkaddr_t));
                      LOG_INFO("New parent : sensor");
                      LOG_INFO_LLADDR(&parent);
                      LOG_INFO("\n");
                      parent_type = SENSOR_NODE;
                      parent_strength = get_strength();
                      // TODO: parent_last_update = now()
                  }
              } // else discard
            }
          }
          break;
        case MESSAGE_TYPE:
          LOG_INFO("Message\n");
          if (is_parent(src)) {
            if (number_of_children == 0) {
              printf("No children => direct response\n");
              send_pkt(OWN_TYPE, MESSAGE_TYPE, get_sensor_count(), 0, &parent);
            } else {
              printf("Have children => must take care of them, clock => %lu\n", pkt.clock);
              total_count = 0;
              must_respond = 1;
              must_repond_before = clock_time() + pkt.clock;
              child_interval = pkt.clock / (number_of_children + 5);
              // current_child = (current_child + 1) % number_of_children;
              starting_child = current_child;
              process_poll(&nullnet_example_process);
              child_has_repond = 0;
              printf("Ask to the first child\n");
              send_pkt(OWN_TYPE, MESSAGE_TYPE, 0, child_interval, &children[current_child]);
            }
          } else {
            if (must_respond && linkaddr_cmp(src, &children[current_child])) {
              // if right child respond
              total_count += pkt.payload;
              child_has_repond = 1;
            } else {
              // TODO: maybe else => indicate the child is still alive
                LOG_INFO_LLADDR(src);
                printf("Sent a message\n");
              }
          }
          break;
        default:
          printf("Unsupported packet type %x\n", pkt.msg);
          break;
      }
    } else {
      printf("Broadcast\n");
      if (pkt.msg == DISCOVERY_TYPE && pkt.node == SENSOR_NODE) {
        // Child node request for parent
        static linkaddr_t to_callback;
        memcpy(&to_callback, src, sizeof(linkaddr_t));
        send_pkt(OWN_TYPE, DISCOVERY_TYPE, 0, 0, &to_callback);
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

  printf("Starting process as Sensor\n");

  static struct etimer wait_for_parents;
  static struct etimer wait_interval;

  while(1) {
    if (!parent_ok) {
      // No parent
      send_pkt(DISCOVERY_TYPE, OWN_TYPE, 0, 0, BROADCAST);
      etimer_set(&wait_for_parents, 2*PERIOD);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_for_parents));
      // TODO: Do parent process here
      if (parent_type != UNDEFINED_NODE) {
        printf("Sending ok to the parent\n");
        send_pkt(OWN_TYPE, DISCOVERY_TYPE, 0, 0, &parent);
        parent_ok = 1;
      } else {printf("No parent found :(\n)");}
      etimer_reset(&wait_for_parents);
    } else {
      if (must_respond) {
        if (clock_time() < (must_repond_before - (2 * child_interval))) {
          // have the time
          if (child_has_repond) {
            current_child = (current_child + 1) % number_of_children;
            if (current_child == starting_child) {
              // get answer from all children => respond
              send_pkt(OWN_TYPE, MESSAGE_TYPE, total_count + get_sensor_count(), 0, &parent);
              must_respond = 0;
            } else {
              // Ask the next child for his count
              send_pkt(OWN_TYPE, MESSAGE_TYPE, 0, child_interval, &children[current_child]);
              child_has_repond = 0;
              // wait an interval
              etimer_set(&wait_interval, child_interval);
              PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_interval));
              etimer_reset(&wait_interval);
            }
          } else {
            // Child have not respond yet
            // TODO: avoid dead child
            // wait an interval
            etimer_set(&wait_interval, child_interval);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_interval));
            etimer_reset(&wait_interval);
          }
        } else {
          // Send to parent before it's too late
          send_pkt(OWN_TYPE, MESSAGE_TYPE, total_count + get_sensor_count(), 0, &parent);
          must_respond = 0;
        }
      } else {
        PROCESS_YIELD();
      }
    }

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
