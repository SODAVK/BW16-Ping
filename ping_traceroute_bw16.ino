#include <Arduino.h>
#include <WiFi.h>

#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/raw.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip_addr.h"

uint32_t last_reply_ip = 0;
uint8_t last_reply_type = 0;
bool reply_received = false;

extern "C" u8_t icmp_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
  (void)arg;
  (void)pcb;
  if (p != NULL) {
    uint8_t *payload = (uint8_t *)p->payload;
    // this IP (0x45), turn 20bytes
    struct icmp_echo_hdr *iecho;
    if (payload[0] == 0x45) {
      iecho = (struct icmp_echo_hdr *)(payload + 20);
    } else {
      iecho = (struct icmp_echo_hdr *)payload;
    }

    last_reply_type = ICMPH_TYPE(iecho);
    last_reply_ip = ip4_addr_get_u32(ip_2_ip4(addr));
    reply_received = true;
    pbuf_free(p);
  }
  return 1;
}

void send_icmp_request(struct raw_pcb *pcb, ip_addr_t *target, uint16_t seq, uint8_t ttl) {
  struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr), PBUF_RAM);
  if (!p) return;
  struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)p->payload;
  ICMPH_TYPE_SET(iecho, ICMP_ECHO);
  ICMPH_CODE_SET(iecho, 0);
  iecho->chksum = 0;
  iecho->id = 0xBEEF;
  iecho->seqno = seq;
  iecho->chksum = inet_chksum(iecho, sizeof(struct icmp_echo_hdr));
  pcb->ttl = ttl;
  raw_sendto(pcb, p, target);
  pbuf_free(p);
}

void run_ping(String ip_str) {
  ip_addr_t target;
  if (!ipaddr_aton(ip_str.c_str(), &target)) {
    Serial.println("invalid IP");
    return;
  }

  struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
  if (!pcb) return;
  raw_recv(pcb, icmp_recv_cb, NULL);
  raw_bind(pcb, IP_ADDR_ANY);
  Serial.print("pinging ");
  Serial.println(ip_str);
  // er. but 
  for (int i = 0; i < 4; i++) {
    reply_received = false;
    uint32_t start = millis();
    send_icmp_request(pcb, &target, i, 64);
    // probel
    while (millis() - start < 1500) {
      if (reply_received) {
        if (last_reply_type == ICMP_ER) {
          Serial.print("Reply from ");
          Serial.print(ip_str);
          Serial.print(": time=");
          Serial.print(millis() - start);
          Serial.println("ms");
        } else {
          // ignor prom.
          reply_received = false;
          continue;
        }
        break;
      }
      delay(10);
    }
    if (!reply_received) Serial.println("Request timed out.");
    delay(400);
  }
  raw_remove(pcb);
}

void run_traceroute(String ip_str) {
  ip_addr_t target;
  if (!ipaddr_aton(ip_str.c_str(), &target)) {
    Serial.println("Invalid IP!");
    return;
  }

  struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
  if (!pcb) return;
  raw_recv(pcb, icmp_recv_cb, NULL);
  raw_bind(pcb, IP_ADDR_ANY);
  Serial.print("Traceroute to ");
  Serial.println(ip_str);

  int timeout_count = 0;

  for (int ttl = 1; ttl <= 30; ttl++) {
    Serial.print(ttl);
    Serial.print("\t");
    bool final_target_reached = false;
    reply_received = false;
    uint32_t start = millis();
    send_icmp_request(pcb, &target, ttl, ttl);

    while (millis() - start < 2000) {
      if (reply_received) {

        timeout_count = 0;

        ip_addr_t reply_addr;
        ip4_addr_set_u32(ip_2_ip4(&reply_addr), last_reply_ip);
        Serial.print(ip4addr_ntoa(ip_2_ip4(&reply_addr)));
        Serial.print("  ");
        Serial.print(millis() - start);
        Serial.println("ms");
        if (last_reply_type == ICMP_ER || last_reply_ip == ip4_addr_get_u32(ip_2_ip4(&target))) {
          final_target_reached = true;
        }
        break;
      }
      delay(5);
    }
    if (!reply_received) {
      Serial.println("no response");
      timeout_count++;
    }
    if (final_target_reached) {
      Serial.println("trace ok");
      break;
    }
    if (timeout_count >= 3) {
      Serial.println("many timeouts, abort");
      break;
    }
    delay(50);
  }
  raw_remove(pcb);
}

void setup() {
  Serial.begin(115200); //interesting, my bw16 work up to 1500000, 2000000bod dont work
  WiFi.begin("u_ssid", "u_pass");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("\nbw16 pingpong normal");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("ping ")) run_ping(cmd.substring(5));
    else if (cmd.startsWith("trace ")) run_traceroute(cmd.substring(6));
  }
}
