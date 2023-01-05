#ifndef __ICMP_H__
#define __ICMP_H__

#include <stdint.h>


#define DEF_ICMP_BUF_SIZE        (1530) // 適当

void icmp_init(void);
uint32_t icmp_packet_gen_10base(uint32_t *buf, volatile uint32_t *in_data);

#endif //__ICMP_H__
