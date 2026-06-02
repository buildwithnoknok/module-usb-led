/*
 * noknok LEDs Module — Firmware v1.0
 * MCU:    CH32V203G6U6 @ 72 MHz (24 MHz HSE x PLL x3)
 * USB:    CDC virtual serial port via fsusb (ch32fun)
 * LEDs:   8x WS2812b RGB on PA3 via TIM2_CH4 + DMA1_Channel2
 *
 * Command protocol (binary over USB serial):
 *   0x00           -> all off (auto-show)
 *   0x01 R G B     -> set all 8 LEDs (auto-show)
 *   0x02 i R G B   -> set LED i (0-7) (auto-show)
 *   0x03 B         -> set global brightness 0-255 (auto-show)
 *   0x04 [24 bytes]-> set all 8 LEDs: 8 x (R G B) (auto-show)
 *   0x05           -> explicit show (no-op, all commands already auto-show)
 *   0xF0           -> identity query -> responds [0x4E, 0x4E, 0x04]
 *
 * Flashing:
 *   USB bootloader:  fit BOOT0 jumper -> power-cycle -> WCHISPTool
 *   SWD:             PA13/PA14 via WCH-Link-E + make flash on RPi4
 */

#include "ch32fun.h"
#include "fsusb.h"
#include <string.h>

/* ============================================================
 * WS2812b driver - TIM2_CH4 (PA3) + DMA1_Channel2 (TIM2_UP)
 * At 72 MHz: 1 bit = 1.25 us = 90 timer ticks
 * T1H = 58 ticks -> 0.806 us (spec 0.65-0.95 us OK)
 * T0H = 28 ticks -> 0.389 us (spec 0.25-0.55 us OK)
 * Reset = 60 x 0 periods -> 75 us (spec >50 us OK)
 * ============================================================ */

#define LED_COUNT      8
#define WS_PERIOD      90
#define WS_T1H         58
#define WS_T0H         28
#define WS_RESET_BITS  60
#define WS_BUF_LEN     (LED_COUNT * 24 + WS_RESET_BITS)

static uint16_t ws_dma_buf[WS_BUF_LEN] __attribute__((aligned(4)));
static volatile uint8_t ws_busy = 0;

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
    RCC->AHBPCENR  |= RCC_AHBPeriph_DMA1;
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA;

    /* PA3 = TIM2_CH4 alternate-function push-pull 50 MHz
     * CFGLR bits [15:12] = CNF:MODE = 1011 = 0xB */
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFUL << 12)) | (0xBUL << 12);

    /* TIM2: up-counter, 72 MHz, period = WS_PERIOD */
    TIM2->PSC    = 0;
    TIM2->ATRLR  = WS_PERIOD - 1;
    TIM2->CH4CVR = 0;

    /* PWM mode 1 on CH4: OC4M[2:0] = 110 = TIM_OC4M_2 | TIM_OC4M_1 */
    TIM2->CHCTLR2 = (TIM2->CHCTLR2 & ~TIM_OC4M) | TIM_OC4M_2 | TIM_OC4M_1;
    TIM2->CCER   |= TIM_CC4E;      /* enable CH4 output */
    TIM2->CTLR1  |= TIM_ARPE;      /* auto-reload preload */

    /* Enable Update DMA request (TIM2_UP -> DMA1_Channel2) */
    TIM2->DMAINTENR |= TIM_UDE;

    /* DMA1_Channel2: mem->periph, 16-bit, memory-increment, high priority, TC IRQ */
    DMA1_Channel2->CFGR  = 0;
    DMA1_Channel2->PADDR = (uint32_t)&TIM2->CH4CVR;
    DMA1_Channel2->CFGR  =
        DMA_CFGR1_TCIE  |   /* TC interrupt */
        DMA_CFGR1_DIR   |   /* memory -> peripheral */
        DMA_CFGR1_MINC  |   /* memory address increments */
        DMA_CFGR1_PSIZE_0 | /* peripheral 16-bit */
        DMA_CFGR1_MSIZE_0 | /* memory 16-bit */
        DMA_CFGR1_PL_1;     /* high priority */

    NVIC_EnableIRQ(DMA1_Channel2_IRQn);

    /* Start TIM2: idles with CCR=0 (PA3 low) until first DMA transfer */
    TIM2->CTLR1 |= TIM_CEN;
}

static void ws_show(uint8_t *grb) {
    while (ws_busy);
    ws_busy = 1;
    ws_build_buf(grb);

    DMA1_Channel2->CFGR  &= ~DMA_CFGR1_EN;
    DMA1_Channel2->CNTR   = WS_BUF_LEN;
    DMA1_Channel2->MADDR  = (uint32_t)ws_dma_buf;
    DMA1_Channel2->CFGR  |= DMA_CFGR1_EN;
}

void DMA1_Channel2_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel2_IRQHandler(void) {
    if (DMA1->INTFR & DMA1_IT_TC2) {
        DMA1->INTFCR = DMA1_IT_TC2;
        ws_busy = 0;
    }
}

/* ============================================================
 * LED state
 * ============================================================ */

static uint8_t fr[LED_COUNT], fg[LED_COUNT], fb[LED_COUNT];
static uint8_t brightness = 255;

static inline uint8_t scale(uint8_t v) {
    return (uint16_t)v * brightness / 255;
}

static void show(void) {
    uint8_t grb[LED_COUNT * 3];
    for (int i = 0; i < LED_COUNT; i++) {
        grb[i*3 + 0] = scale(fg[i]);
        grb[i*3 + 1] = scale(fr[i]);
        grb[i*3 + 2] = scale(fb[i]);
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
static ParseState ps       = PARSE_IDLE;
static uint8_t    cmd_byte = 0;
static uint8_t    cmd_buf[24];
static uint8_t    cmd_need = 0;
static uint8_t    cmd_got  = 0;

static void cdc_send(const uint8_t *buf, int len);

static void execute(void) {
    switch (cmd_byte) {
        case 0x00:
            set_all(0,0,0); show(); break;
        case 0x01:
            set_all(cmd_buf[0], cmd_buf[1], cmd_buf[2]); show(); break;
        case 0x02:
            if (cmd_buf[0] < LED_COUNT) {
                fr[cmd_buf[0]]=cmd_buf[1]; fg[cmd_buf[0]]=cmd_buf[2]; fb[cmd_buf[0]]=cmd_buf[3];
                show();
            }
            break;
        case 0x03:
            brightness = cmd_buf[0]; show(); break;
        case 0x04:
            for (int i=0; i<LED_COUNT; i++) {
                fr[i]=cmd_buf[i*3+0]; fg[i]=cmd_buf[i*3+1]; fb[i]=cmd_buf[i*3+2];
            }
            show(); break;
        case 0x05:
            show(); break;
        case 0xF0: {
            static const uint8_t id[3] = {0x4E, 0x4E, 0x04};
            cdc_send(id, 3);
            break;
        }
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
 * USB CDC callbacks (required by fsusb.h)
 * ============================================================ */

static uint8_t cdc_cfg[7] = {
    0x00, 0xC2, 0x01, 0x00,  /* baud 115200 little-endian */
    0x00, 0x00, 0x08         /* 1 stop, no parity, 8 bits */
};

void HandleDataOut(struct _USBState *ctx, int endp, uint8_t *data, int len) {
    if (endp == 0) {
        ctx->USBFS_SetupReqLen = 0;
        if (ctx->USBFS_SetupReqCode == CDC_SET_LINE_CODING)
            memcpy(cdc_cfg, data, 7);
        return;
    }
    if (endp == 2)
        for (int i=0; i<len; i++) process_byte(data[i]);
}

int HandleInRequest(struct _USBState *ctx, int endp, uint8_t *data, int len) {
    (void)ctx; (void)data; (void)len;
    if (endp == 3) return -1;
    return 0;
}

int HandleSetupCustom(struct _USBState *ctx, int setup_code) {
    if (!(ctx->USBFS_SetupReqType & USB_REQ_TYP_CLASS)) return 0;
    switch (setup_code) {
        case CDC_SET_LINE_CODING:
            return ctx->USBFS_SetupReqLen ? (int)ctx->USBFS_SetupReqLen : -1;
        case CDC_GET_LINE_CODING:
            ctx->pCtrlPayloadPtr = cdc_cfg;
            return (int)ctx->USBFS_SetupReqLen;
        case CDC_SET_LINE_CTLSTE: return -1;
        case CDC_SEND_BREAK:      return -1;
        default: return 0;
    }
}

static void cdc_send(const uint8_t *buf, int len) {
    USBFS_SendEndpointNEW(1, (uint8_t*)buf, len, 1);
}

/* ============================================================
 * Clock: switch to HSE (24 MHz crystal) x PLL x3 = 72 MHz
 * USB prescaler /1.5 -> 48 MHz
 * ============================================================ */

static void clock_switch_to_hse72(void) {
    /* 1. Back to HSI (safe intermediate) */
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_HSI;
    while ((RCC->CFGR0 & RCC_SWS) != RCC_SWS_HSI);

    /* 2. Disable PLL */
    RCC->CTLR &= ~RCC_PLLON;
    while (RCC->CTLR & RCC_PLLRDY);

    /* 3. Enable HSE (24 MHz crystal) */
    RCC->CTLR |= RCC_HSEON;
    while (!(RCC->CTLR & RCC_HSERDY));

    /* 4. Flash wait states = 2 for 72 MHz (bits [2:0] of ACTLR) */
    FLASH->ACTLR = (FLASH->ACTLR & ~0x07UL) | 0x02UL;

    /* 5. PLL: HSE (no div) x3 = 72 MHz; USBPRE=0 -> /1.5 = 48 MHz */
    RCC->CFGR0 = (RCC->CFGR0 & ~(RCC_PLLSRC | RCC_PLLXTPRE | RCC_PLLMULL))
               | RCC_PLLSRC_HSE | RCC_PLLXTPRE_HSE | RCC_PLLMULL3;

    /* 6. Enable PLL, wait ready */
    RCC->CTLR |= RCC_PLLON;
    while (!(RCC->CTLR & RCC_PLLRDY));

    /* 7. Switch to PLL */
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;
    while ((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    clock_switch_to_hse72();
    ws_init();
    USBFSSetup();

    /* Brief white flash on boot -> confirms firmware is alive */
    set_all(20, 20, 20);
    show();
    Delay_Ms(300);
    set_all(0, 0, 0);
    show();

    while (1) {
        __WFI();
    }
}
