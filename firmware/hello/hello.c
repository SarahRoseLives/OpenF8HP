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

#define REG(a) (*(volatile uint32_t *)(a))

#define CRM_CTRL   REG(0x40021000u)
#define CRM_CFG    REG(0x40021004u)
#define CRM_APB2EN REG(0x40021018u)

#define GPIOA 0x40010800u
#define GPIOB 0x40010C00u
#define GPIOC 0x40011000u
#define GPIOF 0x40011C00u

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
#define CR1_RE (1u << 2)
#define CR1_TE (1u << 3)
#define CR1_UE (1u << 13)

#define LCD_W 160
#define LCD_H 128

#define COL_BG     0x0010u   /* dark blue */
#define COL_WHITE  0xFFFFu
#define COL_YELLOW 0xFFE0u
#define COL_RED    0xF800u

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
    Default_Handler,
};

__attribute__((naked, noreturn))
void Reset_Handler(void)
{
    __asm volatile(
        "ldr sp, =_estack\n"
        "cpsid i\n"
        "ldr r0, =0xE000ED08\n"
        "ldr r1, =0x08001000\n"
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
    CRM_APB2EN |= (1u << 2) | (1u << 3) | (1u << 4) | (1u << 7) | (1u << 14);
    (void)CRM_APB2EN;

    /* PA9 USART1_TX, PA10 input pull-up (PTT), PA4 LCD backlight */
    gpio_mode(GPIOA, 9, 0xB);
    gpio_mode(GPIOA, 10, 0x8);
    gpio_mode(GPIOA, 4, 0x3);
    GPIO_ODR(GPIOA) |= (1u << 10);
    GPIO_BRR(GPIOA) = (1u << 4);        /* backlight off for now */

    /* PB6 CS, PB7 RST, PB8-PB15 LCD data / keypad rows */
    gpio_mode(GPIOB, 6, 0x3);
    gpio_mode(GPIOB, 7, 0x3);
    for (unsigned p = 8; p <= 15; p++) {
        gpio_mode(GPIOB, p, 0x3);
    }
    GPIO_ODR(GPIOB) |= (1u << 6) | (1u << 7) | (0x1Fu << 8);

    /* PB2-PB5 keypad columns, input pull-up */
    for (unsigned p = 2; p <= 5; p++) {
        gpio_mode(GPIOB, p, 0x8);
    }
    GPIO_ODR(GPIOB) |= (0xFu << 2);

    /* PF6 LCD WR idle high; PC12 LCD RS/DC low (command) */
    gpio_mode(GPIOF, 6, 0x3);
    GPIO_ODR(GPIOF) |= (1u << 6);
    gpio_mode(GPIOC, 12, 0x3);
    GPIO_BRR(GPIOC) = (1u << 12);
}

/* ---- UART ---- */
static void uart_init(void)
{
    USART_BRR = 0x45;
    USART_CR1 = CR1_UE | CR1_TE | CR1_RE;
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

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) {
        return;
    }
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

/* ---- keypad ---- */
static const uint8_t key_table[6][4] = {
    {0x01, 0x00, 0xFF, 0xFF},
    {0x11, 0x12, 0x13, 0x14},
    {0x02, 0x03, 0x04, 0xFF},
    {0x05, 0x06, 0x07, 0x08},
    {0x09, 0x0A, 0x0B, 0x0C},
    {0x0D, 0x0E, 0x0F, 0x10},
};

static const char *key_name(uint8_t code)
{
    switch (code) {
    case 0x00: return "SK2";
    case 0x01: return "SK1";
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
    return 0xFF;
}

static void delay_loops(uint32_t n)
{
    while (n--) {
        for (volatile unsigned i = 0; i < 200; i++) {
        }
    }
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

    uart_puts("\r\nOPENF8HP hello\r\n");
    uart_puts("USART1 PA9 @115200, LCD ST7735, keypad PB8-12/PB2-5, PTT PA10\r\n");

    lcd_init();
    lcd_text(51, 20, "HELLO", COL_WHITE, COL_BG);
    lcd_text(10, 50, "KEY --", COL_YELLOW, COL_BG);

    uint8_t last_key = 0xFF;
    uint8_t last_ptt = 0;
    uint32_t tick = 0;

    for (;;) {
        uint8_t key = scan_once();
        if (key != last_key) {
            last_key = key;
            if (key != 0xFF) {
                char buf[8] = {'K', 'E', 'Y', ' ', ' ', ' ', ' ', 0};
                static const char h[] = "0123456789ABCDEF";
                buf[4] = h[key >> 4];
                buf[5] = h[key & 0xF];
                lcd_fill_rect(10, 50, 7 * 12, 14, COL_BG);
                lcd_text(10, 50, buf, COL_YELLOW, COL_BG);
                uart_puts("key=0x");
                uart_hex(key);
                uart_puts(" ");
                uart_puts(key_name(key));
                uart_puts("\r\n");
            } else {
                uart_puts("key=release\r\n");
            }
        }

        uint8_t ptt = (GPIO_IDR(GPIOA) & (1u << 10)) ? 0 : 1;
        if (ptt != last_ptt) {
            last_ptt = ptt;
            lcd_fill_rect(130, 100, 20, 20, ptt ? COL_RED : COL_BG);
            uart_puts(ptt ? "ptt=1\r\n" : "ptt=0\r\n");
        }

        delay_loops(25);
        if (++tick % 100 == 0) {
            uart_puts(".\r\n");
        }
    }
}
