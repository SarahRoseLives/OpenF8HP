/*
 * OpenF8HP "hello" image for the BF-F8HP Pro.
 *
 * Runs from 0x08001000 (the KDH bootloader jumps here), sets up HSI 8 MHz,
 * USART1 on PA9, the ST7735 parallel LCD, the keypad matrix and PTT.
 *
 * LCD: D0-D7=PB8-PB15, CS=PB6, RST=PB7, RS/DC=PC12, WR=PF6, BL=PA4.
 * Keypad: rows PB8-PB12, columns PB2-PB5 (shared with LCD D0-D4).
 * PTT: PA10 active low.
 *
 * Prints key codes over the cable and shows HELLO + the last key code.
 */
#include <stdint.h>
#include <stdbool.h>
#include "splash.h"

extern const uint8_t stub_blob[];
extern const uint32_t stub_blob_len;

#define REG(a) (*(volatile uint32_t *)(a))

#define CRM_CTRL   REG(0x40021000u)
#define CRM_CFG    REG(0x40021004u)
#define CRM_APB2EN REG(0x40021018u)

#define GPIOA 0x40010800u
#define GPIOB 0x40010C00u
#define GPIOC 0x40011000u
#define GPIOF 0x40011C00u
#define GPIOD 0x40011400u

#define GPIO_CRL(p)  REG((p) + 0x00u)
#define GPIO_CRH(p)  REG((p) + 0x04u)
#define GPIO_IDR(p)  REG((p) + 0x08u)
#define GPIO_ODR(p)  REG((p) + 0x0Cu)
#define GPIO_BSRR(p) REG((p) + 0x10u)
#define GPIO_BRR(p)  REG((p) + 0x14u)

#define USART1 0x40013800u
#define USART_SR   REG(USART1 + 0x00u)
#define USART_DR   REG(USART1 + 0x04u)
#define USART_BRR  REG(USART1 + 0x08u)
#define USART_CR1  REG(USART1 + 0x0Cu)

#define SR_TXE (1u << 7)
#define SR_RXNE (1u << 5)
#define CR1_RE (1u << 2)
#define CR1_TE (1u << 3)
#define CR1_UE (1u << 13)

#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)

void systick_handler(void);

#define LCD_W 160
#define LCD_H 128

#define COL_BG     0x0000u   /* black */
#define COL_WHITE  0xFFFFu
#define COL_YELLOW 0xFFE0u
#define COL_RED    0xF800u
#define COL_BLUE   0x33DFu   /* #2F7BFF, P25-style rail/accent */
#define COL_GRAY   0x8CB4u   /* dim rules/text */
#define COL_DKGRAY 0x3A09u   /* soft-key bar */
#define COL_IDLE   0x52ACu   /* IDLE state box */
#define COL_NAVY   0x0863u   /* splash background */
#define COL_CYAN   0x575Fu   /* splash cyan */

static uint8_t battery_pct = 86;
static uint16_t battery_raw;
static uint8_t pa_level;
static const uint16_t pa_targets[3] = {0x30D7u, 0x48D7u, 0x64D7u};
static const char *const pa_names[3] = {"LOW", "MID", "HIGH"};

static uint32_t vfo_hz = 446000000u;
static uint8_t freq_entry;
static uint8_t freq_digits[6];
static uint8_t freq_len;
static uint8_t menu_active;
static uint8_t menu_screen;
static uint8_t menu_index;
static uint8_t menu_latch;
static uint8_t menu_lock;
static uint8_t sq_level = 3;
static uint8_t step_index = 2;
static const uint16_t step_hz[5] = {5000, 10000, 12500, 25000, 50000};
static const char *const step_names[5] = {"5.0", "10.0", "12.5", "25.0", "50.0"};
static uint8_t backlight_on = 1;
static uint8_t ctcss_index;
static uint8_t ctcss_edit;

/* 0 = off; indexes follow the radio's standard CTCSS table. */
static const uint16_t ctcss_tenths[51] = {
    0,
    670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
    948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
    1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
    1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
    2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
};

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

void Reset_Handler(void) __attribute__((naked, noreturn));
void Default_Handler(void) __attribute__((noreturn));
void main(void) __attribute__((noreturn));

typedef void (*vec_fn)(void);

struct vector_table {
    const void *sp;
    vec_fn reset;
    vec_fn nmi;
    vec_fn hardfault;
    vec_fn memmanage;
    vec_fn busfault;
    vec_fn usagefault;
    vec_fn reserved0;
    vec_fn reserved1;
    vec_fn reserved2;
    vec_fn reserved3;
    vec_fn svcall;
    vec_fn debugmon;
    vec_fn reserved4;
    vec_fn pendsv;
    vec_fn systick;
};

__attribute__((section(".vectors"), used))
const struct vector_table vectors = {
    &_estack,
    Reset_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    0, 0, 0, 0,
    Default_Handler,
    Default_Handler,
    0,
    Default_Handler,
    systick_handler,
};

__attribute__((naked, noreturn))
void Reset_Handler(void)
{
    __asm volatile(
        "movw r1, #:lower16:_estack\n"
        "movt r1, #:upper16:_estack\n"
        "mov sp, r1\n"
        "cpsid i\n"
        "movw r0, #0xED08\n"
        "movt r0, #0xE000\n"
        "movw r1, #0x1000\n"
        "movt r1, #0x0800\n"
        "str r1, [r0]\n"
        "bl main\n"
        "1: b 1b\n");
}

void Default_Handler(void)
{
    for (;;) {
    }
}

void *memset(void *dst, int c, unsigned n)
{
    uint8_t *d = dst;
    while (n--) {
        *d++ = (uint8_t)c;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

static void delay_ms(uint32_t ms)
{
    while (ms--) {
        for (volatile uint32_t i = 0; i < 2000; i++) {
            __asm volatile("nop");
        }
    }
}

/* ---- clock / pins ---- */
static void clock_hsi_8mhz(void)
{
    CRM_CTRL |= (1u << 0);
    while (!(CRM_CTRL & (1u << 1))) {
    }
    CRM_CFG = 0;
    while ((CRM_CFG & 0xCu) != 0) {
    }
}

static void gpio_mode(uint32_t port, unsigned pin, unsigned nibble)
{
    volatile uint32_t *reg;
    unsigned shift;

    if (pin < 8) {
        reg = &GPIO_CRL(port);
        shift = pin * 4;
    } else {
        reg = &GPIO_CRH(port);
        shift = (pin - 8) * 4;
    }
    *reg = (*reg & ~(0xFu << shift)) | ((nibble & 0xFu) << shift);
}

static void gpio_init(void)
{
    CRM_APB2EN |= (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 14);
    (void)CRM_APB2EN;

    /* PA9 USART1_TX, PA10 input pull-up (PTT), PA4 LCD backlight */
    gpio_mode(GPIOA, 9, 0xB);
    gpio_mode(GPIOA, 10, 0x8);
    gpio_mode(GPIOA, 4, 0x3);
    for (unsigned p = 11; p <= 14; p++) {
        gpio_mode(GPIOA, p, 0x3);       /* RF band-select candidates */
    }
    GPIO_ODR(GPIOA) |= (1u << 10);
    GPIO_BRR(GPIOA) = (1u << 4);        /* backlight off for now */
    GPIO_BRR(GPIOA) = (0xFu << 11);     /* default band bits low */

    /* PB6 CS, PB7 RST, PB8-PB15 LCD data / keypad rows */
    gpio_mode(GPIOB, 6, 0x3);
    gpio_mode(GPIOB, 7, 0x3);
    for (unsigned p = 8; p <= 15; p++) {
        gpio_mode(GPIOB, p, 0x3);
    }
    GPIO_ODR(GPIOB) |= (1u << 6) | (1u << 7) | (0x1Fu << 8);

    /* PA8: vendor early-init sets this high (flash CS / possible mic bias) */
    gpio_mode(GPIOA, 8, 0x1);
    GPIO_BSRR(GPIOA) = (1u << 8);
    /* PB1-PB5 input pull-up (PB2-5 keypad; PB1 vendor pull-up) */
    for (unsigned p = 1; p <= 5; p++) {
        gpio_mode(GPIOB, p, 0x8);
    }
    GPIO_ODR(GPIOB) |= (0x1Fu << 1);
    /* PC0 power key and PC7 vendor pull-up */
    gpio_mode(GPIOC, 0, 0x8);
    gpio_mode(GPIOC, 7, 0x8);
    GPIO_ODR(GPIOC) |= (1u << 0) | (1u << 7);

    /* PF6 LCD WR idle high; PF4/PF5/PF7 are audio-amp candidates */
    gpio_mode(GPIOF, 4, 0x3);
    gpio_mode(GPIOF, 5, 0x3);
    gpio_mode(GPIOF, 6, 0x3);
    gpio_mode(GPIOF, 7, 0x3);
    GPIO_ODR(GPIOF) |= (1u << 6);
    GPIO_BRR(GPIOF) = (1u << 4) | (1u << 5) | (1u << 7);
    gpio_mode(GPIOC, 12, 0x3);
    GPIO_BRR(GPIOC) = (1u << 12);
    for (unsigned p = 1; p <= 3; p++) {
        gpio_mode(GPIOC, p, 0x3);
        GPIO_BRR(GPIOC) = (1u << p);   /* audio candidates */
    }
    for (unsigned p = 6; p <= 11; p++) {
        gpio_mode(GPIOC, p, 0x3);
        GPIO_BRR(GPIOC) = (1u << p);
    }
    for (unsigned p = 13; p <= 15; p++) {
        gpio_mode(GPIOC, p, 0x3);
        GPIO_BRR(GPIOC) = (1u << p);
    }
    gpio_mode(GPIOD, 2, 0x3);
    GPIO_BRR(GPIOD) = (1u << 2);       /* PD2 audio candidate */

    /* FD6818: SCN=PB0, SDA=PC4, SCK=PC5 */
    gpio_mode(GPIOB, 0, 0x3);
    GPIO_BSRR(GPIOB) = (1u << 0);
    gpio_mode(GPIOC, 4, 0x3);
    gpio_mode(GPIOC, 5, 0x3);
    GPIO_BSRR(GPIOC) = (1u << 4);
    GPIO_BRR(GPIOC) = (1u << 5);
}

/* ---- UART ---- */
static void uart_init(void)
{
    USART_BRR = 0x45;
    USART_CR1 = CR1_UE | CR1_TE | CR1_RE;
}

static uint8_t uart_getc(char *c)
{
    if (USART_SR & SR_RXNE) {
        *c = (char)USART_DR;
        return 1;
    }
    return 0;
}

static void uart_putc(char c)
{
    while (!(USART_SR & SR_TXE)) {
    }
    USART_DR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static void uart_hex(uint8_t v)
{
    static const char h[] = "0123456789abcdef";
    uart_putc(h[v >> 4]);
    uart_putc(h[v & 0xF]);
}

static void uart_hex16(uint16_t v)
{
    uart_hex(v >> 8);
    uart_hex(v & 0xFF);
}

static void uart_dec(uint16_t v)
{
    if (v >= 100) uart_putc('0' + (v / 100) % 10);
    if (v >= 10) uart_putc('0' + (v / 10) % 10);
    uart_putc('0' + v % 10);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint8_t uart_getc_timeout(char *c)
{
    for (volatile uint32_t i = 0; i < 2000000u; i++) {
        if (uart_getc(c)) {
            return 1;
        }
    }
    return 0;
}

/* ---- ST7735 8080 bus ---- */
static void lcd_set_data(uint8_t d)
{
    uint32_t odr = GPIO_ODR(GPIOB);
    odr = (odr & ~0xFF00u) | ((uint32_t)d << 8);
    GPIO_ODR(GPIOB) = odr;
}

static void lcd_cmd(uint8_t c)
{
    GPIO_BRR(GPIOB) = (1u << 6);        /* CS low */
    GPIO_BRR(GPIOC) = (1u << 12);       /* RS low = command */
    GPIO_BRR(GPIOF) = (1u << 6);        /* WR low */
    lcd_set_data(c);
    GPIO_BSRR(GPIOF) = (1u << 6);       /* WR high */
    GPIO_BSRR(GPIOB) = (1u << 6);       /* CS high */
}

static void lcd_data(uint8_t d)
{
    GPIO_BRR(GPIOB) = (1u << 6);
    GPIO_BSRR(GPIOC) = (1u << 12);      /* RS high = data */
    GPIO_BRR(GPIOF) = (1u << 6);
    lcd_set_data(d);
    GPIO_BSRR(GPIOF) = (1u << 6);
    GPIO_BSRR(GPIOB) = (1u << 6);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_cmd(0x2A);
    lcd_data(x0 >> 8); lcd_data(x0 & 0xFF);
    lcd_data(x1 >> 8); lcd_data(x1 & 0xFF);
    lcd_cmd(0x2B);
    lcd_data(y0 >> 8); lcd_data(y0 & 0xFF);
    lcd_data(y1 >> 8); lcd_data(y1 & 0xFF);
}


/* 4bpp shadow of the LCD, for serial screen capture. */
static uint8_t screen_fb[LCD_W * LCD_H / 2];

static uint8_t color_index(uint16_t c)
{
    if (c == COL_BG) return 0;
    if (c == COL_WHITE) return 1;
    if (c == COL_BLUE) return 2;
    if (c == COL_GRAY) return 3;
    if (c == COL_RED) return 4;
    if (c == COL_DKGRAY) return 5;
    if (c == COL_IDLE) return 6;
    if (c == COL_NAVY) return 7;
    if (c == COL_CYAN) return 8;
    return 1;
}

static void fb_set_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    uint32_t p = (uint32_t)y * LCD_W + x;
    uint8_t idx = color_index(color);
    uint8_t *b = &screen_fb[p >> 1];
    if (p & 1u) {
        *b = (uint8_t)((*b & 0x0Fu) | (idx << 4));
    } else {
        *b = (uint8_t)((*b & 0xF0u) | idx);
    }
}

static void fb_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint16_t color)
{
    if (!w || !h || x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    for (uint16_t yy = 0; yy < h; yy++) {
        for (uint16_t xx = 0; xx < w; xx++) {
            fb_set_pixel(x + xx, y + yy, color);
        }
    }
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) {
        return;
    }
    fb_fill_rect(x, y, w, h, color);
    lcd_window(x, y, x + w - 1, y + h - 1);
    lcd_cmd(0x2C);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (uint32_t n = (uint32_t)w * h; n; n--) {
        lcd_data(hi);
        lcd_data(lo);
    }
}

static void lcd_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) {
        return;
    }
    fb_set_pixel(x, y, color);
    lcd_window(x, y, x, y);
    lcd_cmd(0x2C);
    lcd_data(color >> 8);
    lcd_data(color & 0xFF);
}

struct glyph {
    char c;
    uint8_t col[5];
};

static const struct glyph font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
};

static const uint8_t *glyph(char c)
{
    for (unsigned i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].c == c) {
            return font[i].col;
        }
    }
    return font[0].col;
}

static void lcd_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *g = glyph(c);
    for (unsigned col = 0; col < 5; col++) {
        for (unsigned row = 0; row < 7; row++) {
            uint16_t color = (g[col] >> row) & 1u ? fg : bg;
            for (unsigned dx = 0; dx < 2; dx++) {
                for (unsigned dy = 0; dy < 2; dy++) {
                    lcd_pixel(x + col * 2 + dx, y + row * 2 + dy, color);
                }
            }
        }
    }
}

static void lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg,
                     uint16_t bg)
{
    while (*s) {
        lcd_char(x, y, *s++, fg, bg);
        x += 12;
    }
}

static void lcd_char1(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *g = glyph(c);
    for (unsigned col = 0; col < 5; col++) {
        for (unsigned row = 0; row < 7; row++) {
            uint16_t color = (g[col] >> row) & 1u ? fg : bg;
            lcd_pixel(x + col, y + row, color);
        }
    }
}

static void lcd_text1(uint16_t x, uint16_t y, const char *s, uint16_t fg,
                      uint16_t bg)
{
    while (*s) {
        lcd_char1(x, y, *s++, fg, bg);
        x += 6;
    }
}

static void lcd_rssi_text(uint16_t x, uint16_t y, uint16_t rssi)
{
    uint16_t dbm = rssi > 160 ? 0 : 160 - rssi;
    char b[10] = {'R', 'S', 'S', 'I', ' ', '-', '0', '0', '0', 0};
    b[6] = (char)('0' + (dbm / 100) % 10);
    b[7] = (char)('0' + (dbm / 10) % 10);
    b[8] = (char)('0' + dbm % 10);
    lcd_text1(x, y, b, COL_WHITE, COL_BG);
}

static void draw_signal(uint16_t x, uint16_t y, uint16_t rssi)
{
    unsigned bars = rssi / 22u;
    if (bars > 5) {
        bars = 5;
    }
    for (unsigned i = 0; i < 5; i++) {
        uint16_t h = 2 + i;
        uint16_t color = i < bars ? COL_WHITE : COL_GRAY;
        lcd_fill_rect(x + i * 3, y + 6 - h, 2, h, color);
    }
}

static void draw_battery(uint16_t x, uint16_t y)
{
    uint16_t fill = (uint16_t)((battery_pct * 11u) / 100u);
    uint16_t color = battery_pct < 20 ? COL_RED : COL_WHITE;
    lcd_fill_rect(x, y, 13, 6, color);
    lcd_fill_rect(x + 1, y + 1, 11, 4, COL_BG);
    if (fill) {
        lcd_fill_rect(x + 1, y + 1, fill, 4, color);
    }
    lcd_fill_rect(x + 13, y + 2, 1, 2, color);
}

static void draw_battery_status(void)
{
    char b[3] = {'0', '0', 0};
    uint8_t pct = battery_pct > 99 ? 99 : battery_pct;
    b[0] = (char)('0' + pct / 10);
    b[1] = (char)('0' + pct % 10);
    lcd_fill_rect(130, 2, 30, 9, COL_BG);
    draw_battery(132, 3);
    lcd_text1(147, 3, b, COL_WHITE, COL_BG);
}

static void lcd_blit_splash(void)
{
    lcd_window(0, 0, LCD_W - 1, LCD_H - 1);
    lcd_cmd(0x2C);
    for (uint32_t i = 0; i < sizeof(splash_pixels); i++) {
        uint8_t b = splash_pixels[i];
        uint16_t c0 = splash_palette[b & 0xFu];
        uint16_t c1 = splash_palette[b >> 4];
        lcd_data((uint8_t)(c0 >> 8));
        lcd_data((uint8_t)c0);
        lcd_data((uint8_t)(c1 >> 8));
        lcd_data((uint8_t)c1);
    }
}

static void draw_splash(bool flashing)
{
    lcd_blit_splash();
    if (flashing) {
        lcd_fill_rect(20, 120, 120, 5, COL_GRAY);
    }
}

static void ctcss_format(char *out, uint8_t index)
{
    if (!index) {
        out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0;
        return;
    }
    uint16_t t = ctcss_tenths[index];
    uint16_t hz = (uint16_t)(t / 10u);
    uint8_t frac = (uint8_t)(t % 10u);
    if (hz >= 100u) {
        out[0] = (char)('0' + (hz / 100u) % 10);
        out[1] = (char)('0' + (hz / 10u) % 10);
        out[2] = (char)('0' + hz % 10u);
        out[3] = '.';
        out[4] = (char)('0' + frac);
        out[5] = 0;
    } else {
        out[0] = (char)('0' + hz / 10u);
        out[1] = (char)('0' + hz % 10u);
        out[2] = '.';
        out[3] = (char)('0' + frac);
        out[4] = 0;
    }
}

static void format_hz(char *out, uint32_t hz)
{
    uint32_t mhz = hz / 1000000u;
    uint32_t frac = (hz / 100u) % 10000u;
    out[0] = (char)('0' + (mhz / 100u) % 10);
    out[1] = (char)('0' + (mhz / 10u) % 10);
    out[2] = (char)('0' + mhz % 10u);
    out[3] = '.';
    out[4] = (char)('0' + (frac / 1000u) % 10);
    out[5] = (char)('0' + (frac / 100u) % 10);
    out[6] = (char)('0' + (frac / 10u) % 10);
    out[7] = (char)('0' + frac % 10u);
    out[8] = 0;
}

static void print_vfo(void)
{
    char f[9];
    format_hz(f, vfo_hz);
    uart_puts("vfo=");
    uart_puts(f);
    uart_puts("\r\n");
}

static void draw_command(bool tx, uint16_t rssi)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    lcd_fill_rect(0, 0, 2, LCD_H, tx ? COL_RED : COL_BLUE); /* left rail */

    lcd_fill_rect(6, 2, 30, 11, tx ? COL_RED : COL_IDLE);
    lcd_text1(9, 4, tx ? "TX" : "IDLE", COL_WHITE, tx ? COL_RED : COL_IDLE);
    lcd_text1(42, 4, "FM ANALOG", COL_BLUE, COL_BG);
    draw_signal(112, 2, rssi);
    draw_battery_status();

    if (freq_entry) {
        lcd_text1(6, 16, "ENTRY ", COL_BLUE, COL_BG);
        for (uint8_t i = 0; i < freq_len; i++) {
            lcd_char1((uint16_t)(42 + i * 6), 16,
                      (char)('0' + freq_digits[i]), COL_WHITE, COL_BG);
        }
    } else {
        char f[9];
        format_hz(f, vfo_hz);
        lcd_text1(6, 16, "VFO A / ", COL_WHITE, COL_BG);
        lcd_text1(54, 16, f, COL_WHITE, COL_BG);
    }
    lcd_fill_rect(6, 25, 148, 1, COL_GRAY);

    lcd_text1(6, 29, tx ? "TRANSMIT" : "FM ANALOG", COL_GRAY, COL_BG);
    lcd_text(6, 38, tx ? "MIC" : "SIMPLEX", COL_WHITE, COL_BG);
    if (tx) {
        lcd_text1(6, 56, "PWR ", COL_WHITE, COL_BG);
        lcd_text1(30, 56, pa_names[pa_level], COL_WHITE, COL_BG);
        lcd_text1(60, 56, " FM", COL_WHITE, COL_BG);
    } else {
        char sq[2] = {(char)('0' + sq_level), 0};
        char ctcss[6];
        ctcss_format(ctcss, ctcss_index);
        lcd_text1(6, 56, "SQL ", COL_WHITE, COL_BG);
        lcd_text1(30, 56, sq, COL_WHITE, COL_BG);
        lcd_text1(48, 56, "CTCSS ", COL_WHITE, COL_BG);
        lcd_text1(84, 56, ctcss, COL_WHITE, COL_BG);
        lcd_text1(132, 56, pa_names[pa_level], COL_WHITE, COL_BG);
    }

    lcd_fill_rect(6, 92, 148, 1, COL_GRAY);
    lcd_text1(6, 96, tx ? "ON AIR" : "READY", COL_WHITE, COL_BG);
    lcd_rssi_text(104, 96, rssi);
    lcd_text1(6, 105,
              freq_entry ? "#=SET *=DEL EXIT=CANCEL"
                         : "CONTROL SQUELCH STEP POWER",
              COL_GRAY, COL_BG);
    char sq[2] = {(char)('0' + sq_level), 0};
    lcd_text1(6, 112, sq, COL_WHITE, COL_BG);
    lcd_text1(48, 112, step_names[step_index], COL_WHITE, COL_BG);
    lcd_text1(142, 112, "H", COL_WHITE, COL_BG);

}

static void draw_vfo_line(void)
{
    char f[9];
    format_hz(f, vfo_hz);
    lcd_fill_rect(6, 16, 100, 9, COL_BG);
    lcd_text1(6, 16, "VFO A / ", COL_WHITE, COL_BG);
    lcd_text1(54, 16, f, COL_WHITE, COL_BG);
    lcd_fill_rect(6, 105, 150, 8, COL_BG);
    lcd_text1(6, 105, "CONTROL SQUELCH STEP POWER", COL_GRAY, COL_BG);
}

static void draw_entry_digits(void)
{
    lcd_fill_rect(42, 16, 36, 8, COL_BG);
    for (uint8_t i = 0; i < freq_len; i++) {
        lcd_char1((uint16_t)(42 + i * 6), 16,
                  (char)('0' + freq_digits[i]), COL_WHITE, COL_BG);
    }
}

static void draw_entry_start(void)
{
    lcd_fill_rect(6, 16, 90, 9, COL_BG);
    lcd_text1(6, 16, "ENTRY ", COL_BLUE, COL_BG);
    draw_entry_digits();
    lcd_fill_rect(6, 105, 150, 8, COL_BG);
    lcd_text1(6, 105, "#=SET *=DEL EXIT=CANCEL", COL_GRAY, COL_BG);
}

static void draw_rssi_only(uint16_t rssi)
{
    lcd_fill_rect(112, 2, 18, 8, COL_BG);
    draw_signal(112, 2, rssi);
    draw_battery_status();
    lcd_fill_rect(104, 96, 55, 8, COL_BG);
    lcd_rssi_text(104, 96, rssi);
}

static void send_screen(void)
{
    /* Header: A5 5A, width, height, bpp. Then RLE pairs: count,value.
       count 0 terminates. */
    uart_putc(0xA5);
    uart_putc(0x5A);
    uart_putc((char)(LCD_W >> 8));
    uart_putc((char)(LCD_W & 0xFF));
    uart_putc((char)(LCD_H >> 8));
    uart_putc((char)(LCD_H & 0xFF));
    uart_putc(4);

    uint8_t last = screen_fb[0];
    uint16_t count = 0;
    for (uint32_t i = 0; i < sizeof(screen_fb); i++) {
        if (screen_fb[i] == last && count < 255) {
            count++;
        } else {
            uart_putc((char)count);
            uart_putc((char)last);
            last = screen_fb[i];
            count = 1;
        }
    }
    uart_putc((char)count);
    uart_putc((char)last);
    uart_putc(0);
    uart_putc(0);
}

static void lcd_init(void)
{
    static const struct {
        uint8_t cmd;
        uint8_t n;
        uint8_t d[16];
    } seq[] = {
        {0xB1, 2, {0x05, 0x3A}},
        {0xB2, 3, {0x05, 0x3A, 0x3A}},
        {0xB3, 6, {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A}},
        {0xB4, 1, {0x03}},
        {0xC0, 3, {0x62, 0x02, 0x04}},
        {0xC1, 1, {0xC0}},
        {0xC2, 2, {0x0D, 0x00}},
        {0xC3, 2, {0x8D, 0x6A}},
        {0xC4, 2, {0x8D, 0xEE}},
        {0xC5, 1, {0x12}},
        {0xE0, 16, {0x03, 0x1B, 0x12, 0x11, 0x3F, 0x3A, 0x32, 0x34,
                    0x2F, 0x2B, 0x30, 0x3A, 0x00, 0x01, 0x02, 0x05}},
        {0xE1, 16, {0x03, 0x1B, 0x12, 0x11, 0x32, 0x2F, 0x2A, 0x2F,
                    0x2E, 0x2C, 0x35, 0x3F, 0x00, 0x00, 0x01, 0x05}},
        {0xFC, 1, {0x8C}},
        {0x36, 1, {0x60}},
        {0x3A, 1, {0x05}},
    };

    GPIO_BSRR(GPIOB) = (1u << 7);       /* RST high */
    delay_ms(1);
    GPIO_BRR(GPIOB) = (1u << 7);        /* RST low */
    delay_ms(1);
    GPIO_BSRR(GPIOB) = (1u << 7);       /* RST high */
    delay_ms(120);

    lcd_cmd(0x11);                      /* sleep out */
    delay_ms(120);

    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        lcd_cmd(seq[i].cmd);
        for (unsigned j = 0; j < seq[i].n; j++) {
            lcd_data(seq[i].d[j]);
        }
    }

    lcd_cmd(0x29);                      /* display on */
    delay_ms(20);
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    GPIO_BSRR(GPIOA) = (1u << 4);       /* backlight on */
}

/* ---- FD6818 3-wire bus ---- */
static void rf_delay(void)
{
    for (volatile unsigned i = 0; i < 30; i++) {
        __asm volatile("nop");
    }
}

static void rf_sda_out(void)
{
    gpio_mode(GPIOC, 4, 0x3);
}

static void rf_sda_in(void)
{
    gpio_mode(GPIOC, 4, 0x8);
    GPIO_ODR(GPIOC) |= (1u << 4);        /* pull-up */
}

static void rf_write_byte(uint8_t v)
{
    for (uint8_t m = 0x80; m; m >>= 1) {
        GPIO_BRR(GPIOC) = (1u << 5);     /* SCK low */
        if (v & m) {
            GPIO_BSRR(GPIOC) = (1u << 4);
        } else {
            GPIO_BRR(GPIOC) = (1u << 4);
        }
        rf_delay();
        GPIO_BSRR(GPIOC) = (1u << 5);    /* SCK high */
        rf_delay();
    }
}

static void rf_write_reg(uint8_t addr, uint16_t val)
{
    GPIO_BRR(GPIOB) = (1u << 0);         /* SCN low */
    rf_delay();
    rf_write_byte(addr & 0x7F);
    rf_write_byte(val >> 8);
    rf_write_byte(val & 0xFF);
    GPIO_BSRR(GPIOB) = (1u << 0);        /* SCN high */
    rf_delay();
    GPIO_BRR(GPIOC) = (1u << 5);         /* SCK low */
}

static uint16_t rf_read_reg(uint8_t addr)
{
    GPIO_BRR(GPIOB) = (1u << 0);
    rf_delay();
    rf_write_byte(addr | 0x80);
    GPIO_BRR(GPIOC) = (1u << 5);
    rf_delay();
    rf_sda_in();
    rf_delay();

    uint16_t v = 0;
    for (unsigned i = 0; i < 16; i++) {
        GPIO_BSRR(GPIOC) = (1u << 5);
        rf_delay();
        v <<= 1;
        if (GPIO_IDR(GPIOC) & (1u << 4)) {
            v |= 1;
        }
        rf_delay();
        GPIO_BRR(GPIOC) = (1u << 5);
        rf_delay();
    }

    rf_sda_out();
    GPIO_BSRR(GPIOB) = (1u << 0);
    rf_delay();
    GPIO_BRR(GPIOC) = (1u << 5);
    return v;
}

static void rf_init(uint16_t id)
{
    static const struct {
        uint8_t r;
        uint16_t v;
    } seq[] = {
        {0x00, 0x8000}, {0x00, 0x0000}, {0x37, 0x1D0F},
        {0x13, 0x03BE}, {0x12, 0x037B}, {0x11, 0x027B}, {0x10, 0x007A},
        {0x14, 0x0019}, {0x49, 0x2A38}, {0x7B, 0x8420}, {0x19, 0x1041},
        {0x2A, 0x4F18}, {0x21, 0x06D8}, {0x5C, 0x5665}, {0x5D, 0x0F00},
        {0x74, 0xFEF0}, {0x44, 0x8D8F}, {0x45, 0x3359}, {0x7D, 0xE952},
        {0x48, 0xB3BF},
        {0x1F, 0x5454}, {0x3E, 0xA037}, {0x77, 0x88EF}, {0x4F, 0x3732},
        {0x26, 0x13A0},
    };
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        rf_write_reg(seq[i].r, seq[i].v);
    }
    if (id == 0x4819 || id == 0x4829) {
        uint16_t v = rf_read_reg(0x40);
        rf_write_reg(0x40, (v & 0xF000) | 0x04E0);
        v = rf_read_reg(0x31);
        rf_write_reg(0x31, v & ~0x0002);
    }
}

static void rf_set_freq(uint32_t hz)
{
    uint32_t word = hz / 10;
    rf_write_reg(0x38, word & 0xFFFF);
    rf_write_reg(0x39, word >> 16);
}

static void rf_gpio(uint8_t pin, uint8_t high)
{
    uint16_t v = rf_read_reg(0x33);
    v &= (uint16_t)~(pin << 8);
    if (high) {
        v |= pin;
    } else {
        v &= (uint16_t)~pin;
    }
    rf_write_reg(0x33, v);
}

static void ctcss_apply(void)
{
    if (!ctcss_index) {
        rf_write_reg(0x51, 0x0000);
        rf_write_reg(0x07, 0x0000);
        rf_write_reg(0x08, 0x0000);
        rf_write_reg(0x52, 0x028F);
        return;
    }
    uint16_t t = ctcss_tenths[ctcss_index];
    uint16_t word = (uint16_t)((uint32_t)t * 206489u / 100000u);
    rf_write_reg(0x51, (uint16_t)(0x9000u | ctcss_index));
    rf_write_reg(0x07, word);
    rf_write_reg(0x07, 0x21CD);
    rf_write_reg(0x52, 0x0292);
}

static void squelch_apply(void)
{
    /* The FD6818 0x78 threshold does not mute this audio path; keep the chip
     * open and mute the speaker amp from RSSI instead. */
    rf_write_reg(0x78, 0);
}

static uint16_t squelch_threshold(void)
{
    /* SQL 0 is fully open. SQL 3 = 34, just above the ~28-30 RSSI noise. */
    return sq_level ? (uint16_t)(22u + sq_level * 4u) : 0;
}

static void squelch_update(uint16_t rssi)
{
    if (sq_level == 0 || rssi >= squelch_threshold()) {
        GPIO_BSRR(GPIOC) = (1u << 14);     /* speaker amp on */
    } else {
        GPIO_BRR(GPIOC) = (1u << 14);      /* speaker amp off / mute */
    }
}

static void backlight_set(uint8_t on)
{
    backlight_on = on;
    if (on) {
        GPIO_BSRR(GPIOA) = (1u << 4);
    } else {
        GPIO_BRR(GPIOA) = (1u << 4);
    }
}

static void rf_rx(uint32_t hz)
{
    if (hz < 300000000u) {
        rf_gpio(0x20, 1);              /* VHF RX front-end: GPIO5 on */
        rf_gpio(0x40, 0);              /* GPIO6 off */
    } else {
        rf_gpio(0x8, 0);
        rf_gpio(0x4, 0);
    }
    rf_write_reg(0x37, 0x1F0F);
    rf_write_reg(0x30, 0x0000);
    rf_write_reg(0x1A, 0x1F80);        /* band word, >=74 MHz */
    rf_write_reg(0x40, 0x3AF0);
    rf_set_freq(hz);
    rf_write_reg(0x43, 0x3028);
    rf_write_reg(0x30, 0xBFF1);
    rf_write_reg(0x47, 0x6140);        /* AF_OUT_BASE | RX audio */
    rf_write_reg(0x48, 0x83FF);        /* max volume for test */
    squelch_apply();
    ctcss_apply();
}

static uint16_t tx_prefix = 0x0004;
static uint16_t tx_afout = 0x6140;
static uint16_t tx_mic = 0xE95F;
static uint16_t tx_dev = 0x00FF;
static uint16_t tx_pa = 0x64D7;
static uint16_t tx_afe = 0x302E;
static uint16_t tx_agc = 0x1041;
static uint16_t tx_2a = 0x4F18;
static uint16_t tx_2c = 0x0000;
static uint16_t tx_2b = 0xC000;
static uint16_t tx_28 = 0x0A40;
static uint16_t tx_uhf = 0x0004;   /* GPIO2 default; V09+ use GPIO4 0x0010 */
static uint16_t tx_48 = 0xBBBF;

static uint16_t rf_rssi(void);

static int key_digit(uint8_t key)
{
    switch (key) {
    case 0x02: return 1;
    case 0x03: return 2;
    case 0x04: return 3;
    case 0x05: return 4;
    case 0x06: return 5;
    case 0x07: return 6;
    case 0x09: return 7;
    case 0x0A: return 8;
    case 0x0B: return 9;
    case 0x0E: return 0;
    default:   return -1;
    }
}

static void vfo_set(uint32_t hz, bool tx)
{
    if (hz < 100000000u || hz > 999999000u) {
        uart_puts("range=100-999\r\n");
        return;
    }
    vfo_hz = hz;
    freq_entry = 0;
    if (!tx) {
        rf_rx(vfo_hz);
    }
    draw_vfo_line();
    print_vfo();
}

static const char *const menu_names[8] = {
    "FREQUENCY", "POWER", "SQUELCH", "CTCSS", "STEP",
    "BACKLIGHT", "ABOUT", "EXIT"
};

static void draw_menu(bool tx)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    lcd_fill_rect(0, 0, 2, LCD_H, COL_BLUE);
    lcd_text(6, 4, "MENU", COL_WHITE, COL_BG);
    lcd_text1(6, 20, "SERVICE SETTINGS", COL_GRAY, COL_BG);
    lcd_fill_rect(6, 30, 148, 1, COL_GRAY);

    for (uint8_t i = 0; i < 8; i++) {
        uint16_t y = (uint16_t)(34 + i * 10);
        if (i == menu_index) {
            lcd_fill_rect(6, (uint16_t)(y - 1), 148, 10, COL_DKGRAY);
        }
        uint16_t fg = i == menu_index ? COL_WHITE : COL_GRAY;
        lcd_text1(10, y, menu_names[i], fg, COL_BG);

        char value[9] = "";
        if (i == 0) {
            format_hz(value, vfo_hz);
        } else if (i == 1) {
            const char *p = pa_names[pa_level];
            for (uint8_t n = 0; p[n] && n < 8; n++) {
                value[n] = p[n];
            }
        } else if (i == 2) {
            value[0] = (char)('0' + sq_level);
        } else if (i == 3) {
            ctcss_format(value, ctcss_index);
        } else if (i == 4) {
            const char *p = step_names[step_index];
            for (uint8_t n = 0; p[n] && n < 8; n++) {
                value[n] = p[n];
            }
        } else if (i == 5) {
            value[0] = 'O';
            value[1] = backlight_on ? 'N' : 'F';
            value[2] = backlight_on ? 0 : 'F';
        } else if (i == 6) {
            value[0] = 'I'; value[1] = 'N'; value[2] = 'F'; value[3] = 'O';
        } else {
            value[0] = 'C'; value[1] = 'L'; value[2] = 'O';
            value[3] = 'S'; value[4] = 'E';
        }
        lcd_text1(104, y, value, fg, COL_BG);
    }

    lcd_fill_rect(6, 112, 148, 1, COL_GRAY);
    lcd_text1(6, 118, "UP/DOWN MENU=OK EXIT=BACK", COL_GRAY, COL_BG);
}

static void draw_about(void)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    lcd_fill_rect(0, 0, 2, LCD_H, COL_BLUE);
    lcd_text(6, 4, "ABOUT", COL_WHITE, COL_BG);

    char f[9];
    format_hz(f, vfo_hz);
    char b[3] = {'0', '0', 0};
    uint8_t pct = battery_pct > 99 ? 99 : battery_pct;
    b[0] = (char)('0' + pct / 10);
    b[1] = (char)('0' + pct % 10);

    lcd_fill_rect(6, 24, 148, 1, COL_GRAY);
    lcd_text1(6, 30, "OPENF8HP", COL_WHITE, COL_BG);
    lcd_text1(6, 44, "CREATED BY", COL_GRAY, COL_BG);
    lcd_text1(6, 54, "SARAH ROSE AD8NT", COL_WHITE, COL_BG);
    lcd_fill_rect(6, 66, 148, 1, COL_GRAY);
    lcd_text1(6, 74, "VFO ", COL_GRAY, COL_BG);
    lcd_text1(30, 74, f, COL_WHITE, COL_BG);
    lcd_text1(6, 84, "PA  ", COL_GRAY, COL_BG);
    lcd_text1(30, 84, pa_names[pa_level], COL_WHITE, COL_BG);
    lcd_text1(6, 94, "BAT ", COL_GRAY, COL_BG);
    lcd_text1(30, 94, b, COL_WHITE, COL_BG);
    lcd_text1(42, 94, "%", COL_GRAY, COL_BG);
    lcd_fill_rect(6, 108, 148, 1, COL_GRAY);
    lcd_text1(6, 116, "MENU/EXIT=BACK", COL_GRAY, COL_BG);
}

static void draw_ctcss(void)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    lcd_fill_rect(0, 0, 2, LCD_H, COL_BLUE);
    lcd_text(6, 4, "CTCSS", COL_WHITE, COL_BG);
    lcd_text1(6, 20, "TONE SELECT", COL_GRAY, COL_BG);
    lcd_fill_rect(6, 30, 148, 1, COL_GRAY);

    char t[6];
    ctcss_format(t, ctcss_edit);
    lcd_text1(6, 48, "CURRENT", COL_GRAY, COL_BG);
    lcd_text(6, 62, t, COL_WHITE, COL_BG);
    lcd_text1(6, 92, "UP/DOWN SELECT", COL_GRAY, COL_BG);
    lcd_fill_rect(6, 108, 148, 1, COL_GRAY);
    lcd_text1(6, 116, "MENU=SET EXIT=BACK", COL_GRAY, COL_BG);
}

static void menu_open(bool tx)
{
    menu_active = 1;
    menu_screen = 0;
    menu_index = 0;
    menu_latch = 1;
    menu_lock = 20;
    draw_menu(tx);
}

static void menu_key(uint8_t key, bool tx)
{
    if (menu_screen == 1) {
        if (key == 0x11 || key == 0x14) {
            menu_screen = 0;
            draw_menu(tx);
        }
        return;
    }
    if (menu_screen == 2) {
        if (key == 0x12) {
            if (ctcss_edit < 50) {
                ctcss_edit++;
            }
            draw_ctcss();
        } else if (key == 0x13) {
            if (ctcss_edit > 0) {
                ctcss_edit--;
            }
            draw_ctcss();
        } else if (key == 0x11) {
            ctcss_index = ctcss_edit;
            ctcss_apply();
            menu_screen = 0;
            draw_menu(tx);
        } else if (key == 0x14) {
            menu_screen = 0;
            draw_menu(tx);
        }
        return;
    }

    if (key == 0x12) {
        if (menu_index > 0) {
            menu_index--;
        }
        draw_menu(tx);
    } else if (key == 0x13) {
        if (menu_index < 7) {
            menu_index++;
        }
        draw_menu(tx);
    } else if (key == 0x11) {
        if (menu_index == 0) {
            menu_active = 0;
            freq_entry = 1;
            freq_len = 0;
            draw_entry_start();
        } else if (menu_index == 1) {
            pa_level = (uint8_t)((pa_level + 1) % 3);
            tx_pa = pa_targets[pa_level];
            draw_menu(tx);
        } else if (menu_index == 2) {
            sq_level = (uint8_t)((sq_level + 1) % 10);
            squelch_update(rf_rssi());
            draw_menu(tx);
        } else if (menu_index == 3) {
            ctcss_edit = ctcss_index;
            menu_screen = 2;
            draw_ctcss();
        } else if (menu_index == 4) {
            step_index = (uint8_t)((step_index + 1) % 5);
            draw_menu(tx);
        } else if (menu_index == 5) {
            backlight_set(backlight_on ? 0 : 1);
            draw_menu(tx);
        } else if (menu_index == 6) {
            menu_screen = 1;
            draw_about();
        } else {
            menu_active = 0;
            draw_command(tx, rf_rssi());
        }
    } else if (key == 0x14) {
        menu_active = 0;
        draw_command(tx, rf_rssi());
    }
}

static void handle_key(uint8_t key, bool tx)
{
    if (key == 0xFF) {
        menu_latch = 0;
        return;
    }
    if (menu_active) {
        if (menu_latch || menu_lock) {
            return;
        }
        menu_key(key, tx);
        return;
    }

    int d = key_digit(key);
    if (freq_entry) {
        if (d >= 0) {
            if (freq_len < 6) {
                freq_digits[freq_len++] = (uint8_t)d;
            }
            draw_entry_digits();
            return;
        }
        if (key == 0x0D) {              /* * = backspace */
            if (freq_len) {
                freq_len--;
            }
            draw_entry_digits();
            return;
        }
        if (key == 0x0F) {              /* # = set */
            if (freq_len == 6) {
                uint32_t khz = 0;
                for (uint8_t i = 0; i < 6; i++) {
                    khz = khz * 10u + freq_digits[i];
                }
                vfo_set(khz * 1000u, tx);
            } else {
                uart_puts("need 6 digits\r\n");
            }
            return;
        }
        if (key == 0x14) {              /* Exit = cancel */
            freq_entry = 0;
            draw_vfo_line();
        }
        return;
    }

    if (d >= 0) {
        freq_entry = 1;
        freq_len = 1;
        freq_digits[0] = (uint8_t)d;
        draw_entry_start();
    } else if (key == 0x01) {           /* SK1 cycles PA level */
        pa_level = (uint8_t)((pa_level + 1) % 3);
        tx_pa = pa_targets[pa_level];
        draw_command(tx, rf_rssi());
    } else if (key == 0x12) {           /* Up: step VFO */
        uint32_t step = step_hz[step_index];
        if (vfo_hz <= 999999000u - step) {
            vfo_set(vfo_hz + step, tx);
        }
    } else if (key == 0x13) {           /* Down: step VFO */
        uint32_t step = step_hz[step_index];
        if (vfo_hz >= 100000000u + step) {
            vfo_set(vfo_hz - step, tx);
        }
    } else if (key == 0x11) {           /* Menu */
        menu_open(tx);
    }
}

static void rf_tx(uint32_t hz)
{
    rf_write_reg(0x47, tx_afout);
    rf_write_reg(0x48, tx_48);
    rf_write_reg(0x37, 0x1D0F);
    rf_write_reg(0x30, 0x0000);
    rf_write_reg(0x1A, 0x1F80);
    rf_set_freq(hz);
    rf_write_reg(0x30, tx_prefix);
    if (hz < 300000000u) {
        rf_gpio(0x8, 1);               /* VHF TX front-end */
        rf_gpio(0x10, 0);
    } else {
        rf_gpio(0x8, 0);
        rf_gpio(0x4, 0);
        rf_gpio(0x10, 0);
        rf_gpio(tx_uhf, 1);            /* UHF PA path: GPIO2 or GPIO4 */
    }
    rf_write_reg(0x36, tx_pa);
    rf_write_reg(0x51, 0x0000);        /* no subaudio tone */
    rf_write_reg(0x07, 0x0000);
    rf_write_reg(0x08, 0x0000);
    rf_write_reg(0x40, tx_dev);
    rf_write_reg(0x50, 0x3B20);
    rf_write_reg(0x52, 0x028F);
    rf_write_reg(0x7E, tx_afe);
    rf_write_reg(0x19, tx_agc);
    rf_write_reg(0x2A, tx_2a);
    if (tx_2c) {
        rf_write_reg(0x2C, tx_2c);
    }
    if (tx_2b) {
        rf_write_reg(0x2B, tx_2b);
    }
    if (tx_28) {
        rf_write_reg(0x28, tx_28);
    }

    ctcss_apply();

#if 0
    /* 1 kHz TX tone: proves the modulator/PA path without the mic */
    uint16_t g = rf_read_reg(0x70);
    rf_write_reg(0x70, (g & 0x70FF) | 0x8000 | (60 << 8));
    rf_write_reg(0x30, 0xC3FA);        /* TX tone state */
    rf_write_reg(0x71, 10324);         /* 1 kHz tone word */
#else
    rf_write_reg(0x7D, tx_mic);
    rf_write_reg(0x30, 0xC1FE);        /* TX on */
#endif
}

static void rf_tx_tone(uint32_t hz)
{
    rf_write_reg(0x47, 0x6040);        /* AF out muted */
    rf_write_reg(0x37, 0x1D0F);
    rf_write_reg(0x30, 0x0000);
    rf_write_reg(0x1A, 0x1F80);
    rf_set_freq(hz);
    rf_write_reg(0x30, 0x0004);
    rf_gpio(0x8, 1);                   /* VHF TX front-end */
    rf_gpio(0x10, 0);
    rf_write_reg(0x36, 0x64D7);        /* PA target 100, low gain */
    rf_write_reg(0x51, 0x0000);
    rf_write_reg(0x07, 0x0000);
    rf_write_reg(0x08, 0x0000);
    rf_write_reg(0x40, 0x34EA);        /* FM deviation */
    rf_write_reg(0x50, 0x3B20);        /* TX unmute */
    rf_write_reg(0x70, 0x8000 | (60 << 8)); /* Tone1, gain 60 */
    rf_write_reg(0x71, 10324);         /* 1 kHz */
    rf_write_reg(0x30, 0xC3FA);        /* TX tone state */
}

static uint16_t rf_rssi(void)
{
    return (rf_read_reg(0x67) & 0x01FF) >> 1;
}

static void show_rssi(uint16_t rssi)
{
    char b[9] = "RSSI    ";
    b[5] = '0' + (rssi / 100) % 10;
    b[6] = '0' + (rssi / 10) % 10;
    b[7] = '0' + rssi % 10;
    lcd_fill_rect(32, 54, 8 * 12, 14, COL_BG);
    lcd_text(32, 54, b, COL_YELLOW, COL_BG);
}

static void band_set(uint8_t b)
{
    uint32_t odr = GPIO_ODR(GPIOA);
    odr = (odr & ~(0xFu << 11)) | ((uint32_t)(b & 0xF) << 11);
    GPIO_ODR(GPIOA) = odr;
}

static void rf_dump(void)
{
    static const uint8_t regs[] = {0x00, 0x07, 0x08, 0x30, 0x37, 0x38, 0x39, 0x43, 0x47, 0x48, 0x51, 0x52, 0x67, 0x78};
    for (unsigned i = 0; i < sizeof(regs); i++) {
        uart_puts("reg");
        uart_hex(regs[i]);
        uart_puts("=");
        uart_hex16(rf_read_reg(regs[i]));
        uart_puts("\r\n");
    }
}

static void band_sweep(void)
{
    for (uint8_t b = 0; b < 16; b++) {
        band_set(b);
        delay_ms(30);
        uint16_t r = rf_rssi();
        uart_puts("band=0x");
        uart_hex(b);
        uart_puts(" rssi=");
        uart_dec(r);
        uart_puts("\r\n");
    }
    band_set(0);
}

struct tx_pin {
    uint32_t port;
    uint8_t pin;
};

static const struct tx_pin tx_pins[] = {
    {GPIOF, 4}, {GPIOF, 5}, {GPIOF, 7}, {GPIOD, 2},
    {GPIOC, 1}, {GPIOC, 2}, {GPIOC, 3}, {GPIOC, 6},
    {GPIOC, 8}, {GPIOC, 9}, {GPIOC, 10}, {GPIOC, 11},
    {GPIOC, 13}, {GPIOC, 15},
};

static void tx_pins_clear(void)
{
    for (unsigned i = 0; i < sizeof(tx_pins) / sizeof(tx_pins[0]); i++) {
        GPIO_BRR(tx_pins[i].port) = (1u << tx_pins[i].pin);
    }
}

static void tx_pins_select(uint8_t step)
{
    tx_pins_clear();
    unsigned n = sizeof(tx_pins) / sizeof(tx_pins[0]);
    if (step >= 1 && step <= n) {
        unsigned i = step - 1;
        GPIO_BSRR(tx_pins[i].port) = (1u << tx_pins[i].pin);
    } else if (step == n + 1) {
        for (unsigned i = 0; i < n; i++) {
            GPIO_BSRR(tx_pins[i].port) = (1u << tx_pins[i].pin);
        }
    }
}

static void show_tx_step(uint8_t step)
{
    char b[6] = {'T', 'X', ' ', '0', '0', 0};
    b[3] = '0' + (step / 10) % 10;
    b[4] = '0' + step % 10;
    lcd_fill_rect(8, 104, 8 * 12, 14, COL_BG);
    lcd_text(8, 104, b, COL_WHITE, COL_BG);
}

#define ADC1_BASE 0x40012400u
#define ADC_STS   REG(ADC1_BASE + 0x00u)
#define ADC_CTRL2 REG(ADC1_BASE + 0x08u)
#define ADC_SPT2  REG(ADC1_BASE + 0x10u)
#define ADC_OSQ1  REG(ADC1_BASE + 0x2Cu)
#define ADC_OSQ3  REG(ADC1_BASE + 0x34u)
#define ADC_ODT   REG(ADC1_BASE + 0x4Cu)

static void adc_init(void)
{
    CRM_APB2EN |= (1u << 9);            /* ADC1 clock */
    gpio_mode(GPIOA, 0, 0x0);           /* PA0 analog */
    gpio_mode(GPIOA, 1, 0x0);           /* PA1 analog */
    (void)CRM_APB2EN;
    delay_ms(5);
    ADC_CTRL2 |= (1u << 0);             /* ADC enable */
    delay_ms(5);
    ADC_CTRL2 |= (1u << 3);             /* calibration init */
    while (ADC_CTRL2 & (1u << 3)) {
    }
    ADC_CTRL2 |= (1u << 2);             /* calibration */
    while (ADC_CTRL2 & (1u << 2)) {
    }
}

static uint16_t adc_read(uint8_t ch)
{
    ADC_OSQ1 = (ADC_OSQ1 & ~(0xFu << 20));   /* sequence length 1 */
    uint32_t spt = ADC_SPT2;
    if (ch < 10) {
        spt &= ~(7u << (ch * 3));
        spt |= (7u << (ch * 3));
    } else {
        spt &= ~(7u << ((ch - 10) * 3));
        spt |= (7u << ((ch - 10) * 3));
    }
    ADC_SPT2 = spt;
    ADC_OSQ3 = ch;
    ADC_CTRL2 |= (1u << 20) | (1u << 22);
    for (volatile uint32_t i = 0; i < 20000 && !(ADC_STS & 2u); i++) {
    }
    uint16_t v = ADC_ODT & 0xFFFu;
    ADC_STS = ~2u;
    return v;
}

static void battery_update(void)
{
    battery_raw = adc_read(1);
    if (battery_raw <= 48) {
        battery_pct = 0;
    } else if (battery_raw >= 70) {
        battery_pct = 100;
    } else {
        battery_pct = (uint8_t)((battery_raw - 48) * 100u / 22u);
    }
}

static void show_adc(uint8_t ch, uint16_t v)
{
    char b[8] = {'A', '0', ' ', '0', '0', '0', '0', 0};
    b[1] = (char)('0' + ch);
    b[3] = (char)('0' + (v / 1000) % 10);
    b[4] = (char)('0' + (v / 100) % 10);
    b[5] = (char)('0' + (v / 10) % 10);
    b[6] = (char)('0' + v % 10);
    lcd_fill_rect(38, 32 + ch * 22, 7 * 12, 14, COL_BG);
    lcd_text(38, 32 + ch * 22, b, COL_WHITE, COL_BG);
}

static void show_bias_step(uint8_t step)
{
    char b[8] = {'B', ' ', ' ', ' ', '0', '0', 0, 0};
    b[3] = '0' + (step / 10) % 10;
    b[4] = '0' + step % 10;
    lcd_fill_rect(38, 78, 7 * 12, 14, COL_BG);
    lcd_text(38, 78, b, COL_WHITE, COL_BG);
}

struct mic_variant {
    uint16_t mic;
    uint16_t dev;
    uint16_t pa;
    uint16_t afe;
    uint16_t agc;
    uint16_t r2a;
    uint16_t r48;
    uint16_t afout;
    uint16_t r2b;
    uint16_t r28;
    uint16_t uhf;
};

static const struct mic_variant mic_variants[] = {
    {0xE95F, 0x34EA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V00 baseline */
    {0xE95F, 0x34EA, 0x64D7, 0x302E, 0x1041, 0x7400, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V01 2A=7400 */
    {0xE95F, 0x34EA, 0x64D7, 0x302E, 0x1041, 0x6600, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V02 2A=6600 */
    {0xE95F, 0x34EA, 0x64D7, 0x302E, 0x1041, 0x7FFF, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V03 2A=7FFF */
    {0xE95F, 0x3AEA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V04 40=3AEA */
    {0xE95F, 0x3AF0, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V05 40=3AF0 */
    {0xE95F, 0x3AFF, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V06 40=3AFF */
    {0xE95F, 0x34FF, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V07 40=34FF */
    {0xE95F, 0x34F0, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V08 40=34F0 */
    {0xE95F, 0x34EA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0010}, /* V09 GPIO4 front-end */
    {0xE95F, 0x3AEA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0010}, /* V10 GPIO4 + 3AEA */
    {0xE95F, 0x3AF0, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0010}, /* V11 GPIO4 + 3AF0 */
    {0xE35F, 0x3AEA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V12 7D=E35F */
    {0xE952, 0x3AEA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V13 7D=E952 */
    {0xE95F, 0x3AEA, 0x64D7, 0x302E, 0x1041, 0x7400, 0xBBBF, 0x6140, 0xC000, 0x0A40, 0x0004}, /* V14 3AEA+7400 */
    {0xE95F, 0x3AEA, 0x64D7, 0x302E, 0x1041, 0x4F18, 0xBBBF, 0x6140, 0x0000, 0x0000, 0x0004}, /* V15 3AEA no 2B/28 */
};

static void show_variant(uint8_t v)
{
    char b[6] = {'V', ' ', '0', '0', 0, 0};
    b[2] = '0' + (v / 10) % 10;
    b[3] = '0' + v % 10;
    lcd_fill_rect(38, 104, 7 * 12, 14, COL_BG);
    lcd_text(38, 104, b, COL_WHITE, COL_BG);
}

static void set_variant(uint8_t v)
{
    tx_mic = mic_variants[v].mic;
    tx_dev = mic_variants[v].dev;
    tx_pa = mic_variants[v].pa;
    tx_afe = mic_variants[v].afe;
    tx_agc = mic_variants[v].agc;
    tx_2a = mic_variants[v].r2a;
    tx_48 = mic_variants[v].r48;
    tx_afout = mic_variants[v].afout;
    tx_2b = mic_variants[v].r2b;
    tx_28 = mic_variants[v].r28;
    tx_uhf = mic_variants[v].uhf;
    tx_prefix = 0x0004;
    tx_2c = 0x0000;
    show_variant(v);
}

/* ---- keypad ---- */
/* Virtual buttons injected over the debug UART.  Bit N = key code N.
 * Codes 0x00-0x15 are the matrix/side/power keys; 0x16 is PTT. */
#define VKEY_PTT_CODE 0x16u
#define VKEY_PTT (1u << VKEY_PTT_CODE)
#define VKEY_MASK ((1u << 23) - 1u)
static uint32_t vkey_mask;

/* Physical matrix (observed with tools/keytest): rows are PB12, PB8, PB9,
 * PB10, PB11; columns are PB2..PB5.
 *   r1: 7 8 9 #
 *   r2: V/M A/B Band -
 *   r3: Menu Up Down Exit
 *   r4: 1 2 3 *
 *   r5: 4 5 6 0
 * r0 is the idle/side-key row (SK1, SK2). */
static const uint8_t key_table[6][4] = {
    {0x01, 0x00, 0xFF, 0xFF},
    {0x09, 0x0A, 0x0B, 0x0F},
    {0x08, 0x0C, 0x10, 0xFF},
    {0x11, 0x12, 0x13, 0x14},
    {0x02, 0x03, 0x04, 0x0D},
    {0x05, 0x06, 0x07, 0x0E},
};

static const char *key_name(uint8_t code)
{
    switch (code) {
    case 0x00: return "SK2";
    case 0x01: return "SK1";
    case 0x15: return "Power";
    case 0x16: return "PTT";
    case 0x02: return "1";
    case 0x03: return "2";
    case 0x04: return "3";
    case 0x05: return "4";
    case 0x06: return "5";
    case 0x07: return "6";
    case 0x08: return "V/M";
    case 0x09: return "7";
    case 0x0A: return "8";
    case 0x0B: return "9";
    case 0x0C: return "A/B";
    case 0x0D: return "*";
    case 0x0E: return "0";
    case 0x0F: return "#";
    case 0x10: return "Band";
    case 0x11: return "Menu";
    case 0x12: return "Up";
    case 0x13: return "Down";
    case 0x14: return "Exit";
    default:   return "?";
    }
}

static void rows_idle(void)
{
    GPIO_ODR(GPIOB) |= (0x1Fu << 8);
}

static uint8_t scan_once(void)
{
    static const uint8_t row_pin[6] = {0xFF, 12, 8, 9, 10, 11};

    for (unsigned r = 0; r < 6; r++) {
        rows_idle();
        if (r != 0) {
            GPIO_ODR(GPIOB) &= ~(1u << row_pin[r]);
        }
        for (volatile unsigned d = 0; d < 200; d++) {
        }
        uint32_t col = GPIO_IDR(GPIOB) & 0x3Cu;
        if (col == 0x3Cu) {
            continue;
        }
        int c = -1;
        if (col == 0x1Cu) c = 3;
        else if (col == 0x2Cu) c = 2;
        else if (col == 0x34u) c = 1;
        else if (col == 0x38u) c = 0;
        if (c >= 0 && key_table[r][c] != 0xFF) {
            rows_idle();
            return key_table[r][c];
        }
    }
    rows_idle();

    for (unsigned code = 0; code <= 0x15; code++) {
        if (vkey_mask & (1u << code)) {
            return (uint8_t)code;
        }
    }
    return 0xFF;
}

static void print_keymask(void)
{
    uart_puts("keymask=");
    uart_hex16((uint16_t)(vkey_mask >> 16));
    uart_hex16((uint16_t)vkey_mask);
    uart_puts("\r\n");
}

/* K <hexmask> — set the entire virtual button mask at once. */
static void key_mask_cmd(void)
{
    char ch;
    uint32_t mask = 0;
    uint8_t nhex = 0;

    while (uart_getc_timeout(&ch)) {
        if (ch == ' ' || ch == '\t') {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            break;
        }
        int v = hexval(ch);
        if (v < 0) {
            break;
        }
        mask = (mask << 4) | (uint32_t)v;
        nhex++;
    }
    if (nhex) {
        vkey_mask = mask & VKEY_MASK;
        print_keymask();
    } else {
        print_keymask();
    }
}

static void delay_loops(uint32_t n)
{
    while (n--) {
        for (volatile unsigned i = 0; i < 200; i++) {
        }
    }
}

static void enter_stub(void)
{
    __asm volatile("cpsid i" ::: "memory");
    volatile uint8_t *dst = (volatile uint8_t *)0x20004000u;
    for (uint32_t i = 0; i < stub_blob_len; i++) {
        dst[i] = stub_blob[i];
    }
    uint32_t sp = *(volatile uint32_t *)0x20004000u;
    uint32_t pc = *(volatile uint32_t *)0x20004004u;
    REG(0xE000ED08u) = 0x20004000u;
    __asm volatile("msr msp, %0" : : "r"(sp) : "memory");
    ((void (*)(void))pc)();
}

void main(void)
{
    for (uint32_t *src = &_sidata, *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *p = &_sbss; p < &_ebss;) {
        *p++ = 0;
    }

    clock_hsi_8mhz();
    gpio_init();
    uart_init();

    uart_puts("\r\nOPENF8HP RF hello\r\n");
    uart_puts("USART1 PA9 @115200, LCD ST7735, FD6818, keypad/PTT\r\n");

    lcd_init();
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    draw_splash(false);
    delay_ms(1200);

    uint16_t rf_id = rf_read_reg(0x00);
    if (rf_id == 0xFFFF || rf_id == 0x0000) {
        delay_ms(10);
        rf_id = rf_read_reg(0x00);
    }
    rf_init(rf_id);
    rf_rx(vfo_hz);
    delay_ms(30);

    adc_init();
    battery_update();
    set_variant(0);
    tx_pa = pa_targets[0];
    pa_level = 0;
    GPIO_BSRR(GPIOC) = (1u << 14);     /* speaker amp enable (confirmed) */
    draw_command(false, rf_rssi());
    squelch_update(rf_rssi());

    uart_puts("RF id=0x");
    uart_hex16(rf_id);
    uart_puts("\r\n");
    uart_puts("COMMAND UI; V00 mic; cmds: d/b/r/s/v/K/x\r\n");
    print_vfo();

    uint8_t ptt_stable = 0;
    uint8_t ptt_count = 0;
    uint8_t last_key = 0xFF;
    uint16_t tx_ticks = 0;
    uint8_t tx_cut = 0;
    uint32_t tick = 0;

    for (;;) {
        char c;
        if (uart_getc(&c)) {
            if (c == 'd') {
                rf_dump();
            } else if (c == 'b') {
                band_sweep();
            } else if (c == 'r') {
                uint16_t r = rf_rssi();
                uart_puts("rssi=");
                uart_dec(r);
                uart_puts("\r\n");
                draw_rssi_only(r);
            } else if (c == 's') {
                send_screen();
            } else if (c == 'x') {
                draw_splash(true);
                uart_puts("entering RAM stub\r\n");
                enter_stub();
            } else if (c == 'v') {
                battery_update();
                uart_puts("bat raw=");
                uart_dec(battery_raw);
                uart_puts(" pct=");
                uart_dec(battery_pct);
                uart_puts(" pa=");
                uart_puts(pa_names[pa_level]);
                uart_puts(" sql=");
                uart_dec(sq_level);
                uart_puts(" amp=");
                uart_dec((uint8_t)((GPIO_ODR(GPIOC) >> 14) & 1u));
                uart_puts("\r\n");
            } else if (c == 'K') {
                key_mask_cmd();
            }
        }

        uint8_t key = scan_once();
        if (key != last_key) {
            last_key = key;
            handle_key(key, ptt_stable);
        }

        uint8_t ptt = ((GPIO_IDR(GPIOA) & (1u << 10)) ? 0 : 1) |
                      ((vkey_mask & VKEY_PTT) ? 1 : 0);
        if (ptt != ptt_stable) {
            if (++ptt_count >= 3) {
                ptt_count = 0;
                ptt_stable = ptt;
                if (ptt) {
                    tx_ticks = 0;
                    tx_cut = 0;
                    freq_entry = 0;
                    menu_active = 0;
                    menu_screen = 0;
                    GPIO_BRR(GPIOC) = (1u << 14);   /* speaker amp off */
                    rf_tx(vfo_hz);
                    draw_command(true, rf_rssi());
                } else {
                    rf_rx(vfo_hz);
                    squelch_update(rf_rssi());
                    draw_command(false, rf_rssi());
                }
                uart_puts(ptt ? "ptt=1\r\n" : "ptt=0\r\n");
            }
        } else {
            ptt_count = 0;
        }

        if (ptt_stable) {
            if (!tx_cut && ++tx_ticks >= 600) {
                tx_cut = 1;
                rf_rx(vfo_hz);
                squelch_update(rf_rssi());
                draw_command(false, rf_rssi());
                uart_puts("tx timeout\r\n");
            }
        } else {
            tx_ticks = 0;
            tx_cut = 0;
        }

        if (menu_lock) {
            menu_lock--;
        }
        delay_loops(20);

        if (!ptt_stable && !menu_active && ++tick % 10 == 0) {
            battery_update();
            uint16_t rssi = rf_rssi();
            squelch_update(rssi);
            draw_rssi_only(rssi);
            if (tick % 50 == 0) {
                uart_puts("rssi=");
                uart_dec(rssi);
                uart_puts("\r\n");
            }
        }
    }
}


/* ---------------- AFSK 1200 / AX.25 TX ---------------- */

#define TNC_HZ 145100000u
#define TONE_GAIN 60
#define TONE1_1200 12389u
#define TONE2_2200 22714u

static volatile uint8_t afsk_bits[6000];
static uint8_t raw_bits[6000];
static uint16_t raw_len;
static volatile uint16_t afsk_len, afsk_pos;
static volatile uint8_t afsk_active;
static uint8_t stuff_ones;

static uint8_t ax25[400];
static uint16_t axlen;

static uint8_t kiss_buf[400];
static uint16_t kiss_len;
static uint8_t kiss_esc;

static void raw_add(uint8_t b)
{
    if (raw_len < sizeof(raw_bits)) {
        raw_bits[raw_len++] = b;
    }
}

static void add_flag(void)
{
    for (uint8_t i = 0; i < 8; i++) {
        raw_add((0x7E >> i) & 1);
    }
    stuff_ones = 0;
}

static void add_byte_stuffed(uint8_t b)
{
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t bit = (b >> i) & 1;
        if (stuff_ones == 5 && bit) {
            raw_add(0);
            stuff_ones = 0;
        }
        raw_add(bit);
        if (bit) {
            stuff_ones++;
        } else {
            stuff_ones = 0;
        }
    }
}

static uint16_t crc16_x25(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (uint16_t)((crc >> 1) ^ 0x8408);
            } else {
                crc >>= 1;
            }
        }
    }
    return (uint16_t)~crc;
}

void systick_handler(void)
{
    if (!afsk_active) {
        SYST_CSR = 0;
        return;
    }
    if (afsk_pos < afsk_len) {
        uint8_t t = afsk_bits[afsk_pos++];
        uint16_t v = 0x8000u | 0x0080u;      /* both generators on */
        if (t) {
            v |= (uint16_t)(TONE_GAIN << 8); /* 1200 Hz */
        } else {
            v |= TONE_GAIN;                  /* 2200 Hz */
        }
        rf_write_reg(0x70, v);
    } else {
        afsk_active = 0;
        SYST_CSR = 0;
        rf_write_reg(0x70, 0x0000);
    }
}

static void rf_tx_afsk_prepare(uint32_t hz)
{
    rf_write_reg(0x47, 0x6040);        /* AF out muted */
    rf_write_reg(0x37, 0x1D0F);
    rf_write_reg(0x30, 0x0000);
    rf_write_reg(0x1A, 0x1F80);
    rf_set_freq(hz);
    rf_write_reg(0x30, 0x0004);
    rf_gpio(0x8, 1);                   /* VHF TX front-end */
    rf_gpio(0x10, 0);
    rf_write_reg(0x36, 0x64D7);
    rf_write_reg(0x51, 0x0000);
    rf_write_reg(0x07, 0x0000);
    rf_write_reg(0x08, 0x0000);
    rf_write_reg(0x40, 0x34EA);        /* FM deviation */
    rf_write_reg(0x50, 0x3B20);        /* TX unmute */
    rf_write_reg(0x71, TONE1_1200);
    rf_write_reg(0x72, TONE2_2200);
    rf_write_reg(0x70, 0x8000u | (uint16_t)(TONE_GAIN << 8) | 0x0080u);
    rf_write_reg(0x30, 0xC3FA);        /* tone TX state */
    GPIO_BRR(GPIOC) = (1u << 14);      /* speaker amp off */
}

static void afsk_send_frame(const uint8_t *frame, uint16_t n)
{
    if (afsk_active) {
        return;
    }
    raw_len = 0;
    stuff_ones = 0;
    for (uint8_t i = 0; i < 48; i++) {
        add_flag();
    }
    for (uint16_t i = 0; i < n; i++) {
        add_byte_stuffed(frame[i]);
    }
    uint16_t fcs = crc16_x25(frame, n);
    add_byte_stuffed((uint8_t)(fcs & 0xFF));
    add_byte_stuffed((uint8_t)(fcs >> 8));
    for (uint8_t i = 0; i < 8; i++) {
        add_flag();
    }

    uint8_t tone = 1;                  /* 1200 Hz mark */
    for (uint16_t i = 0; i < raw_len; i++) {
        if (!raw_bits[i]) {
            tone ^= 1;
        }
        afsk_bits[i] = tone;
    }
    afsk_len = raw_len;
    afsk_pos = 0;

    rf_tx_afsk_prepare(TNC_HZ);
    afsk_active = 1;
    SYST_RVR = 6666;                   /* 8 MHz / 1200 */
    SYST_CVR = 0;
    SYST_CSR = 7;                      /* core clock, irq, enable */
    for (uint32_t guard = 0; afsk_active && guard < 20000000u; guard++) {
    }
    afsk_active = 0;
    SYST_CSR = 0;
    rf_write_reg(0x70, 0x0000);
    rf_rx(TNC_HZ);
    GPIO_BSRR(GPIOC) = (1u << 14);
}

static void ax25_addr(const char *call, uint8_t ssid, uint8_t last, uint8_t cbit)
{
    for (uint8_t i = 0; i < 6; i++) {
        char c = call[i];
        if (!c) {
            c = ' ';
        }
        ax25[axlen++] = (uint8_t)((uint8_t)c << 1);
    }
    ax25[axlen++] = (uint8_t)(0x60u | ((ssid & 0xFu) << 1) |
                              (last ? 1u : 0u) | (cbit ? 0x80u : 0u));
}

static void send_k8cy(void)
{
    axlen = 0;
    ax25_addr("APRS  ", 0, 0, 0);
    ax25_addr("AD8NT ", 7, 1, 1);
    ax25[axlen++] = 0x03;              /* UI */
    ax25[axlen++] = 0xF0;              /* no layer 3 */
    const char info[] = ">OpenF8HP AFSK 1200 test";
    for (uint8_t i = 0; info[i]; i++) {
        ax25[axlen++] = (uint8_t)info[i];
    }
    afsk_send_frame(ax25, axlen);
}

static void kiss_byte(uint8_t c)
{
    if (c == 0xC0) {
        if (kiss_len > 1 && kiss_buf[0] == 0x00) {
            afsk_send_frame(kiss_buf + 1, (uint16_t)(kiss_len - 1));
        }
        kiss_len = 0;
        kiss_esc = 0;
        return;
    }
    if (kiss_esc) {
        if (c == 0xDC) {
            c = 0xC0;
        } else if (c == 0xDD) {
            c = 0xDB;
        }
        kiss_esc = 0;
    } else if (c == 0xDB) {
        kiss_esc = 1;
        return;
    }
    if (kiss_len < sizeof(kiss_buf)) {
        kiss_buf[kiss_len++] = c;
    }
}

void main_mic(void)
{
    for (uint32_t *src = &_sidata, *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *p = &_sbss; p < &_ebss;) {
        *p++ = 0;
    }

    clock_hsi_8mhz();
    gpio_init();
    uart_init();
    lcd_init();
    lcd_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);

    uint16_t id = rf_read_reg(0x00);
    if (id == 0xFFFF || id == 0x0000) {
        delay_ms(10);
        id = rf_read_reg(0x00);
    }
    rf_init(id);
    rf_rx(145100000u);
    GPIO_BSRR(GPIOC) = (1u << 14);     /* speaker amp on */

    lcd_text(38, 10, "145.100", COL_WHITE, COL_BG);
    lcd_text(38, 32, "MIC TX", COL_WHITE, COL_BG);
    lcd_text(38, 78, "PTT = TX", COL_YELLOW, COL_BG);
    set_variant(0);

    uart_puts("\r\nMIC TX locked V00 @145.100\r\n");

    uint8_t ptt_stable = 0;
    uint8_t ptt_count = 0;

    for (;;) {
        uint8_t ptt = (GPIO_IDR(GPIOA) & (1u << 10)) ? 0 : 1;
        if (ptt != ptt_stable) {
            if (++ptt_count >= 3) {
                ptt_count = 0;
                ptt_stable = ptt;
                if (ptt) {
                    GPIO_BRR(GPIOC) = (1u << 14);
                    rf_tx(145100000u);
                    lcd_fill_rect(38, 78, 7 * 12, 14, COL_BG);
                    lcd_text(38, 78, "TX", COL_RED, COL_BG);
                } else {
                    rf_rx(145100000u);
                    GPIO_BSRR(GPIOC) = (1u << 14);
                    lcd_fill_rect(38, 78, 7 * 12, 14, COL_BG);
                    lcd_text(38, 78, "PTT = TX", COL_YELLOW, COL_BG);
                }
            }
        } else {
            ptt_count = 0;
        }
        delay_loops(20);
    }
}
