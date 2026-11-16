/*
 * RAM flash stub for the OpenF8HP app.
 *
 * Entered by the running app via the blob copied to 0x20004000.  It runs
 * entirely from RAM, so the app flash can be erased/programmed safely.
 *
 * Protocol (115200 8N1):
 *   stub -> "RFSTUB\n"
 *   host -> "FLSH" + u32le length + u32le crc32
 *   stub -> "OK\n"
 *   host -> u16le chunk length + chunk bytes  (up to 256 bytes)
 *   stub -> 0x06 per chunk
 *   stub -> "DONE\n" then reset
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define CRM_CTRL   REG(0x40021000u)
#define CRM_CFG    REG(0x40021004u)
#define CRM_APB2EN REG(0x40021018u)
#define GPIOA      0x40010800u
#define GPIO_CRL(p)  REG((p) + 0x00u)
#define GPIO_CRH(p)  REG((p) + 0x04u)
#define USART1     0x40013800u
#define USART_SR   REG(USART1 + 0x00u)
#define USART_DR   REG(USART1 + 0x04u)
#define USART_BRR  REG(USART1 + 0x08u)
#define USART_CR1  REG(USART1 + 0x0Cu)
#define SR_TXE (1u << 7)
#define SR_RXNE (1u << 5)
#define CR1_RE (1u << 2)
#define CR1_TE (1u << 3)
#define CR1_UE (1u << 13)

#define FLASH_BASE   0x40022000u
#define FLASH_UNLOCK REG(FLASH_BASE + 0x04u)
#define FLASH_STS    REG(FLASH_BASE + 0x0Cu)
#define FLASH_CTRL   REG(FLASH_BASE + 0x10u)
#define FLASH_ADDR   REG(FLASH_BASE + 0x14u)
#define FLASH_KEY1   0x45670123u
#define FLASH_KEY2   0xCDEF89ABu

#define APP_BASE     0x08001000u
#define APP_MAX      0x1F000u
#define PAGE_SIZE    0x400u

extern uint32_t _estack;
extern uint32_t _sbss, _ebss;

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

void Reset_Handler(void) __attribute__((naked, noreturn));
void Default_Handler(void) __attribute__((noreturn));
void reset_handler(void) __attribute__((noreturn));

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

static void clock_init(void)
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

static void uart_init(void)
{
    CRM_APB2EN |= (1u << 2) | (1u << 14);
    (void)CRM_APB2EN;
    gpio_mode(GPIOA, 9, 0xB);
    gpio_mode(GPIOA, 10, 0x8);
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

static uint8_t uart_getc(void)
{
    while (!(USART_SR & SR_RXNE)) {
    }
    return (uint8_t)USART_DR;
}

static int flash_wait(void)
{
    for (uint32_t i = 0; i < 2000000u; i++) {
        uint32_t s = FLASH_STS;
        if (!(s & 1u)) {
            return (s & (4u | 16u)) ? -1 : 0;
        }
    }
    return -1;
}

static int flash_erase_page(uint32_t addr)
{
    FLASH_CTRL = 2u;
    FLASH_ADDR = addr;
    FLASH_CTRL = 2u | 64u;
    int r = flash_wait();
    FLASH_CTRL = 0;
    return r;
}

static int flash_program_half(uint32_t addr, uint16_t data)
{
    FLASH_CTRL = 1u;
    *(volatile uint16_t *)addr = data;
    int r = flash_wait();
    FLASH_CTRL = 0;
    return r;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static void fail(const char *msg)
{
    uart_puts(msg);
    for (;;) {
    }
}

void reset_handler(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss;) {
        *p++ = 0;
    }
    clock_init();
    uart_init();

    uart_puts("RFSTUB\n");

    uint8_t hdr[12];
    for (unsigned i = 0; i < sizeof(hdr); i++) {
        hdr[i] = uart_getc();
    }
    if (!(hdr[0] == 'F' && hdr[1] == 'L' && hdr[2] == 'S' && hdr[3] == 'H')) {
        fail("ERR magic\n");
    }
    uint32_t len = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                   ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint32_t expect = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) |
                      ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    if (len == 0 || len > APP_MAX || (len & 1u)) {
        fail("ERR length\n");
    }

    FLASH_UNLOCK = FLASH_KEY1;
    FLASH_UNLOCK = FLASH_KEY2;

    for (uint32_t off = 0; off < len; off += PAGE_SIZE) {
        if (flash_erase_page(APP_BASE + off) != 0) {
            fail("ERR erase\n");
        }
    }
    uart_puts("OK\n");

    uint8_t chunk[256];
    uint32_t pos = 0;
    uint32_t crc = 0xFFFFFFFFu;
    while (pos < len) {
        uint8_t lo = uart_getc();
        uint8_t hi = uart_getc();
        uint32_t n = (uint32_t)lo | ((uint32_t)hi << 8);
        if (n == 0 || n > sizeof(chunk) || pos + n > len) {
            fail("ERR chunk\n");
        }
        for (uint32_t i = 0; i < n; i++) {
            chunk[i] = uart_getc();
        }
        crc = crc32_update(crc, chunk, n);
        for (uint32_t i = 0; i < n; i += 2) {
            uint16_t v = (uint16_t)chunk[i] | ((uint16_t)chunk[i + 1] << 8);
            if (flash_program_half(APP_BASE + pos + i, v) != 0) {
                fail("ERR program\n");
            }
        }
        pos += n;
        uart_putc(0x06);
    }

    if ((~crc) != expect) {
        fail("ERR crc\n");
    }
    uart_puts("DONE\n");
    for (volatile uint32_t i = 0; i < 200000u; i++) {
    }
    REG(0xE000ED0Cu) = 0x05FA0004u; /* NVIC_SystemReset */
    for (;;) {
    }
}

__attribute__((naked, noreturn))
void Reset_Handler(void)
{
    __asm volatile(
        "movw r1, #:lower16:_estack\n"
        "movt r1, #:upper16:_estack\n"
        "mov sp, r1\n"
        "bl reset_handler\n"
        "1: b 1b\n");
}

void Default_Handler(void)
{
    for (;;) {
    }
}
