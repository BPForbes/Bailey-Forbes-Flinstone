#include <stdint.h>

uint8_t  port_inb (uint16_t p) { (void)p; return 0xFF; }
void     port_outb(uint16_t p, uint8_t  v) { (void)p; (void)v; }
uint16_t port_inw (uint16_t p) { (void)p; return 0xFFFF; }
void     port_outw(uint16_t p, uint16_t v) { (void)p; (void)v; }
uint32_t port_inl (uint16_t p) { (void)p; return 0xFFFFFFFF; }
void     port_outl(uint16_t p, uint32_t v) { (void)p; (void)v; }
