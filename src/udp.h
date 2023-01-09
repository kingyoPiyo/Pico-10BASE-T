#ifndef __UDP_H__
#define __UDP_H__

#include <stdint.h>

// == DMA settings ==
// If 1, using DMA to copy payload and calculate CRC(FCS). 
// Using DMA increases the processing speed of "udp_packet_gen_10base()".
// Performance example:
//  ----------------------------------------------------
//  Payload size [Byte]   use DMA[us]   not use DMA[us]
//  ----------------------------------------------------
//                   64          13.8              26.2
//                  512          60.4             137.3
//                 1472         160.2             375.4
//  ----------------------------------------------------
#ifndef UDP_DMA_EN
#define UDP_DMA_EN 1
#endif

// Buffer size config
#define DEF_UDP_PAYLOAD_SIZE    (64)                // in Byte(Octet), 1472 Byte max
// Ethernet
#define DEF_ETH_DST_MAC         (0xFFFFFFFFFFFF)    // Destination MAC Address
#define DEF_ETH_SRC_MAC         (0x123456789ABC)    // RasPico MAC Address

// IP Header
#define DEF_IP_ADR_SRC1         (192)               // RasPico IP Address
#define DEF_IP_ADR_SRC2         (168)
#define DEF_IP_ADR_SRC3         (37)
#define DEF_IP_ADR_SRC4         (24)

#define DEF_IP_ADR_DST1         (192)               // Destination IP Address
#define DEF_IP_ADR_DST2         (168)
#define DEF_IP_ADR_DST3         (37)
#define DEF_IP_ADR_DST4         (19)

// UDP Header
#define DEF_UDP_SRC_PORTNUM     (1234)
#define DEF_UDP_DST_PORTNUM     (1234)
#define DEF_UDP_LEN             (DEF_UDP_PAYLOAD_SIZE + 8)

// -------------------
// Preamble     7
// SFD          1
// Ether        14
// IP Header    20
// UDP Header   8
// UDP Payload  x
// FCS          4
// -------------------
//              x + 54
#define DEF_UDP_BUF_SIZE        (DEF_UDP_PAYLOAD_SIZE + 54)


void udp_init(int tx_pin);
void udp_packet_gen_10base(uint32_t *buf, uint8_t *udp_payload);
void udp_send_nlp(void);
void udp_send_packet(uint32_t *buf);

#endif //__UDP_H__
