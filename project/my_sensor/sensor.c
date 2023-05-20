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
PROCESS(check_for_parent, "Sensor node check parent");
AUTOSTART_PROCESSES(&nullnet_example_process , &check_for_parent);
/*---------------------------------------------------------------------------*/

#define MAX_PAYLOAD_LENGTH (uint8_t) 42
#define PERIOD (5 * CLOCK_SECOND)
#define DURATION (1 * CLOCK_SECOND)
unsigned DEAD = 42;

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
static clock_time_t parent_last_update;
static clock_time_t children_last_update;
static uint8_t recovery_period = 0;
// static linkaddr_t* child_nodes;
radio_value_t  parent_strength;
static packet_t to_send;

// static unsigned received_clock = 0;

void send_pkt(node_type node, packet_type type, uint8_t payload, clock_time_t clock_v, linkaddr_t *dest) {
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

uint8_t get_sensor_count() {
  uint8_t mask = 0b00000011;
  return rand() & mask;  
}

static uint8_t must_respond = 0;

#define MAX_CHILDREN 16
static linkaddr_t children[MAX_CHILDREN]; // TODO resize if necessary
static uint8_t total_count = 0;
static uint8_t number_of_children = 0;

// Variable for 
uint8_t child_has_repond = 0;
uint8_t current_child = 0;
uint8_t starting_child = 0;
clock_time_t child_interval;
clock_time_t must_repond_before;

void dead_parent() {
  printf("Parent DEAD, RIP\n");
  memset(&parent, 0, sizeof(parent));
  parent_type = UNDEFINED_NODE;
  parent_ok = 0;
  number_of_children = 0;
  total_count = 0;
  must_respond = 0;
  parent_strength = INT_MIN;
  // Broadcast the death
  send_pkt(OWN_TYPE, SYNCHRO_TYPE, DEAD, 0, BROADCAST);
  // Activate main thread
  recovery_period = 1;
  process_poll(&nullnet_example_process);
}

void dead_child() {
  printf("Child %d is DEAD, RIP\n", current_child);
  // Shift all elements from "current_child" to the left
  for (int i = current_child; i < number_of_children; i++){
    children[i]=children[i+1];
  }
  // Shift the last element to the left (DON'T WORK => linkaddr_t != int)
  //if (children[number_of_children-1] != 0x00) {children[number_of_children-1] = 0x00}
  number_of_children--;
}

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (!linkaddr_cmp(src, &linkaddr_node_addr) && len == sizeof(packet_t)) {
    packet_t pkt;
    memcpy(&pkt, data, sizeof(packet_t));
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    if (is_parent(src)) {
      // Update parent last update
      parent_last_update = clock_time();
    } else {
      // Update children last update
      children_last_update = clock_time();
    }
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
              } else {
                  // compare strengt
                  radio_value_t new_strength = get_strength();
                  if (new_strength > parent_strength) {
                    LOG_INFO("New parent : coordinator with higher signal strength :");
                    LOG_INFO_LLADDR(&parent);
                    LOG_INFO("\n");
                    memcpy(&parent, src, sizeof(linkaddr_t));
                    parent_strength = new_strength;
                  }
              }
          } else if (pkt.node == SENSOR_NODE) {
            if (parent_ok) {
              // child discovery
              //printf("New child\n");
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
                      }
                  } else {
                      // LOG_INFO("Discovery + sensor : new parent\n");
                      memcpy(&parent, src, sizeof(linkaddr_t));
                      LOG_INFO("New parent : sensor");
                      LOG_INFO_LLADDR(&parent);
                      LOG_INFO("\n");
                      parent_type = SENSOR_NODE;
                      parent_strength = get_strength();
                  }
              } // else discard
            }
          }
          break;
        case MESSAGE_TYPE:
          LOG_INFO("Message\n");
          if (is_parent(src)) {
            if (number_of_children == 0) {
              //printf("No children => direct response\n");
              uint8_t to_send = get_sensor_count();
              LOG_INFO("sensor count : %u\n",  to_send);
              send_pkt(OWN_TYPE, MESSAGE_TYPE, to_send, 0, &parent);
            } else {
              //printf("Have children => must take care of them, clock => %lu\n", pkt.clock);
              total_count = 0;
              must_respond = 1;
              must_repond_before = clock_time() + pkt.clock;
              child_interval = pkt.clock / (number_of_children + 5);
              // current_child = (current_child + 1) % number_of_children;
              starting_child = current_child;
              process_poll(&nullnet_example_process);
              child_has_repond = 0;
              //printf("Ask to the first child\n");
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
                //printf("Sent a message\n");
              }
          }
          break;
        default:
          //printf("Unsupported packet type %x\n", pkt.msg);
          break;
      }
    } else {
      //printf("Broadcast\n");
      if (pkt.msg == DISCOVERY_TYPE && pkt.node == SENSOR_NODE && parent_ok) {
        // Child node request for parent
        static linkaddr_t to_callback;
        memcpy(&to_callback, src, sizeof(linkaddr_t));
        send_pkt(OWN_TYPE, DISCOVERY_TYPE, 0, 0, &to_callback);
      }
      if (pkt.msg == SYNCHRO_TYPE && is_parent(src) && pkt.payload == DEAD) {
        //printf("My parent is no longer active");
        dead_parent();
      }
      //if (pkt.msg == SYNCHRO_TYPE) printf("Synchro\n");
      //if (is_parent(src)) printf("My parent\n");
      //if (pkt.payload == DEAD) printf("DEAD\n");
      //else printf("Payload %d %d\n", pkt.payload, DEAD);
    }
  }
}

PROCESS_THREAD(nullnet_example_process, ev, data) {
  //static struct etimer timer;

  PROCESS_BEGIN();
  // SENSORS_ACTIVATE(button_sensor);
  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&to_send;
  nullnet_len = sizeof(packet_t);
  nullnet_set_input_callback(input_callback);

  printf("Starting process as Sensor\n");

  static struct etimer wait_for_parents;
  static struct etimer wait_interval;

  while(1) {
    if (!parent_ok) {
      if (recovery_period) {
        printf("Recovery period\n");
        etimer_set(&wait_for_parents, 2*PERIOD);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_for_parents));
        etimer_reset(&wait_for_parents);
        recovery_period = 0;
        printf("End of recovery, searching a new parent\n");
      }
      // No parent
      send_pkt(DISCOVERY_TYPE, OWN_TYPE, 0, 0, BROADCAST);
      etimer_set(&wait_for_parents, 2*PERIOD);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_for_parents));
      // TODO: Do parent process here
      if (parent_type != UNDEFINED_NODE) {
        //printf("Sending ok to the parent\n");
        send_pkt(OWN_TYPE, DISCOVERY_TYPE, 0, 0, &parent);
        parent_ok = 1;
        parent_last_update = clock_time();
        children_last_update = clock_time(); // PAS SÛR DE ÇA
        // Starts check for parent failure
        process_poll(&check_for_parent);
      } else {
        //printf("No parent found :(\n)");
      }
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

PROCESS_THREAD(check_for_parent, ev, data) {
  PROCESS_BEGIN();
  static struct etimer wait_interval;

  while (1) {
    if (!parent_ok) {
      PROCESS_YIELD();
    } else {
      // TODO: check
      if (clock_time() > (parent_last_update + (5*PERIOD))) {
        dead_parent();
      }
      if (clock_time() > (children_last_update + (5*PERIOD))) {
        dead_child();
      }
      etimer_set(&wait_interval, PERIOD);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_interval));
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
 