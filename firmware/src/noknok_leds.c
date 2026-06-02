/*
 * noknok LEDs Module — Firmware v1.1
 * MCU:    CH32V203G6U6 @ 144 MHz (HSI x18, ch32fun default)
 * USB:    CDC virtual serial port via fsusb (ch32fun extralibs)
 * LEDs:   8x WS2812b RGB on PA3 via TIM2_CH4 + DMA1_Channel2 (interrupt)
 *
 * USB clock: fsusb sets USBPRE=DIV3 at compile time → 144/3 = 48 MHz OK
 * TIM2 clock: APB1 = HCLK/2 = 72 MHz, timer clock = 2xAPB1 = 144 MHz
 * WS2812b: 1 bit = 180 TIM2 ticks = 1.25 us OK
 *
 * Command protocol (binary over USB serial):
 *   0x00           -> all off
 *   0x01 R G B     -> set all 8 LEDs
 *   0x02 i R G B   -> set LED i (0-7)
 *   0x03 B         -> set global brightness 0-255
 *   0x04 [24 bytes]-> set all 8 LEDs: 8 x (R G B)
 *   0x05           -> explicit show (no-op, all commands auto-show)
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
 * At 144 MHz: 1 bit = 180 ticks = 1.25 us
 *   T1H = 115 ticks -> 0.799 us  (spec 0.65-0.95 us OK)
 *   T0H =  58 ticks -> 0.403 us  (spec 0.25-0.55 us OK)
 *   Reset = 60 zero-CCR periods -> 75 us (spec >50 us OK)
 * ============================================================ */

#define LED_COUNT      8
#define WS_PERIOD      180
#define WS_T1H         115
#define WS_T0H         58
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
    /* AFIO clock required for alternate-function pin config on CH32V20x */
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA;

    /* PA3 = TIM2_CH4 AF push-pull 50 MHz: CFGLR bits [15:12] = 0xB */
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFUL << 12)) | (0xBUL << 12);

    /* TIM2: up-counter, PSC=0, period=WS_PERIOD, CH4 idles at CCR=0 */
    TIM2->PSC     = 0;
    TIM2->ATRLR   = WS_PERIOD - 1;
    TIM2->CH4CVR  = 0;
    /* PWM mode 1 on CH4 (OC4M=110 at CHCTLR2[14:12]), preload disabled */
    TIM2->CHCTLR2 = (TIM2->CHCTLR2 & ~0xFF00) | (0x60 << 8);
    TIM2->CCER   |= TIM_CC4E;
    TIM2->CTLR1  |= TIM_ARPE;
    /* TIM2 Update event -> DMA1_Channel2 request */
    TIM2->DMAINTENR |= TIM_UDE;

    /* DMA1_Channel2: mem->periph, 16-bit, mem-increment, high-priority, TC IRQ */
    DMA1_Channel2->CFGR  = 0;
    DMA1_Channel2->PADDR = (uint32_t)&TIM2->CH4CVR;
    DMA1_Channel2->CFGR  =
        DMA_CFGR1_DIR   |   /* memory -> peripheral */
        DMA_CFGR1_MINC  |   /* memory address increments */
        DMA_CFGR1_PSIZE_0 | /* peripheral 16-bit */
        DMA_CFGR1_MSIZE_0 | /* memory 16-bit */
        DMA_CFGR1_PL_1  |   /* high priority */
        DMA_CFGR1_TCIE;     /* TC interrupt clears ws_busy */

    NVIC_EnableIRQ(DMA1_Channel2_IRQn);

    /* Start TIM2: idles with CCR=0 (PA3 low) until first DMA transfer */
    TIM2->CTLR1 |= TIM_CEN;
}

/* DMA TC ISR - clears ws_busy so the next ws_show() can proceed */
void DMA1_Channel2_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel2_IRQHandler(void) {
    if (DMA1->INTFR & DMA1_IT_TC2) {
        DMA1->INTFCR = DMA1_IT_TC2;
        ws_busy = 0;
        /* TIM2 keeps running at CCR=0 -> PA3 stays low (WS2812b idle) */
    }
}

static void ws_show(uint8_t *grb) {
    while (ws_busy);            /* wait for any in-progress transfer */
    ws_busy = 1;
    ws_build_buf(grb);
    DMA1_Channel2->CFGR  &= ~DMA_CFGR1_EN; /* disable to reconfigure */
    DMA1->INTFCR = DMA1_IT_TC2;             /* clear stale TC flag */
    DMA1_Channel2->CNTR  = WS_BUF_LEN;
    DMA1_Channel2->MADDR = (uint32_t)ws_dma_buf;
    DMA1_Channel2->CFGR |= DMA_CFGR1_EN;   /* start */
    TIM2->SWEVGR = TIM_UG;                  /* force first update -> first DMA kick */
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
        grb[i*3 + 0] = scale(fg[i]);   /* G first - WS2812b wire order */
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
    0x00, 0x00, 0x08          /* 1 stop, no parity, 8 bits */
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
    uint8_t *ep1_buf = USBFSCTX.ENDPOINTS[1];
    memcpy(ep1_buf, buf, len);
    USBFS_SendEndpoint(1, len);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    /* ch32fun SystemInit() already ran (HSI x18 = 144 MHz).
     * fsusb was compiled with FUNCONF_SYSTEM_CORE_CLOCK=144000000
     * and sets USBPRE=DIV3 -> 144/3 = 48 MHz USB clock.
     * Do NOT switch PLL after this point - it breaks USB timing. */

    ws_init();

    /* Boot flash: dim white for 300 ms - confirms firmware is alive */
    set_all(20, 20, 20);
    show();
    Delay_Ms(300);
    set_all(0, 0, 0);
    show();

    USBFSSetup();

    while (1) {
        __WFI();
    }
}
