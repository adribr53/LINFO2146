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

#include <stdio.h> /* For printf() */

#include <stdlib.h>
#include <string.h>
#include <radio.h>
/*---------------------------------------------------------------------------*/
PROCESS(test_serial, "Sensor node");
AUTOSTART_PROCESSES(&test_serial);
/*---------------------------------------------------------------------------*/

#define MAX_PAYLOAD_LENGTH (uint8_t) 42

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

#define OWN_TYPE SENSOR_NODE;
static linkaddr_t parent;
static node_type parent_type;
static linkaddr_t* child_nodes;
radio_value_t  parent_strength;
struct etimer parent_last_update;
packet_t to_send;

nullnet_buf = (uint8_t *) &to_send;
nullnet_len = sizeof(packet_t);

radio_value_t get_strength() {
  return 42;
}

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  if (!linkaddr_cmp(src, &linkaddr_node_addr) && len == sizeof(packet_t)) {
    packet_t pkt;
    memcpy(&pkt, data, sizeof(packet_t));
    LOG_INFO("Received from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
    if (pkt.type == DISCOVERY_TYPE) {
      if (pkt.snd == COORDINATOR_NODE) {
          if (parent_type != COORDINATOR_NODE) {
            // old parent : unkown or sensor
            parent = *src; // TODO: working ?
            // maybe : memcpy(parent, src, sizeof(linkaddr_t));
            parent_type = COORDINATOR_NODE;
            parent_strength = get_strength();
            // TODO: parent_last_update = now()
          } else {
              // compare strengt
              radio_value_t new_strength = get_strength();
              if (new_strength > parent_strength) {
                parent = *src; // TODO: working ?
                parent_strength = new_strength;
                // TODO: parent_last_update = now()
              }
          }
      } else if (pkt.snd == SENSOR_NODE) {
          if (parent_type != COORDINATOR_NODE) {
              if (parent_type != UNDEFINED_NODE) {
                  // already have a parent
                  radio_value_t new_strength = get_strength();
                  if (new_strength > parent_strength) {
                    parent = *src; // TODO: working ?
                    parent_strength = new_strength;
                    // TODO: parent_last_update = now()
                  }
              } else {
                  parent = *src; // TODO: working ?
                  parent_type = SENSOR_NODE;
                  parent_strength = get_strength();
                  // TODO: parent_last_update = now()
              }
          } // else discard
      } // else border or bug
  }
  }
}

void send_discovery() {
  to_send.type    = DISCOVERY_TYPE;
  to_send.snd = OWN_TYPE;
  for (uint8_t i = 0; i < MAX_PAYLOAD_LENGTH; i++) to_send.payload[i] = 0;
  NETSTACK_NETWORK.output(NULL);
}

void send_message(linkaddr_t *dst, uint8_t *msg, uint8_t msg_size) {
  to_send.type = MESSAGE_TYPE;
  to_send.snd = OWN_TYPE;
  uint8_t i;
  for (i = 0; i < MAX_PAYLOAD_LENGTH; i++) to_send.payload[i] = 0;
  for (i = 0; i < msg_size; i++) to_send.payload[i] = msg[i];
  NETSTACK_NETWORK.output(dst);
}

PROCESS_THREAD(test_serial, ev, data) {
  //static struct etimer timer;

  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_set_input_callback(input_callback);

  //etimer_set(&timer, CLOCK_SECOND * 1000);
  printf("Starting process\n");
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL((ev == sensors_event) && (data == &button_sensor));
    send_discovery()
    // TODO: Wait timer 
    // To send discovery & send data to parent
    // memcpy(nullnet_buf, &msg, sizeof(count));
    // nullnet_len = 6;

    // NETSTACK_NETWORK.output(NULL);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
