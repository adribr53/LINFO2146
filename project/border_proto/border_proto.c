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

static uint8_t count = 0;

static packet_t my_pkt;

static linkaddr_t children[5]; // TODO resize if necessary
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
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    packet_t recv_pkt;
    memcpy(&recv_pkt, data, sizeof(recv_pkt));

    switch (recv_pkt.node)
    {
    case COORDINATOR_NODE:
      switch (recv_pkt.msg)
      {
      case DISCOVERY_TYPE:
        LOG_INFO("A coordinator wants to join");
        children[next_index++] = *src;
        static linkaddr_t coordinator;
        coordinator.u8[0] = src->u8[0];
        coordinator.u8[1] = src->u8[1];
        // todo : send slot
        send_pkt(BORDER_NODE, DISCOVERY_TYPE, 0, &coordinator);
        break;      
      case MESSAGE_TYPE:
        LOG_INFO("A coordinator sent some data");
        count += recv_pkt.payload;
      case SYNCHRO_TYPE:
        LOG_INFO("A coordinator sent some clock");
      default:
        break;
      }
      break;    
    default:
      LOG_INFO("Msg from node that ain't coordinator");
      break;
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
  static packet_t my_pkt;
  static unsigned count = 0;  
  
  my_pkt.node = BORDER_NODE;
  my_pkt.msg = DISCOVERY_TYPE;

  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  /* Initialize NullNet */
  nullnet_buf = (void *)&my_pkt;
  nullnet_len = sizeof(my_pkt);
  nullnet_set_input_callback(input_callback);

  etimer_set(&periodic_timer, SEND_INTERVAL);
  while(1) {
    /* SEND SIGNALING MSG "I AM THE BORDER" */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    LOG_INFO("Border signalling its existence \n");
    LOG_INFO_LLADDR(NULL);
        
    memcpy(nullnet_buf, &my_pkt, sizeof(my_pkt));
    nullnet_len = sizeof(my_pkt);

    NETSTACK_NETWORK.output(NULL);

    /* SEND DATA TO SERVER */
    printf("%u\n", count); 
    count = 0;
    /* RESET TIMER */
    etimer_reset(&periodic_timer);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


