#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"
#include <string.h>
#include <stdio.h> /* For printf() */

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define SEND_INTERVAL (8 * CLOCK_SECOND)

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
} packet_t;

static unsigned count = 0;
static linkaddr_t parent;
static unsigned has_parent = 0;

static packet_t my_pkt;

static linkaddr_t children[16]; // TODO resize if necessary
static unsigned next_index = 0;
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "NullNet broadcast example");
AUTOSTART_PROCESSES(&nullnet_example_process);


void send_pkt(node_type node, packet_type type, unsigned payload, linkaddr_t *dest) {
  my_pkt.node = node;
  my_pkt.msg = type;
  my_pkt.payload = payload;
  memcpy(nullnet_buf, &my_pkt, sizeof(my_pkt));
  nullnet_len = sizeof(my_pkt);        
  NETSTACK_NETWORK.output(dest); 
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
      LOG_INFO("Discovery");
      switch (pkt.node) {
        case BORDER_NODE:
          LOG_INFO("From border");
          static linkaddr_t border;
          border.u8[0] = src->u8[0];
          border.u8[1] = src->u8[1];
          switch (pkt.msg)
          {
          case DISCOVERY_TYPE:
            if (!linkaddr_cmp(dest, &linkaddr_node_addr)) { // BC
              if (!has_parent) {
                LOG_INFO("For the first time");
                memcpy(&parent, src, sizeof(linkaddr_t));
                //has_parent = 1;
                send_pkt(COORDINATOR_NODE, DISCOVERY_TYPE, 0, &border);
              } else {            
                LOG_INFO("Not the first time");
                // todo send clock
                send_pkt(COORDINATOR_NODE, SYNCHRO_TYPE, 0, &border);
              }
            } else { // coordinator was given a slot !
                LOG_INFO("received a slot"); // duration : in timestamp, slot number : in payload
                has_parent = 1;
            }
            break;          
          case SYNCHRO_TYPE:
            LOG_INFO("received his clock");
            // todo : clock management
            break;
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
            send_pkt(COORDINATOR_NODE, DISCOVERY_TYPE, 9, &sensor);
          } else {
            // unicast
            LOG_INFO("Received a unicast");
            children[next_index++] = *src;
          }        
        case COORDINATOR_NODE:
          LOG_INFO("From Coordinator");      
        default:
          break;
        }
      break;
    case MESSAGE_TYPE:
      LOG_INFO("Message");
      count = count + pkt.payload; // should come from sensor that's 
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

  etimer_set(&periodic_timer, SEND_INTERVAL);
    
  send_pkt(UNDEFINED_NODE, DISCOVERY_TYPE, 0, &linkaddr_node_addr);

  // my_pkt.node = COORDINATOR_NODE;
  // my_pkt.msg = DISCOVERY_TYPE;
  // my_pkt.payload = count;
  while(1) {
    /* SEND SIGNALING MSG "I AM THE BORDER" */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    if (has_parent) {
      LOG_INFO("Sending data to the border");
      LOG_INFO_("\n");      
      send_pkt(COORDINATOR_NODE, MESSAGE_TYPE, count, &parent);
      count = 0;
    }
    /* RESET TIMER */
    etimer_reset(&periodic_timer);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


