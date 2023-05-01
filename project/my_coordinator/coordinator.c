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
#define PERIOD (18 * CLOCK_SECOND)

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

static unsigned count = 0;
static linkaddr_t parent;
static unsigned has_parent = 0;

static clock_time_t network_clock = 0;
static clock_time_t prev_clock = 0;
static clock_time_t cur_clock = 0;
static clock_time_t duration;
static clock_time_t wait_slot;


static packet_t my_pkt;
static unsigned slot;

static unsigned received_clock = 0;

static linkaddr_t children[16]; // TODO resize if necessary
static unsigned next_index = 0;
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "NullNet broadcast example");
AUTOSTART_PROCESSES(&nullnet_example_process);


void send_pkt(node_type node, packet_type type, unsigned payload, clock_time_t clock_v, linkaddr_t *dest) {
  my_pkt.node = node;
  my_pkt.msg = type;
  my_pkt.payload = payload;
  my_pkt.clock = clock_v;    
  memcpy(nullnet_buf, &my_pkt, sizeof(my_pkt));
  nullnet_len = sizeof(my_pkt);        
  NETSTACK_NETWORK.output(dest); 
}

void set_wait_slot_time() {  
  wait_slot = SEND_INTERVAL;
  //slot;
  //network_clock;
  //PERIOD;
  clock_time_t start_clock = slot*CLOCK_SECOND; // replace by duration
  clock_time_t current_clock =  network_clock % PERIOD;
  /*if (current_clock<PERIOD/2) {
    LOG_INFO("cur clock in begin of period\n");
    wait_slot = (start_clock>current_clock) ? (start_clock-current_clock) : 0;
  } else {
    LOG_INFO("cur clock in end of period\n");
    wait_slot = start_clock + (PERIOD-current_clock);
  }*/
  wait_slot = start_clock + (PERIOD-current_clock);
  LOG_INFO("wait : %lu\n", (long unsigned) wait_slot);
  
}

/*---------------------------------------------------------------------------*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
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
        case BORDER_NODE:
          LOG_INFO("From border");
          static linkaddr_t border;
          border.u8[0] = src->u8[0];
          border.u8[1] = src->u8[1];

          if (!linkaddr_cmp(dest, &linkaddr_node_addr)) { // BC
            if (!has_parent) {
              //LOG_INFO("For the first time");                
              //has_parent = 1;                
              send_pkt(COORDINATOR_NODE, DISCOVERY_TYPE, 0, 0, &border);
            } else {            
            //  LOG_INFO("Not the first time");
              // todo send clock                
              if (network_clock>0) { // node has a say
                prev_clock = cur_clock; // was set last when receiving a clock from border
                cur_clock = clock_time();
                send_pkt(COORDINATOR_NODE, SYNCHRO_TYPE, 0, network_clock+cur_clock-prev_clock, &border);
              }
            }
          } else { // coordinator was given a slot !
              LOG_INFO("received a slot : %u", pkt.payload); // duration : in timestamp, slot number : in payload                              
              memcpy(&parent, src, sizeof(linkaddr_t));
              has_parent = 1;
              slot = pkt.payload;
              duration = pkt.clock;
          }
          break;          
          
                 
        case SENSOR_NODE:
          LOG_INFO("From sensor\n");
          if(!linkaddr_cmp(dest, &linkaddr_node_addr)) {
            // broadcast
            LOG_INFO("Received a BC");  
            static linkaddr_t sensor;
            sensor.u8[0] = src->u8[0];
            sensor.u8[1] = src->u8[1];   
            send_pkt(COORDINATOR_NODE, DISCOVERY_TYPE, 0, 0, &sensor);
          } else {
            // unicast
            LOG_INFO("Received a unicast");
            children[next_index++] = *src;
          }        
        case COORDINATOR_NODE:
          //LOG_INFO("From Coordinator");      
        default:
          break;
        }
      break;
    case MESSAGE_TYPE:
      //LOG_INFO("Message");
      count = count + pkt.payload; // should come from sensor that's 
      break;
    case SYNCHRO_TYPE:
      if (has_parent) {
        cur_clock = clock_time();
        //LOG_INFO("received his clock");
        // todo : clock management
        // static struct etimer synchro_timer;      
        network_clock = pkt.clock;
        //set_wait();
        received_clock = 1;      
        process_poll(&nullnet_example_process);
      }
      break;
    default:
      // Discard
      LOG_INFO("Type not recognized");
    }

  }
  LOG_INFO_("\n");

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(nullnet_example_process, ev, data)
{
  static struct etimer periodic_timer;    
  static unsigned count = 0;  
    
  
  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  /* Initialize NullNet */
  nullnet_buf = (void *)&my_pkt;
  nullnet_len = sizeof(my_pkt);
  nullnet_set_input_callback(input_callback);
      
  send_pkt(UNDEFINED_NODE, DISCOVERY_TYPE, 0, 0, &linkaddr_node_addr);

  while(1) {
    if (!received_clock) {      
      PROCESS_YIELD();
    } else {      
      set_wait_slot_time();
      etimer_set(&periodic_timer, wait_slot);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      if (has_parent) {
        LOG_INFO("After syn, sending data to the border");
        LOG_INFO_("\n");      
        send_pkt(COORDINATOR_NODE, MESSAGE_TYPE, count, 0, &parent);
        count = 0;
      }
      /* RESET TIMER */
      etimer_reset(&periodic_timer);
      received_clock = 0;
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


