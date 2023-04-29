/*
 * Copyright (c) 2006, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         A very simple Contiki application showing how Contiki programs look
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"

#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "dev/button-sensor.h"

#include <stdlib.h>
#include <string.h>
#include <radio.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
/*---------------------------------------------------------------------------*/
PROCESS(nullnet_example_process, "Coordinator node");
AUTOSTART_PROCESSES(&nullnet_example_process);
/*---------------------------------------------------------------------------*/

#define MAX_PAYLOAD_LENGTH 42

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
  node_type type;
  node_type snd;
  uint8_t payload[MAX_PAYLOAD_LENGTH];
} packet_t;


#define OWN_TYPE COORDINATOR_NODE

static linkaddr_t parent;
// struct etimer parent_last_update; // TODO: maybe another type
packet_t to_send;

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (!linkaddr_cmp(src, &linkaddr_node_addr) && len == sizeof(packet_t)) {
    packet_t pkt;
    memcpy(&pkt, data, sizeof(packet_t));
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    switch (pkt.type)
    {
    case DISCOVERY_TYPE:
      LOG_INFO("Discovery\n");
      if (pkt.snd == BORDER_NODE) {
        memcpy(&parent, src, sizeof(linkaddr_t));
        // TODO: parent_last_update = now()
      } else {
        if (pkt.snd == SENSOR_NODE) LOG_INFO("From sensor\n");
        if (pkt.snd == COORDINATOR_NODE) LOG_INFO("From coordinator\n");
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
}

PROCESS_THREAD(nullnet_example_process, ev, data) {

  //static struct etimer timer;

  PROCESS_BEGIN();
  SENSORS_ACTIVATE(button_sensor);
  static packet_t discorvery_pkt;
  discorvery_pkt.snd = OWN_TYPE;
  discorvery_pkt.type = DISCOVERY_TYPE;
  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&discorvery_pkt;
  nullnet_len = sizeof(packet_t);
  nullnet_set_input_callback(input_callback);

  //etimer_set(&timer, CLOCK_SECOND * 1000);
  printf("Starting process\n");
  LOG_INFO("Type : Coordinator\n");
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL((ev == sensors_event) && (data == &button_sensor));
    LOG_INFO("Sending discovery broadcast\n");
    nullnet_buf = (uint8_t *)&discorvery_pkt;
    nullnet_len = sizeof(packet_t);
    NETSTACK_NETWORK.output(NULL);
    // TODO : send discovery when actived
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
