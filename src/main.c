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

#define HW_PINNUM_LED0      (25)    // Pico onboard LED

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

    uint offset;
    PIO pio_ser_wr = pio0;
    uint sm0 = 0;
    uint32_t data_32b;
    uint32_t lp_cnt = 0;

    stdio_init_all();
    udp_init();


    // Onboard LED tikatika~
    gpio_init(HW_PINNUM_LED0);
    gpio_set_dir(HW_PINNUM_LED0, GPIO_OUT);
    add_repeating_timer_ms(-500, repeating_timer_callback, NULL, &timer);


    // 10BASE-T Serializer PIO init
    // sideset を使う都合上、GPIOピンは連番である必要がある。16,17を使用する。
    offset = pio_add_program(pio_ser_wr, &ser_10base_t_program);
    ser_10base_t_program_init(pio_ser_wr, sm0, offset, 16);


    // 最初にNLPを送って対向機器に10BASE-T半二重であることを認識させる
    for (uint32_t nlp = 0, lp = 0; nlp < 200;) {
        // リンクパルスは16ms毎
        // ループ周期は0.8us
        // 16ms / 0.8us = 32000
        if (++lp == 20000) {
            lp = 0;
            nlp++;
            data_32b = 0x0000000A;  // High(パルス幅100ns)
        } else {
            data_32b = 0x00000000;  // IDLE
        }
        ser_10base_t_tx_10b(pio_ser_wr, sm0, data_32b);
    }


    // パケット送出間隔が伸びる場合はリンクを維持するためにNLPの発行が必要だが、
    // 20ms周期で送る場合はなくても良いみたい。
    while (1) {
        lp_cnt++;
        sprintf(udp_payload, "Hello World!! Raspico 10BASE-T !! lp_cnt:%d", lp_cnt);

        udp_packet_gen_10base(tx_buf_udp, udp_payload);
        for (uint32_t i = 0; i < DEF_UDP_BUF_SIZE+1; i++) {
            ser_10base_t_tx_10b(pio_ser_wr, sm0, tx_buf_udp[i]);
        }

        sleep_ms(20);
    }
}
