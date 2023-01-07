/********************************************************
* Title    : Pico-10BASE-T Sample
* Date     : 2022/08/22
* Design   : kingyo
********************************************************/
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/irq.h"
#include "eth.h"

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
    // Core voltage
    //vreg_set_voltage(VREG_VOLTAGE_1_10); // default
    //vreg_set_voltage(VREG_VOLTAGE_1_15); // 
    vreg_set_voltage(VREG_VOLTAGE_1_20); // 
    //vreg_set_voltage(VREG_VOLTAGE_1_25); // 
    sleep_ms(10);
    set_sys_clock_khz(240000, true);    // Over clock 240MHz

    stdio_init_all();
    eth_init();

    printf("[BOOT]\r\n");

    // Onboard LED tikatika~
    gpio_init(HW_PINNUM_LED0);
    gpio_set_dir(HW_PINNUM_LED0, GPIO_OUT);
    add_repeating_timer_ms(-500, repeating_timer_callback, NULL, &timer);

    while (1) {
        eth_main();
    }

}
