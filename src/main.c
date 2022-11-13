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
static void rx_main() {
    uint32_t rx_buf;
    uint32_t rx_buf_old;
    uint32_t buf_shift;

    for (;;) {
        rx_buf = pio_sm_get_blocking(pio_serdes, sm_rx);

        gpio_put(HW_PINNUM_OUT1, 1);

        // Search SFD
        for (int i = 0; i < 32; i++) {
         buf_shift = (rx_buf << (32 - i)) + (rx_buf_old >> i);
            if (buf_shift == 0xd5555555) {
                shift_val = i;
                sfd_det = true;
                break;
            }
        }
        rx_buf_old = rx_buf;

        gpio_put(HW_PINNUM_OUT1, 0);

        //printf("%08x", rx_buf);
    }
}
