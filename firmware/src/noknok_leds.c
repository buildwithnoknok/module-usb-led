/*
 * noknok LEDs Module â€” Firmware v1.4 (USBD / FSDEV)
 * MCU:   CH32V203G6U6
 * CLOCK: boots on HSI, manual switch to HSE(24MHz) x2 = 48 MHz (crystal-accurate).
 * USB:   USBD (FSDEV) controller via extralibs/usbd.c â€” the SAME controller the
 *        WCH bootloader uses (D+ pull-up via EXTEN_USBD_PU_EN). fsusb/USBFS did
 *        NOT assert a host-visible pull-up on this board; USBD does.
 *        48 MHz core -> USBPRE=DIV1 -> 48 MHz USB clock.
 * LEDs:  8x WS2812b on PA3 via TIM2_CH4 + DMA1_Channel2 (polling).
 *        WS2812b @48MHz TIM2: 1 bit = 60 ticks = 1.25 us. T1H=38, T0H=19.
 *
 * Command protocol (binary over USB serial):
 *   0x00 / 0x01 RGB / 0x02 i RGB / 0x03 B / 0x04 [24] / 0x05 / 0xF0(->4E 4E 04)
 */

#include "ch32fun.h"
#include "usbd.h"
#include <string.h>

/* ============================================================
 * Manual clock: HSI boot -> HSE 24 MHz x2 = 48 MHz
 * ============================================================ */
static int clock_to_hse_48(void) {
    RCC->CTLR |= RCC_HSEON;
    volatile uint32_t t = 1500000;
    while (t--) { if (RCC->CTLR & RCC_HSERDY) break; }
    if (!(RCC->CTLR & RCC_HSERDY)) return 0;
    FLASH->ACTLR = (FLASH->ACTLR & ~0x07u) | 0x01u;       /* 1 wait-state @48MHz */
    RCC->CFGR0 = RCC_HPRE_DIV1 | RCC_PPRE1_DIV1 | RCC_PPRE2_DIV1
               | RCC_PLLSRC | RCC_PLLMULL2;
    RCC->CTLR |= RCC_PLLON;
    t = 1500000;
    while (t--) { if (RCC->CTLR & RCC_PLLRDY) break; }
    if (!(RCC->CTLR & RCC_PLLRDY)) return 0;
    RCC->CFGR0 = (RCC->CFGR0 & ~(uint32_t)0x03) | RCC_SW_PLL;
    t = 1500000;
    while (t--) { if ((RCC->CFGR0 & RCC_SWS) == 0x08) break; }
    return ((RCC->CFGR0 & RCC_SWS) == 0x08);
}

/* ============================================================
 * WS2812b driver (polling) @48MHz
 * ============================================================ */
#define LED_COUNT      8
#define WS_PERIOD      60
#define WS_T1H         38
#define WS_T0H         19
#define WS_RESET_BITS  60
#define WS_BUF_LEN     (LED_COUNT * 24 + WS_RESET_BITS)

static uint16_t ws_dma_buf[WS_BUF_LEN] __attribute__((aligned(4)));

static void ws_build_buf(uint8_t *grb) {
    uint16_t *p = ws_dma_buf;
    for (int i = 0; i < LED_COUNT * 3; i++) {
        uint8_t byte = grb[i];
        for (int bit = 7; bit >= 0; bit--)
            *p++ = (byte >> bit) & 1 ? WS_T1H : WS_T0H;
    }
    memset(p, 0, WS_RESET_BITS * sizeof(uint16_t));
}

static void ws_init(void) {
    RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
    RCC->AHBPCENR  |= RCC_AHBPeriph_DMA1;     /* re-enable DMA1 (USBDSetup overwrote AHBPCENR) */
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFUL << 12)) | (0xBUL << 12);
    TIM2->PSC     = 0;
    TIM2->ATRLR   = WS_PERIOD - 1;
    TIM2->CH4CVR  = 0;
    TIM2->CHCTLR2 = (TIM2->CHCTLR2 & ~0xFF00) | (0x60 << 8);
    TIM2->CCER   |= TIM_CC4E;
    TIM2->CTLR1  |= TIM_ARPE;
    TIM2->DMAINTENR |= TIM_UDE;
    DMA1_Channel2->CFGR  = 0;
    DMA1_Channel2->PADDR = (uint32_t)&TIM2->CH4CVR;
    DMA1_Channel2->CFGR  =
        DMA_CFGR1_DIR | DMA_CFGR1_MINC | DMA_CFGR1_PSIZE_0 |
        DMA_CFGR1_MSIZE_0 | DMA_CFGR1_PL_1;
    TIM2->CTLR1 |= TIM_CEN;
}

static void ws_show(uint8_t *grb) {
    ws_build_buf(grb);
    DMA1_Channel2->CFGR  &= ~DMA_CFGR1_EN;
    DMA1->INTFCR = DMA1_IT_TC2;
    DMA1_Channel2->CNTR  = WS_BUF_LEN;
    DMA1_Channel2->MADDR = (uint32_t)ws_dma_buf;
    DMA1_Channel2->CFGR |= DMA_CFGR1_EN;
    TIM2->SWEVGR = TIM_UG;
    while (!(DMA1->INTFR & DMA1_IT_TC2));
    DMA1->INTFCR = DMA1_IT_TC2;
}

/* ============================================================
 * LED state
 * ============================================================ */
static uint8_t fr[LED_COUNT], fg[LED_COUNT], fb[LED_COUNT];
static uint8_t brightness = 255;
static inline uint8_t scale(uint8_t v) { return (uint16_t)v * brightness / 255; }

static void show(void) {
    uint8_t grb[LED_COUNT * 3];
    for (int i = 0; i < LED_COUNT; i++) {
        grb[i*3+0] = scale(fg[i]); grb[i*3+1] = scale(fr[i]); grb[i*3+2] = scale(fb[i]);
    }
    ws_show(grb);
}
static void set_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) { fr[i]=r; fg[i]=g; fb[i]=b; }
}

/* ============================================================
 * Command parser
 * ============================================================ */
typedef enum { PARSE_IDLE, PARSE_WAIT } ParseState;
static ParseState ps = PARSE_IDLE;
static uint8_t cmd_byte=0, cmd_buf[24], cmd_need=0, cmd_got=0;

static void execute(void) {
    switch (cmd_byte) {
        case 0x00: set_all(0,0,0); show(); break;
        case 0x01: set_all(cmd_buf[0],cmd_buf[1],cmd_buf[2]); show(); break;
        case 0x02:
            if (cmd_buf[0] < LED_COUNT) {
                fr[cmd_buf[0]]=cmd_buf[1]; fg[cmd_buf[0]]=cmd_buf[2]; fb[cmd_buf[0]]=cmd_buf[3]; show();
            }
            break;
        case 0x03: brightness = cmd_buf[0]; show(); break;
        case 0x04:
            for (int i=0;i<LED_COUNT;i++){ fr[i]=cmd_buf[i*3+0]; fg[i]=cmd_buf[i*3+1]; fb[i]=cmd_buf[i*3+2]; }
            show(); break;
        case 0x05: show(); break;
        case 0xF0: { static const uint8_t id[3]={0x4E,0x4E,0x04}; USBD_SendEndpoint(3, (uint8_t*)id, 3); break; }
    }
}

static void process_byte(uint8_t b) {
    if (ps == PARSE_IDLE) {
        cmd_byte = b; cmd_got = 0;
        switch (b) {
            case 0x00: cmd_need=0; break;
            case 0x01: cmd_need=3; break;
            case 0x02: cmd_need=4; break;
            case 0x03: cmd_need=1; break;
            case 0x04: cmd_need=24; break;
            case 0x05: cmd_need=0; break;
            case 0xF0: cmd_need=0; break;
            default: return;
        }
        if (cmd_need==0) execute(); else ps=PARSE_WAIT;
    } else {
        cmd_buf[cmd_got++] = b;
        if (cmd_got >= cmd_need) { execute(); ps=PARSE_IDLE; }
    }
}

/* ============================================================
 * USBD callbacks
 * ============================================================ */
/* RX from host (EP2) is delivered here by poll_input(). */
void handle_usbd_input(int numbytes, uint8_t *data) {
    for (int i = 0; i < numbytes; i++) process_byte(data[i]);
}

/* ch32fun's USB-printf backend calls this symbol; route it to USBD. */
int USBFS_SendEndpointNEW(int endp, uint8_t *data, int len, int copy) {
    (void)copy;
    return USBD_SendEndpoint(endp, data, len);
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void) {
    clock_to_hse_48();        /* crystal-accurate 48 MHz first */
    USBDSetup();              /* FSDEV USB device (asserts EXTEN_USBD_PU_EN) */
    ws_init();                /* re-enables DMA1 after USBDSetup AHBPCENR overwrite */

    /* Boot flash: dim white 300 ms */
    set_all(20,20,20); show();
    Delay_Ms(300);
    set_all(0,0,0); show();

    while (1) {
        poll_input();         /* dispatches received bytes to handle_usbd_input */
        Delay_Ms(2);
    }
}
