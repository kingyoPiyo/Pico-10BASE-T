#ifndef __UDP_H__
#define __UDP_H__

#include <stdint.h>

// Buffer size config
#define DEF_UDP_PAYLOAD_SIZE    (64)

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


void udp_init(void);
void udp_packet_gen_10base(uint32_t *buf, uint8_t *udp_payload);

#endif //__UDP_H__
