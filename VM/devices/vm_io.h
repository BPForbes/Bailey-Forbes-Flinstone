#ifndef VM_IO_H
#define VM_IO_H

#include <stdint.h>

struct vm_mem;
struct vm_cpu;
struct vm_host;

void vm_io_init(void);
void vm_io_shutdown(void);
void vm_io_set_host(struct vm_host *host);
uint32_t vm_io_in(struct vm_mem *mem, uint32_t port, int size);
void vm_io_out(struct vm_mem *mem, uint32_t port, uint32_t value, int size);

/* Reset: guest writes 0x06/0x0E to port 0xCF9; run loop checks and resets. */
int vm_io_reset_requested(void);
void vm_io_clear_reset(void);

#endif /* VM_IO_H */
