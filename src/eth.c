#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "system.h"
#include "eth.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "ser_10base_t.pio.h"
#include "des_10base_t.pio.h"


// UART Debug
#define UART_EBG_EN             (1)             // 有効にするとちょい重たい


// Define
#define DEF_10BASET_FULL_ENABLE                 // Enable 10BASE-T Full Duplex

#define HW_PINNUM_RXP           (17)            // Ethernet RX+
#define HW_PINNUM_LED_G         (13)            // Ethernet LED G
#define HW_PINNUM_LED_Y         (18)            // Ethernet LED Y

#define HW_PINNUM_OUT0          (1)             // SMA OUT0 for Debug
#define HW_PINNUM_OUT1          (0)             // SMA OUT0 for Debug
#define DEF_NFLP_INTERVAL_US    (16000)         // NLP/FLP interval = 16ms +/- 8ms
#define DEF_DMY_INTERVAL_US     (1000000)       // Dummy Data send interval
#define DEF_LINK_TIMEOUT_US     (400000)        // Link down time out

#define DEF_ETHTYPE_IPV4        (0x0800)        // EtherType : IPv4
#define DEF_ETHTYPE_ARP         (0x0806)        // EtherType : ARP
#define DEF_ARPOPC_REQUEST      (0x0001)        // ARP Opcode : Request
#define DEF_ARPOPC_REPLY        (0x0002)        // ARP Opcode : Reply

#define DEF_IP_PROTOCOL_ICMP    (0x01)
#define DEF_IP_PROTOCOL_TCP     (0x06)
#define DEF_IP_PROTOCOL_UDP     (0x11)


// Global
static PIO pio_serdes = pio0;
static uint sm_tx = 0;
static uint sm_rx = 1;
volatile static uint32_t gsram[8][512]; // RX data buffer for Core0 and 1
static uint32_t tx_buf_udp[DEF_UDP_BUF_SIZE+1] = {0};
static uint32_t tx_buf_arp[DEF_ARP_BUF_SIZE+1] = {0};
static uint32_t tx_buf_icmp[DEF_ICMP_BUF_SIZE+1] = {0};

static const uint32_t pico_ip_addr = (DEF_SYS_PICO_IP1 << 24) +
                                     (DEF_SYS_PICO_IP2 << 16) +
                                     (DEF_SYS_PICO_IP3 << 8) +
                                     (DEF_SYS_PICO_IP4 << 0);


// Prototype
static void __time_critical_func(_rx_isr)(void);
static bool _send_nlp(void);
static bool _send_flp(uint16_t data);
static bool _send_udp(void);

// DMA
static uint32_t dma_ch_10base_t;
dma_channel_config dma_conf_10base_t;


void eth_init(void) {
    uint offset;
    
    udp_init();
    arp_init();
    icmp_init();

    // 10BASE-T Serializer PIO init
    // Pin numbers must be sequential. (use pin14, 15)
    offset = pio_add_program(pio_serdes, &ser_10base_t_program);
    ser_10base_t_program_init(pio_serdes, sm_tx, offset, 14);

    // Wait for Link up....
    for (uint32_t i = 0; i < 200;) {
#ifdef DEF_10BASET_FULL_ENABLE
        if (_send_flp(0x8602)) i++;   // 10BASE-T Full, ACK = 1
#else
        if (_send_nlp()) i++;
#endif
    }

    // LED
    gpio_init(HW_PINNUM_LED_G);
    gpio_set_dir(HW_PINNUM_LED_G, GPIO_OUT);
    gpio_init(HW_PINNUM_LED_Y);
    gpio_set_dir(HW_PINNUM_LED_Y, GPIO_OUT);

    gpio_put(HW_PINNUM_LED_G, true);
    gpio_put(HW_PINNUM_LED_Y, true);
    sleep_ms(500);
    gpio_put(HW_PINNUM_LED_G, false);
    gpio_put(HW_PINNUM_LED_Y, false);
    
    // RX
    gpio_init(HW_PINNUM_RXP);   gpio_set_dir(HW_PINNUM_RXP, GPIO_IN);       // Ethernet RX+
    gpio_set_input_hysteresis_enabled(HW_PINNUM_RXP, false);
    gpio_init(HW_PINNUM_OUT0);  gpio_set_dir(HW_PINNUM_OUT0, GPIO_OUT);     // SMA Out for Debug
    gpio_init(HW_PINNUM_OUT1);  gpio_set_dir(HW_PINNUM_OUT1, GPIO_OUT);     // SMA Out for Debug
    offset = pio_add_program(pio_serdes, &des_10base_t_program);
    des_10base_t_program_init(pio_serdes, sm_rx, offset, HW_PINNUM_RXP, HW_PINNUM_OUT0);
    multicore_launch_core1(_rx_isr);

    // DMA
    dma_ch_10base_t = dma_claim_unused_channel(true);
    dma_conf_10base_t = dma_channel_get_default_config(dma_ch_10base_t);
    channel_config_set_dreq(&dma_conf_10base_t, pio_get_dreq(pio_serdes, sm_tx, true));
    channel_config_set_transfer_data_size(&dma_conf_10base_t, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_conf_10base_t, true);
    channel_config_set_write_increment(&dma_conf_10base_t, false);
}


// RUN at Core0
void eth_main(void) {    
    static uint32_t sfd_cnt = 0;
    static uint32_t st_time = 0;

    // Link Pulse
#ifdef DEF_10BASET_FULL_ENABLE
        _send_flp(0x8602);
#else
        _send_nlp();
#endif

    // RX Debug
    if (multicore_fifo_rvalid()) {
        uint32_t pop_data = multicore_fifo_pop_blocking();  // index num
        uint32_t slot = pop_data & 0x7; // 0 ~ 7
        uint32_t size = pop_data >> 3;  // x 32bit
        sfd_cnt++;

#if UART_EBG_EN
        printf("slot:%d, size:%d\r\n", slot, size);
#endif

        // Busy LED Pulse Period = 50ms(min)
        if ((time_us_32() - st_time) > 50000) {
            gpio_put(HW_PINNUM_LED_G, true);
            st_time = time_us_32();
        }

        uint64_t eth_dst = ((((uint64_t)gsram[slot][0]) << 16) + (gsram[slot][1] >> 16)) & 0xFFFFFFFFFFFF;
        uint64_t eth_src = ((((uint64_t)gsram[slot][1]) << 32) + (gsram[slot][2])) & 0xFFFFFFFFFFFF;
        uint16_t eth_type = gsram[slot][3] >> 16;
        uint32_t arp_sender_ip = gsram[slot][7];
        uint32_t arp_target_ip = (gsram[slot][9] << 16) + (gsram[slot][10] >> 16);
        uint16_t arp_opcode = gsram[slot][5] >> 16;

        if (eth_type == DEF_ETHTYPE_ARP) {
            if (arp_opcode == DEF_ARPOPC_REQUEST) {
                if (arp_target_ip == pico_ip_addr) {
                    arp_packet_gen_10base(tx_buf_arp, eth_src, arp_sender_ip);

                    // for (uint32_t i = 0; i < DEF_ARP_BUF_SIZE+1; i++) {
                    //     ser_10base_t_tx_10b(pio_serdes, sm_tx, tx_buf_arp[i]);
                    // }

                    dma_channel_configure (
                        dma_ch_10base_t,        // Channel to be configured
                        &dma_conf_10base_t,                    // The configuration we just created
                        &pio_serdes->txf[0],    // Destination address
                        tx_buf_arp,             // Source address
                        (DEF_ARP_BUF_SIZE+1),   // Number of transfers
                        true                    // Start yet
                    );
                    dma_channel_wait_for_finish_blocking(dma_ch_10base_t);
#if UART_EBG_EN
                    printf("[ARP] Who has %d.%d.%d.%d? ", (arp_target_ip >> 24), (arp_target_ip >> 16) & 0xFF, (arp_target_ip >> 8) & 0xFF, (arp_target_ip & 0xFF));
                    printf("Tell %d.%d.%d.%d \r\n", (arp_sender_ip >> 24), (arp_sender_ip >> 16) & 0xFF, (arp_sender_ip >> 8) & 0xFF, (arp_sender_ip & 0xFF));
#endif
                }
            }
        } else if (eth_type == DEF_ETHTYPE_IPV4) {
            uint16_t ip_len = gsram[slot][4] >> 16;
            uint16_t ip_identification = gsram[slot][4] & 0xFFFF;
            uint8_t ip_ttl = (gsram[slot][5] >> 8) & 0xFF;
            uint8_t ip_protocol = gsram[slot][5] & 0xFF;
            uint32_t ip_src_adr = (gsram[slot][6] << 16) + (gsram[slot][7] >> 16);
            uint32_t ip_dst_adr = (gsram[slot][7] << 16) + (gsram[slot][8] >> 16);

            if ((ip_protocol == DEF_IP_PROTOCOL_ICMP) && (ip_len < 1500)) {
                // ICMP Echo test
                uint32_t icmp_tx_size = icmp_packet_gen_10base(tx_buf_icmp, gsram[slot]);
                
                // for (uint32_t i = 0; i < icmp_tx_size; i++) {
                //     ser_10base_t_tx_10b(pio_serdes, sm_tx, tx_buf_icmp[i]);
                // }

                dma_channel_configure (
                    dma_ch_10base_t,        // Channel to be configured
                    &dma_conf_10base_t,                    // The configuration we just created
                    &pio_serdes->txf[0],    // Destination address
                    tx_buf_icmp,            // Source address
                    icmp_tx_size,           // Number of transfers
                    true                    // Start yet
                );
                dma_channel_wait_for_finish_blocking(dma_ch_10base_t);
#if UART_EBG_EN
                printf("[ICMP] src:%d.%d.%d.%d ", (ip_src_adr >> 24), (ip_src_adr >> 16) & 0xFF, (ip_src_adr >> 8) & 0xFF, (ip_src_adr & 0xFF));
                printf("dst:%d.%d.%d.%d ", (ip_dst_adr >> 24), (ip_dst_adr >> 16) & 0xFF, (ip_dst_adr >> 8) & 0xFF, (ip_dst_adr & 0xFF));
                printf("ipv4_len:%d \r\n", ip_len);
#endif
            }
        }
    }
    else
    {
        // Send dummy udp packet
        _send_udp();
    }

    // Busy LED Pulse width = 25ms
    if ((time_us_32() - st_time) > 25000) {
        gpio_put(HW_PINNUM_LED_G, false);
    }
}


// NLP
bool _send_nlp(void) {
    uint32_t time_now = time_us_32();
    static uint32_t time_nlp = 0;
    bool ret = false;

    // Sending NLP Pulse (Pulse width = 100ns)
    if ((time_now - time_nlp) > DEF_NFLP_INTERVAL_US) {
        time_nlp = time_now;
        ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
        ret = true;
    }

    return ret;
}


// FLP
bool _send_flp(uint16_t data) {
    uint32_t time_now = time_us_32();
    static uint32_t time_flp = 0;
    bool ret = false;

    if ((time_now - time_flp) > DEF_NFLP_INTERVAL_US) {
        time_flp = time_now;
        for (int i = 0; i < 16; i++) {
            // Clock
            ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
            sleep_us(62);
            // Data
            if ((data << i) & 0x8000) {
                ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
            }
            sleep_us(62);
        }
        ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
        ret = true;
    }

    return ret;
}


// UDP Test
bool _send_udp(void) {
    uint32_t time_now = time_us_32();
    uint8_t udp_payload[DEF_UDP_PAYLOAD_SIZE] = {0};
    static uint32_t time_udp = 0;
    static uint32_t udp_cnt = 0;
    bool ret = false;

    if ((time_now - time_udp) > DEF_DMY_INTERVAL_US) {
        time_udp = time_now;
        sprintf(udp_payload, "Hello World!! Raspico 10BASE-T !! lp_cnt:%d", udp_cnt++);
        udp_packet_gen_10base(tx_buf_udp, udp_payload);
        for (uint32_t i = 0; i < DEF_UDP_BUF_SIZE+1; i++) {
            ser_10base_t_tx_10b(pio_serdes, sm_tx, tx_buf_udp[i]);
        }
        ret = true;
        sleep_us(10);   // IFG
    }

    return ret;
}


// Core1
static void __time_critical_func(_rx_isr)(void) {
    uint32_t rx_buf;
    uint32_t rx_buf_old;
    bool sfd_det = false;
    bool frame_busy = false;
    bool link_up = false;
    bool link_up_old = false;
    uint8_t shift_num;
    uint32_t index = 0;
    uint32_t slot = 0;   // 0~7
    uint32_t ldm_timer = time_us_32();
    
    
    while(1) {
        uint rxf_lv = pio_sm_get_rx_fifo_level(pio_serdes, sm_rx);

        if (rxf_lv != 0) {
            gpio_put(HW_PINNUM_OUT1, 1);    // For CPU usage monitor
            rx_buf = pio_sm_get(pio_serdes, sm_rx);
            // Search for SFD pattern
            sfd_det = false;
            if ( 0xd5555555 == rx_buf_old                          ) { shift_num =  0; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 31) + (rx_buf_old >>  1) ) { shift_num =  1; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 30) + (rx_buf_old >>  2) ) { shift_num =  2; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 29) + (rx_buf_old >>  3) ) { shift_num =  3; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 28) + (rx_buf_old >>  4) ) { shift_num =  4; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 27) + (rx_buf_old >>  5) ) { shift_num =  5; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 26) + (rx_buf_old >>  6) ) { shift_num =  6; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 25) + (rx_buf_old >>  7) ) { shift_num =  7; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 24) + (rx_buf_old >>  8) ) { shift_num =  8; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 23) + (rx_buf_old >>  9) ) { shift_num =  9; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 22) + (rx_buf_old >> 10) ) { shift_num = 10; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 21) + (rx_buf_old >> 11) ) { shift_num = 11; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 20) + (rx_buf_old >> 12) ) { shift_num = 12; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 19) + (rx_buf_old >> 13) ) { shift_num = 13; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 18) + (rx_buf_old >> 14) ) { shift_num = 14; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 17) + (rx_buf_old >> 15) ) { shift_num = 15; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 16) + (rx_buf_old >> 16) ) { shift_num = 16; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 15) + (rx_buf_old >> 17) ) { shift_num = 17; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 14) + (rx_buf_old >> 18) ) { shift_num = 18; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 13) + (rx_buf_old >> 19) ) { shift_num = 19; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 12) + (rx_buf_old >> 20) ) { shift_num = 20; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 11) + (rx_buf_old >> 21) ) { shift_num = 21; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf << 10) + (rx_buf_old >> 22) ) { shift_num = 22; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  9) + (rx_buf_old >> 23) ) { shift_num = 23; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  8) + (rx_buf_old >> 24) ) { shift_num = 24; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  7) + (rx_buf_old >> 25) ) { shift_num = 25; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  6) + (rx_buf_old >> 26) ) { shift_num = 26; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  5) + (rx_buf_old >> 27) ) { shift_num = 27; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  4) + (rx_buf_old >> 28) ) { shift_num = 28; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  3) + (rx_buf_old >> 29) ) { shift_num = 29; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  2) + (rx_buf_old >> 30) ) { shift_num = 30; sfd_det = true; }
            if ( 0xd5555555 == (rx_buf <<  1) + (rx_buf_old >> 31) ) { shift_num = 31; sfd_det = true; }
        
            if (sfd_det) {
                index = 0;
                frame_busy = true;
            } else {
                uint32_t tmp = (rx_buf <<  (32 - shift_num)) + (rx_buf_old >> shift_num);
                gsram[slot][index] = (tmp << 24) | ((tmp & 0x0000FF00) << 8) | ((tmp & 0x00FF0000) >> 8) | (tmp >> 24);
                index = (index + 1) & 0x1FF;
            }
            rx_buf_old = rx_buf;

            // Link Status
            link_up = true;
            ldm_timer = time_us_32();

            gpio_put(HW_PINNUM_OUT1, 0);    // For CPU usage monitor
        }

        
        if (frame_busy && ((time_us_32() - ldm_timer) > 6)) {
            frame_busy = false;

            // ねじ込み
            uint32_t tmp = (rx_buf <<  (32 - shift_num)) + (rx_buf_old >> shift_num);
            gsram[slot][index] = (tmp << 24) | ((tmp & 0x0000FF00) << 8) | ((tmp & 0x00FF0000) >> 8) | (tmp >> 24);

            multicore_fifo_push_blocking((index << 3) + slot);  // Notify for Core0
            slot = (slot + 1) & 0x7;
        }

        // Detect Link Down
        if ((time_us_32() - ldm_timer) > DEF_LINK_TIMEOUT_US) {
            link_up = false;
            ldm_timer = time_us_32();
        }

        // Show Link Status
        if (link_up != link_up_old) {
            gpio_put(HW_PINNUM_LED_Y, link_up);
#if UART_EBG_EN
            printf(link_up ? "Link Up\r\n" : "Link Down\r\n");
#endif
            link_up_old = link_up;
        }

    }
}
