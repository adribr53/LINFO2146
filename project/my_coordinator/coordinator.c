#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"
#include "sys/process.h"
#include <string.h>
#include <stdio.h> /* For printf() */

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define SEND_INTERVAL (8 * CLOCK_SECOND)
#define PERIOD (5 * CLOCK_SECOND)

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr =  {{ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};
#endif /* MAC_CONF_WITH_TSCH */

//static linkaddr_t dest_addr =  {{ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};

typedef enum {
  SENSOR_NODE = 0,
  COORDINATOR_NODE = 1,
  BORDER_NODE = 2,
  UNDEFINED_NODE = 3
} node_type;

typedef enum {
  DISCOVERY_TYPE = 0,
  MESSAGE_TYPE = 1,
  SYNCHRO_TYPE = 2
} packet_type;

typedef struct packet {
  node_type node : 2;
  packet_type msg : 2;
  unsigned payload : 12;
  clock_time_t clock : 32;
} packet_t;

typedef struct slot_packet {
  node_type node : 2;
  packet_type msg : 2;
  unsigned payload : 12;
  uint32_t duration : 32;
  uint32_t clock : 32;
} slot_packet_t;

unsigned DEAD = 42;
#define BROADCAST NULL

#define OWN_TYPE COORDINATOR_NODE

// static unsigned count = 0;
static linkaddr_t parent;
static clock_time_t parent_last_update;

static unsigned has_parent = 0;

static clock_time_t network_clock = 0;
static clock_time_t clock_at_bc = 0;
static clock_time_t duration;
static clock_time_t child_duration;
static clock_time_t wait_slot;
static clock_time_t must_respond_before;
static packet_t my_pkt;
static unsigned slot;

static unsigned received_clock = 0;

#define MAX_CHILDREN 16
static linkaddr_t children[MAX_CHILDREN]; // TODO resize if necessary
static clock_time_t children_last_update[MAX_CHILDREN];

static uint8_t total_count = 0;
static uint8_t received_values = 0;
static uint8_t number_of_children = 0;
static uint8_t current_child = 0;
static uint8_t starting_child = 0;
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "NullNet broadcast example");
PROCESS(check_parent_process, "Coord check parent");
AUTOSTART_PROCESSES(&nullnet_example_process, &check_parent_process);

void send_pkt(node_type node, packet_type type, unsigned payload, clock_time_t clock_v, linkaddr_t *dest) {
  my_pkt.node = node;
  my_pkt.msg = type;
  my_pkt.payload = payload;
  my_pkt.clock = clock_v;    
  memcpy(nullnet_buf, &my_pkt, sizeof(my_pkt));
  nullnet_len = sizeof(my_pkt);        
  NETSTACK_NETWORK.output(dest); 
}

void dead_parent() {
  printf("Parent DEAD, RIP\n");
  memset(&parent, 0, sizeof(parent));
  has_parent = 0;
  number_of_children = 0;
  total_count = 0;
  received_values = 0;
  received_clock = 0;
  // Broadcast the death
  send_pkt(OWN_TYPE, SYNCHRO_TYPE, DEAD, 0, BROADCAST);
  // Activate main thread
  process_poll(&nullnet_example_process);
}

void dead_child(int child_id) {
  printf("Child is DEAD, RIP\n");
  // Shift all elements from "current_child" to the left
  for (int i = child_id; i < number_of_children; i++){
    children[i]=children[i+1];
  }
  number_of_children--;
}

void check_dead_children() {
  for (int i = 0; i < number_of_children; i++) {
    if (clock_time() > (children_last_update[i] + (10*PERIOD))) {
      dead_child(i);
    }
  }
}

void set_wait_slot_time() {  
  wait_slot = SEND_INTERVAL;
  //slot;
  //network_clock;
  //PERIOD;
  clock_time_t start_clock = slot*duration; // replace by duration
  clock_time_t current_clock =  network_clock % PERIOD;
  wait_slot = start_clock + (PERIOD-current_clock);
  LOG_INFO("COORDINATOR - wait before taking its slot is : %lu\n", (long unsigned) wait_slot);
  
}

int is_parent(const linkaddr_t *addr) {
  return linkaddr_cmp(&parent, addr);
}

int get_child_id(const linkaddr_t *addr) {
  for (int i = 0; i < number_of_children; i++) {
    if (linkaddr_cmp(&children[i], addr)) return i;
  }
  return -1;
}

/*---------------------------------------------------------------------------*/
void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (is_parent(src)) { parent_last_update = clock_time(); }
  int id = get_child_id(src);
  if (id >= 0) {
    children_last_update[id] = clock_time();
  }

  if(len == sizeof(packet_t)) {    
    static packet_t pkt;
    memcpy(&pkt, data, sizeof(packet_t));
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    switch (pkt.msg)
    {
    case DISCOVERY_TYPE:
      //LOG_INFO("Discovery");
      switch (pkt.node) {
        case BORDER_NODE: {
          static linkaddr_t border;
          border.u8[0] = src->u8[0];
          border.u8[1] = src->u8[1];
          duration = pkt.clock;
          if (!linkaddr_cmp(dest, &linkaddr_node_addr)) { // BC
            if (!has_parent) {
              LOG_INFO("COORDINATOR - LEARNS ABOUT BORDER, RESPOND TO IT\n");                
              send_pkt(COORDINATOR_NODE, DISCOVERY_TYPE, 0, 0, &border);
            } else {            
              if (network_clock>0) { 
                clock_time_t clock_at_recomp = clock_time();
                LOG_INFO("COORDINATOR - GIVES ITS CLOCK TO BORDER : %lu\n",  network_clock+clock_at_recomp-clock_at_bc);
                send_pkt(COORDINATOR_NODE, SYNCHRO_TYPE, 0, network_clock+clock_at_recomp-clock_at_bc, &border);
              }
            }
          } 
          break;  
        }                                   
        case SENSOR_NODE: {
          if (has_parent) {
            if(!linkaddr_cmp(dest, &linkaddr_node_addr)) {
              // broadcast
              LOG_INFO("COORDINATOR - RECEIVES BROADCAST FROM SENSOR\n");  
              static linkaddr_t sensor;
              sensor.u8[0] = src->u8[0];
              sensor.u8[1] = src->u8[1];   
              send_pkt(COORDINATOR_NODE, DISCOVERY_TYPE, 0, 0, &sensor);
            } else {
              // unicast
              LOG_INFO("COORDINATOR - A SENSOR JOINED HIM\n");
              children[number_of_children] = *src;
              children_last_update[number_of_children] = clock_time();
              LOG_INFO_LLADDR(children + number_of_children);
              number_of_children++;
            }        
          }
          break;
        }  
        case COORDINATOR_NODE: {
          LOG_INFO("COORDINATOR - RECEIVED SMT From Coordinator\n");      
        }
        default: {
          break;
        }
      }
      break;
    case MESSAGE_TYPE:      
      if ((number_of_children > 0) && get_child_id(src) >= 0) {      
        total_count += pkt.payload;
        received_values++;
        LOG_INFO("COORDINATOR - Received value %u from SENSOR\n", pkt.payload);
      } 
      break;
    default:
      // Discard
      LOG_INFO("Type not recognized");
    }

  }  
  LOG_INFO_("\n");
  if (len == sizeof(slot_packet_t)) {
    memcpy(&parent, src, sizeof(linkaddr_t));    
    parent_last_update = clock_time();
    static slot_packet_t slot_pkt;
    memcpy(&slot_pkt, data, sizeof(slot_packet_t));
    clock_at_bc =  clock_time();   
    network_clock = slot_pkt.clock;        
    duration = slot_pkt.duration;
    slot = slot_pkt.payload;
    LOG_INFO("COORDINATOR - FROM BORDER \n Received Slot : %u, Duration : %lu, Netclock %lu\n", slot, duration, network_clock);
    received_clock = 1;
    if (!has_parent) {
      has_parent = 1;
      process_poll(&nullnet_example_process);
      process_poll(&check_parent_process);
    } else {
      process_poll(&nullnet_example_process);
    }

  }

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(nullnet_example_process, ev, data) {
  static struct etimer periodic_timer;    
  static uint8_t is_in_slot = 0; 
  
  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (void *)&my_pkt;
  nullnet_len = sizeof(my_pkt);
  nullnet_set_input_callback(input_callback);
      
  send_pkt(UNDEFINED_NODE, DISCOVERY_TYPE, 0, 0, &linkaddr_node_addr);
  while(1) {
    if (!received_clock) {      
      PROCESS_YIELD();
    } else {
      if (!is_in_slot) {
        // Not in the slot => WAIT
        set_wait_slot_time();
        etimer_set(&periodic_timer, wait_slot);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
        // In the slot => prepare actions
        is_in_slot = 1;
        must_respond_before = clock_time() + duration;
        child_duration = duration / (number_of_children + 1);
        if (number_of_children > 0) {
            starting_child = current_child;
            LOG_INFO("COORDINATOR - ask first SENSOR %d\n", starting_child);
            LOG_INFO_LLADDR(&(children[current_child]));
            send_pkt(OWN_TYPE, MESSAGE_TYPE, 0, child_duration, &(children[current_child]));
        }
      } else {
        // In the slot
        if (has_parent) {
          if (number_of_children > 0) {
            if (clock_time() < (must_respond_before - ((5*child_duration/4)))) {
              LOG_INFO("COORDINATOR -  ask SENSOR\n");

              current_child = (current_child + 1) % number_of_children;              
              if (received_values ==number_of_children) {
                LOG_INFO("COORDINATOR - All responded => data count at send : %u\n", total_count);
                send_pkt(OWN_TYPE, MESSAGE_TYPE, total_count, 0, &parent);
                received_clock = 0;
                total_count = 0;
                is_in_slot = 0;
                received_values = 0;
              } else {
                if (current_child != starting_child) {                  
                  send_pkt(OWN_TYPE, MESSAGE_TYPE, 0, child_duration, &children[current_child]);
                }
              }
              
            } else {
              printf("COORDINATOR - not enough time remains => respond to border\n");
              send_pkt(OWN_TYPE, MESSAGE_TYPE, total_count, 0, &parent);
              total_count = 0;
              is_in_slot = 0;
              received_clock = 0;
              received_values = 0;
            }
            printf("COORDINATOR - Waiting %lu time\n", child_duration);
            etimer_set(&periodic_timer, child_duration);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

          } else {
            // TODO: send to parent
            printf("COORDINATOR - No child => just send 0\n");
            send_pkt(OWN_TYPE, MESSAGE_TYPE, 0, 0, &parent);
            is_in_slot = 0;
            received_clock = 0;
          }
        }
      }
      etimer_reset(&periodic_timer);
    }
  }

  PROCESS_END();
}


PROCESS_THREAD(check_parent_process, ev, data) {
  PROCESS_BEGIN();
  static struct etimer wait_interval;

  while (1) {
    if (!has_parent) {
      PROCESS_YIELD();
    } else {
      LOG_INFO("COORDINATOR - CHECKING IF BORDER STILL THERE\n");
      if (clock_time() > (parent_last_update + (5*PERIOD))) {
        dead_parent();
      }
      check_dead_children();
      etimer_set(&wait_interval, PERIOD);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_interval));
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


