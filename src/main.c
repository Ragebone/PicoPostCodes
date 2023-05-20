#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "lpc_bus_sniffer.pio.h"

// Set the system frequency to 33 MHz x 6 to make time for handling LPC frames.
#define SYS_FREQ_IN_KHZ (198 * 1000)

#define LED_STATUS_PIN PICO_DEFAULT_LED_PIN
#define INTERVAL_IM_ALIVE_MS (1000)

void gpio_initialization() {
    // Init the Status LED GPIO pin. (Also sets the pin to low.)
    gpio_init(LED_STATUS_PIN);

    // Set the Status LED GPIO pin as output.
    gpio_set_dir(LED_STATUS_PIN, GPIO_OUT);
}

void flash_led_once(void) {
    // Led ON
    gpio_put(LED_STATUS_PIN, 1);
    sleep_ms(200);
    // Led OFF
    gpio_put(LED_STATUS_PIN, 0);
}

int init_lpc_bus_sniffer(PIO pio) {
    // LAD[0-3] + LCLK + LFRAME which starts from GPIO0
    uint lpc_bus_pin_base = 0;
    uint offset;

    printf("initializing the lpc bus sniffer program\n");

    int sm = pio_claim_unused_sm(pio, true);
    if(sm < 0){
        printf("Error: Cannot claim a free state machine\n");
        return -1;
    }

    if (pio_can_add_program(pio, &lpc_bus_sniffer_program)) {
        offset = pio_add_program(pio, &lpc_bus_sniffer_program);
    } else {
        printf("Error: pio program can not be loaded\n");
        return -2;
    }

    lpc_bus_sniffer_program_init(pio, sm, offset, lpc_bus_pin_base);
    pio_sm_set_enabled(pio, sm, true);

    // To sniff the POST codes
    // Set the filter to I/O write cycle (0x2) and the address to 0x80.
    pio->txf[sm] = 0x08002000;

    printf("program loaded at %d, sm: %d\n", offset, sm);
    return sm;
}

bool repeating_timer_callback(struct repeating_timer *t) {
    // Print the I'm alive sign every 1 sec.
    printf("...\n");
    return true;
}

int main() {
    // Initialize the GPIOs. 
    gpio_initialization();

    // Set the system frequency.
    set_sys_clock_khz(SYS_FREQ_IN_KHZ, true);

    stdio_init_all();
    // Wait for a while. Otherwise, USB CDC doesn't print all printfs.
    sleep_ms(500);
    printf("STARTED\n");

    // Init the PIO
    PIO _pio0 = pio0;
    int sm_lpc_bus_sniffer = init_lpc_bus_sniffer(_pio0);
    if (sm_lpc_bus_sniffer < 0) return -1;

    // Print the  system frequency.
    printf("System clock is %u kHz\n", frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS));

    // Set the repeating timer for I'm alive sign.
#if INTERVAL_IM_ALIVE_MS > 0
    struct repeating_timer timer;
    add_repeating_timer_ms(INTERVAL_IM_ALIVE_MS, repeating_timer_callback, NULL, &timer);
#endif

    // Let the user know initialization was successful and the program is going to start.
    flash_led_once();
    printf("Starting to sample the POST codes.\n");

    while(1) {
        if (!pio_sm_is_rx_fifo_empty(_pio0, sm_lpc_bus_sniffer)) {
            uint32_t rxdata = _pio0->rxf[sm_lpc_bus_sniffer];
            printf("data: %02x\n", rxdata);
        }
    }
}