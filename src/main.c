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
#include <stdio.h>

#define HW_PINNUM_TXD       (16)        // 10BASE-T TX- Pin. The TX+ pin is the number plus one.
#define HW_PINNUM_LED0      (25)        // Pico onboard LED
#define DEF_NLP_INTERVAL_US (16000)     // NLP interval (16ms +/- 8ms)
#define DEF_TX_INTERVAL_US  (200000)    // Dummy Data TX interval


static struct repeating_timer timer;

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

    uint32_t lp_cnt = 0;
    uint32_t time_now = 0;
    uint32_t time_nlp = 0;
    uint32_t time_tx = 0;

    // Setting the Clock frequency divider to a multiple of 20 MHz,
    // allows the PIO divider to operate at integer multiples (Reduce jitter).
    set_sys_clock_khz(120000, true);

    stdio_init_all();
    udp_init(HW_PINNUM_TXD);

    // Onboard LED tikatika~
    gpio_init(HW_PINNUM_LED0);
    gpio_set_dir(HW_PINNUM_LED0, GPIO_OUT);
    add_repeating_timer_ms(-500, repeating_timer_callback, NULL, &timer);


    // Wait for Link up....
    for (uint32_t i = 0; i < 100; i++) {
        udp_send_nlp();     // Sending NLP Pulse
        sleep_ms(16);       // NLP interval = 16ms +/- 8ms
    }


    // Main loop
    // Send packets every about 200ms.
    while (1) {
        time_now = time_us_32();


        // Sending NLP Puls
        if ((time_now - time_nlp) > DEF_NLP_INTERVAL_US) {
            time_nlp = time_now;
            udp_send_nlp();
        }


        // Sending UDP packets
        if ((time_now - time_tx) > DEF_TX_INTERVAL_US) {
            time_tx = time_now;
            sprintf(udp_payload, "Hello World!! Raspico 10BASE-T !! lp_cnt:%d", lp_cnt++);
            udp_packet_gen_10base(tx_buf_udp, udp_payload);
            udp_send_packet(tx_buf_udp);
        }


        /****  Put your code here ****/

    }

}
