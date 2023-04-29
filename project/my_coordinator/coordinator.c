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
} packet_type;

typedef struct packet {
  node_type node : 2;
  packet_type msg : 1;
  uint8_t payload : 5;
} packet_t;

static linkaddr_t parent;
static unsigned has_parent = 0;
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "NullNet broadcast example");
AUTOSTART_PROCESSES(&nullnet_example_process);

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
      LOG_INFO("Discovery\n");
      if (pkt.node == BORDER_NODE) {
        LOG_INFO("From border");
        memcpy(&parent, src, sizeof(linkaddr_t));
        has_parent = 1;
        // TODO: parent_last_update = now()
      } else {
        if (pkt.node == SENSOR_NODE) LOG_INFO("From sensor\n");
        if (pkt.node == COORDINATOR_NODE) LOG_INFO("From coordinator\n");
        if (pkt.node == UNDEFINED_NODE) LOG_INFO("From undefined\n");
      }
      break;
    case MESSAGE_TYPE:
      // If have parent => fw
      LOG_INFO("Message\n");
      break;
    default:
      // Discard
      break;
    }

  }
  LOG_INFO_("\n");

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(nullnet_example_process, ev, data)
{
  static struct etimer periodic_timer;  
  static packet_t discovery_pkt;
  static unsigned count = 0;  
    
  
  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&discovery_pkt;
  nullnet_len = sizeof(discovery_pkt);
  nullnet_set_input_callback(input_callback);

  etimer_set(&periodic_timer, SEND_INTERVAL);
  
  // sends a dummy pkt to prevent bug
  discovery_pkt.node = UNDEFINED_NODE;
  discovery_pkt.msg = DISCOVERY_TYPE;
  NETSTACK_NETWORK.output(&linkaddr_node_addr);
  
  discovery_pkt.node = COORDINATOR_NODE;
  discovery_pkt.msg = DISCOVERY_TYPE;
  discovery_pkt.payload = count;
  while(1) {
    /* SEND SIGNALING MSG "I AM THE BORDER" */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    if (has_parent) {
      LOG_INFO("Sending pkt of type %u with payload %u to ", discovery_pkt.node, discovery_pkt.payload);
      LOG_INFO_LLADDR(NULL);
      LOG_INFO_("\n");      
      memcpy(nullnet_buf, &discovery_pkt, sizeof(discovery_pkt));
      nullnet_len = sizeof(discovery_pkt);      
      NETSTACK_NETWORK.output(&parent);
      discovery_pkt.payload++;
    }
    /* RESET TIMER */
    etimer_reset(&periodic_timer);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


