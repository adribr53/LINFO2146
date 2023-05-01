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
  uint32_t clock : 32;
} packet_t;

static uint8_t count = 0;

static packet_t my_pkt;

static linkaddr_t children[5]; // TODO resize if necessary
static clock_time_t children_clocks[5];
static unsigned next_index = 0;

static clock_time_t network_clock = 0;
static clock_time_t prev_clock = 0;
static clock_time_t cur_clock = 0;
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

void register_clock(linkaddr_t child, clock_time_t clock_child) {
  for (int i = 0; i<next_index; i++) {
    if (linkaddr_cmp(&child, &children[i])) {
      children_clocks[i] = clock_child;
    }
  }
}

void handle_synchro() {
  long signed avg_delta = 0;
  unsigned cnt_clocks = 1; // count the diff of border with itself
  LOG_INFO("call to handle_synchro gives next_time %u\n", next_index);
  for (int i=0; i<next_index; i++) {
    if (children_clocks[i]>0) {
      unsigned long top = (unsigned long)children_clocks[i];
      LOG_INFO("children clock %lu %lu\n", top, PERIOD);
      avg_delta += network_clock - children_clocks[i];      
      cnt_clocks++;
      children_clocks[i]=0; // reset
    }
  }  
  // include border in the computation
  prev_clock = cur_clock;
  cur_clock = clock_time();
  
  avg_delta = avg_delta / cnt_clocks;
  LOG_INFO("avg delta %ld, cnt clocks %u, cur_clock %lu, prev_clock %lu", avg_delta, cnt_clocks, cur_clock, prev_clock);
  network_clock = network_clock + avg_delta + (cur_clock-prev_clock); 
}

unsigned get_slot() {
  return next_index-1;
}

clock_time_t get_duration() {
  return CLOCK_SECOND;
}

/*---------------------------------------------------------------------------*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
  if(len == sizeof(packet_t)) {    
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    switch (pkt.node)
    {
    case COORDINATOR_NODE:
      switch (pkt.msg)
      {
      case DISCOVERY_TYPE:
        LOG_INFO("A coordinator wants to join");
        children[next_index++] = *src;
        static linkaddr_t coordinator;
        coordinator.u8[0] = src->u8[0];
        coordinator.u8[1] = src->u8[1];
        // todo : send slot
        // todo : adapt slot of other nodes (first, 1 second by slot)
        send_pkt(BORDER_NODE, DISCOVERY_TYPE, get_slot(), get_duration(), &coordinator);
        break;      
      case MESSAGE_TYPE:
        LOG_INFO("A coordinator sent some data");
        count += pkt.payload;
      case SYNCHRO_TYPE:
        LOG_INFO("A coordinator sent some clock");
        register_clock(*src, pkt.clock);
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

  etimer_set(&periodic_timer, 16*CLOCK_SECOND);
  while(1) {
    /* 1) SEND SIGNALING MSG "I AM THE BORDER" */
    
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer)); // take time into account
    LOG_INFO("Border signalling its existence \n");
    LOG_INFO_LLADDR(NULL);
    send_pkt(BORDER_NODE, DISCOVERY_TYPE, 0, 0, NULL);       
    
    etimer_reset(&periodic_timer);

    /* 2) wait for coordinator to respond, then compute the new clock*/
    etimer_set(&periodic_timer, CLOCK_SECOND); 
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));        
    handle_synchro();    
    send_pkt(BORDER_NODE, SYNCHRO_TYPE, 0, network_clock, NULL);
    
    cur_clock = clock_time();

    LOG_INFO("Current time: %lu ticks\n", (unsigned long)network_clock);

    /* 3) SEND DATA TO SERVER */
    printf("%u\n", count); 
    count = 0;
    /* RESET TIMER */
    etimer_reset(&periodic_timer);

    clock_time_t remaining_clock =  PERIOD - (network_clock % PERIOD);
    LOG_INFO("REMAIN time %lu\n", remaining_clock);
    etimer_set(&periodic_timer, 16*CLOCK_SECOND + remaining_clock); // wait an additional time to not go too fast
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/


