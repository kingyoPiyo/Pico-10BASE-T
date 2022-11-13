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

static void rx_main(void);

static struct repeating_timer timer;

static PIO pio_serdes = pio0;
static uint sm_tx = 0;
static uint sm_rx = 1;


volatile static bool sfd_det;
volatile static uint32_t shift_val;


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


    // Send packets every about 200ms.
    for (uint32_t i = 0;;i++) {

        if ((i % 10) == 0) {

            // Sending UDP packets
            sprintf(udp_payload, "Hello World!! Raspico 10BASE-T !! lp_cnt:%d", lp_cnt++);
            udp_packet_gen_10base(tx_buf_udp, udp_payload);
            for (uint32_t i = 0; i < DEF_UDP_BUF_SIZE+1; i++) {
                ser_10base_t_tx_10b(pio_serdes, sm_tx, tx_buf_udp[i]);
            }

        } else {
            // Sending NLP Pulse (Pulse width = 100ns)
            ser_10base_t_tx_10b(pio_serdes, sm_tx, 0x0000000A);
        }

        if (sfd_det) {
            sfd_det = false;
            sfd_cnt++;
            printf("SFD[%06d] shift:%d\r\n", sfd_cnt, shift_val);
        }

        sleep_ms(20);
    }
}


// Rx Process
static void __time_critical_func(rx_main)(void) {
    uint32_t rx_buf;
    uint32_t rx_buf_old;
    uint32_t buf_shift;
    
    for (;;) {
        rx_buf = pio_sm_get_blocking(pio_serdes, sm_rx);

        gpio_put(HW_PINNUM_OUT1, 1);

        if ( 0xd5555555 == rx_buf_old                          ) { shift_val =  0; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 31) + (rx_buf_old >>  1) ) { shift_val =  1; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 30) + (rx_buf_old >>  2) ) { shift_val =  2; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 29) + (rx_buf_old >>  3) ) { shift_val =  3; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 28) + (rx_buf_old >>  4) ) { shift_val =  4; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 27) + (rx_buf_old >>  5) ) { shift_val =  5; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 26) + (rx_buf_old >>  6) ) { shift_val =  6; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 25) + (rx_buf_old >>  7) ) { shift_val =  7; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 24) + (rx_buf_old >>  8) ) { shift_val =  8; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 23) + (rx_buf_old >>  9) ) { shift_val =  9; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 22) + (rx_buf_old >> 10) ) { shift_val = 10; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 21) + (rx_buf_old >> 11) ) { shift_val = 11; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 20) + (rx_buf_old >> 12) ) { shift_val = 12; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 19) + (rx_buf_old >> 13) ) { shift_val = 13; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 18) + (rx_buf_old >> 14) ) { shift_val = 14; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 17) + (rx_buf_old >> 15) ) { shift_val = 15; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 16) + (rx_buf_old >> 16) ) { shift_val = 16; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 15) + (rx_buf_old >> 17) ) { shift_val = 17; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 14) + (rx_buf_old >> 18) ) { shift_val = 18; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 13) + (rx_buf_old >> 19) ) { shift_val = 19; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 12) + (rx_buf_old >> 20) ) { shift_val = 20; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 11) + (rx_buf_old >> 21) ) { shift_val = 21; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf << 10) + (rx_buf_old >> 22) ) { shift_val = 22; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  9) + (rx_buf_old >> 23) ) { shift_val = 23; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  8) + (rx_buf_old >> 24) ) { shift_val = 24; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  7) + (rx_buf_old >> 25) ) { shift_val = 25; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  6) + (rx_buf_old >> 26) ) { shift_val = 26; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  5) + (rx_buf_old >> 27) ) { shift_val = 27; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  4) + (rx_buf_old >> 28) ) { shift_val = 28; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  3) + (rx_buf_old >> 29) ) { shift_val = 29; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  2) + (rx_buf_old >> 30) ) { shift_val = 30; sfd_det = true; }
        if ( 0xd5555555 == (rx_buf <<  1) + (rx_buf_old >> 31) ) { shift_val = 31; sfd_det = true; }

        rx_buf_old = rx_buf;

        gpio_put(HW_PINNUM_OUT1, 0);

        //printf("%08x", rx_buf);
    }
}
