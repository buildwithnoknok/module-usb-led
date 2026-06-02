#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define FUNCONF_USE_DEBUGPRINTF    0
#define FUNCONF_SYSTICK_USE_HCLK   1
#define FUNCONF_ENABLE_HPE         1

// Clock: ch32fun default for CH32V20x = HSI x18 = 144 MHz
// USB clock: handled by fsusb (144/3 = 48 MHz via WCH extended prescaler)
// TIM2 on APB1 (HCLK/2 = 72 MHz, but timer clock = 2x APB1 = 144 MHz)
// WS2812b period at 144 MHz: 180 cycles = 1.25 us (see noknok_leds.c)

#endif
