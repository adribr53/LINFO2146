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

static uint8_t server_count = 0;
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "NullNet broadcast example");
AUTOSTART_PROCESSES(&nullnet_example_process);

/*---------------------------------------------------------------------------*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
  if(len == sizeof(packet_t)) {    
    packet_t recv_pkt;
    memcpy(&recv_pkt, data, sizeof(recv_pkt));
    if (recv_pkt.node==COORDINATOR_NODE) {
        if (recv_pkt.msg==DISCOVERY_TYPE) {
            LOG_INFO("Some coordinator wants to join me !");
        } else {
            server_count++; // simple for now
            LOG_INFO("Some coordinator wants to send me data");
        }
    } else {
        LOG_INFO("Received %u from not coord", recv_pkt.payload);
    }    
  } else {
    LOG_INFO_("data of unadequate size");
  }
  LOG_INFO_("\n");

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(nullnet_example_process, ev, data)
{
  static struct etimer periodic_timer;  
  static packet_t discovery_pkt;
  static unsigned count = 0;  
  
  discovery_pkt.node = BORDER_NODE;
  discovery_pkt.msg = DISCOVERY_TYPE;
  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&discovery_pkt;
  nullnet_len = sizeof(discovery_pkt);
  nullnet_set_input_callback(input_callback);

  etimer_set(&periodic_timer, SEND_INTERVAL);
  while(1) {
    /* SEND SIGNALING MSG "I AM THE BORDER" */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    LOG_INFO("Sending pkt of type %u with payload %u to ", discovery_pkt.node, discovery_pkt.payload);
    LOG_INFO_LLADDR(NULL);
    LOG_INFO_("\n");
        
    memcpy(nullnet_buf, &discovery_pkt, sizeof(discovery_pkt));
    nullnet_len = sizeof(discovery_pkt);

    NETSTACK_NETWORK.output(NULL);
    discovery_pkt.payload++;

    /* SEND DATA TO SERVER */
    printf("%u\n", count); 
    count = count + 1;
    /* RESET TIMER */
    etimer_reset(&periodic_timer);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


