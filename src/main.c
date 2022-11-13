/********************************************************
* Title    : Pico-10BASE-T Sample
* Date     : 2022/08/22
* Note     : GP16 TX -
             GP17 TX +
* Design   : kingyo
********************************************************/
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "udp.h"
#include "hardware/pio.h"
#include "ser_10base_t.pio.h"
#include "des_10base_t.pio.h"
#include "pico/multicore.h"

#define HW_PINNUM_LED0      (25)    // Pico onboard LED
#define HW_PINNUM_RXP       (18)    // Ethernet RX+
#define HW_PINNUM_OUT0      (1)     // SMA OUT0 for Debug
#define HW_PINNUM_OUT1      (0)     // SMA OUT0 for Debug

#define DEF_NLP_INTERVAL_US (16000)     // NLP interval = 16ms +/- 8ms
#define DEF_DMY_INTERVAL_US (200000)    // Dummy Data send interval

static void rx_main(void);

static struct repeating_timer timer;

static PIO pio_serdes = pio0;
static uint sm_tx = 0;
static uint sm_rx = 1;

// Debug
volatile static uint32_t gsram[1024];
volatile static uint32_t gsram_num;


// Timer interrupt (L-tika)
static bool repeating_timer_callback(struct repeating_timer *t) {
    static bool led0_state = true;

    gpio_put(HW_PINNUM_LED0, led0_state);
    led0_state = !led0_state;

    return true;
}


int main() {
    uint32_t tx_buf_udp[DEF_UDP_BUF_SIZE+1] = {0};
    uint8_t udp_payload[DEF_UDP_PAYLOAD_SIZE] = {0};

    uint offset;

    uint32_t lp_cnt = 0;
    uint32_t sfd_cnt = 0;
    uint32_t time_now;
    uint32_t time_nlp;
    uint32_t time_send;

    set_sys_clock_khz(240000, true);    // Over clock 240MHz

    stdio_init_all();
    udp_init();


    // Onboard LED tikatika~
    gpio_init(HW_PINNUM_LED0);
    gpio_set_dir(HW_PINNUM_LED0, GPIO_OUT);
    add_repeating_timer_ms(-500, repeating_timer_callback, NULL, &timer);


    // 10BASE-T Serializer PIO init
    // Pin numbers must be sequential. (use pin16, 17)
    offset = pio_add_program(pio_serdes, &ser_10base_t_program);
    ser_10base_t_program_init(pio_serdes, sm_tx, offset, 16);


    // Wait for Link up....
    for (uint32_t i = 0; i < 100; i++) {
        // Sending NLP Pulse (Pulse width = 100ns)
        ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
        // NLP interval = 16ms +/- 8ms
        sleep_ms(20);
    }

    
    // RX Test
    gpio_init(HW_PINNUM_RXP);   gpio_set_dir(HW_PINNUM_RXP, GPIO_IN);       // Ethernet RX+
    gpio_init(HW_PINNUM_OUT0);  gpio_set_dir(HW_PINNUM_OUT0, GPIO_OUT);     // SMA Out for Debug
    gpio_init(HW_PINNUM_OUT1);  gpio_set_dir(HW_PINNUM_OUT1, GPIO_OUT);     // SMA Out for Debug
    offset = pio_add_program(pio_serdes, &des_10base_t_program);
    des_10base_t_program_init(pio_serdes, sm_rx, offset, HW_PINNUM_RXP, HW_PINNUM_OUT0);
    multicore_launch_core1(rx_main);


    while (1) {
        time_now = time_us_32();

        // NLP
        if ((time_now - time_nlp) > DEF_NLP_INTERVAL_US) {
            time_nlp = time_now;

            // Sending NLP Pulse (Pulse width = 100ns)
            ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
        }

        // Test Packet
        if ((time_now - time_send) > DEF_DMY_INTERVAL_US) {
            time_send = time_now;

            // Sending UDP packets
            sprintf(udp_payload, "Hello World!! Raspico 10BASE-T !! lp_cnt:%d", lp_cnt++);
            udp_packet_gen_10base(tx_buf_udp, udp_payload);
            for (uint32_t i = 0; i < DEF_UDP_BUF_SIZE+1; i++) {
                ser_10base_t_tx_10b(pio_serdes, sm_tx, tx_buf_udp[i]);
            }
        }

        // for RX Debug
        if (multicore_fifo_rvalid()) {
            sfd_cnt++;
            printf("\r\nSFD[%06d, %02d] ", sfd_cnt, multicore_fifo_pop_blocking());

            //////////////////////// パケット解析
            // 送信先MAC
            uint64_t eth_dst = ((((uint64_t)gsram[0]) << 16) + (gsram[1] >> 16)) & 0xFFFFFFFFFFFF;
            printf("eth.dst=%04x%08x ", (uint32_t)(eth_dst >> 32), (uint32_t)(eth_dst));

            // 送信元MAC
            uint64_t eth_src = ((((uint64_t)gsram[1]) << 32) + (gsram[2])) & 0xFFFFFFFFFFFF;
            printf("eth.src=%04x%08x ", (uint32_t)(eth_src >> 32), (uint32_t)(eth_src));

            // EtherType
            uint16_t eth_type = gsram[3] >> 16;
            printf("eth.type=%04x ", eth_type);

            // ARP（仮）
            if (eth_type == 0x0806) {
                printf("ARP ");

                // Request
                if ((gsram[5] >> 16) == 0x0001) {
                    // ARP Request
                    printf("Who has %d.%d.%d.%d? ", (gsram[9] >> 8) & 0xff, (gsram[9] >> 0) & 0xff, (gsram[10] >> 24) & 0xff, (gsram[10] >> 16) & 0xff);
                    printf("Tell %d.%d.%d.%d ", (gsram[7] >> 24) & 0xff, (gsram[7] >> 16) & 0xff, (gsram[7] >> 8) & 0xff, (gsram[7] >> 0) & 0xff);
                } else if ((gsram[5] >> 16) == 0x0002) {
                    // ARP Reply
                    printf("%d.%d.%d.%d is at ", (gsram[7] >> 24) & 0xff, (gsram[7] >> 16) & 0xff, (gsram[7] >> 8) & 0xff, (gsram[7] >> 0) & 0xff);
                    printf("%02x:%02x:%02x:%02x:%02x:%02x", (gsram[5] >> 8) & 0xff, (gsram[5] >> 0) & 0xff, (gsram[6] >> 24) & 0xff,  (gsram[6] >> 16) & 0xff,  (gsram[6] >> 8) & 0xff,  (gsram[6] >> 0) & 0xff);
                }

            } else {
                printf("    ");
            }

            // for (int i = 0; i < gsram_num; i++)
            // {
            //     printf("%08x ", gsram[i]);
            // }
        }
    }

}


// Rx Process
static void __time_critical_func(rx_main)(void) {
    uint32_t rx_buf;
    uint32_t rx_buf_old;
    bool sfd_det;
    uint8_t shift_num;
    uint32_t sram_tmp[1024];
    uint32_t index = 0;
    uint32_t tmp;
    
    for (;;) {
        rx_buf = pio_sm_get_blocking(pio_serdes, sm_rx);

        gpio_put(HW_PINNUM_OUT1, 1);

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
            sfd_det = false;
            for (int i = 0; i < index; i++) {
                gsram[i] = sram_tmp[i];
            }
            gsram_num = index;
            index = 0;
            multicore_fifo_push_blocking(shift_num);
        } else {
            // データ格納
            tmp = (rx_buf <<  (32 - shift_num)) + (rx_buf_old >> shift_num);
            sram_tmp[index++] = (tmp << 24) | ((tmp & 0x0000FF00) << 8) | ((tmp & 0x00FF0000) >> 8) | (tmp >> 24);
        }

        rx_buf_old = rx_buf;

        gpio_put(HW_PINNUM_OUT1, 0);

        //printf("%08x", rx_buf);
    }
}
