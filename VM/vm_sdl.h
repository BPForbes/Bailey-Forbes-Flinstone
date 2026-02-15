#ifndef VM_SDL_H
#define VM_SDL_H

#include "vm_host.h"

/* SDL2 window frontend. Requires VM_SDL=1 and libSDL2. */
int vm_sdl_init(void);
void vm_sdl_shutdown(void);
int vm_sdl_create_window(int scale);
void vm_sdl_present(vm_host_t *host);
int vm_sdl_poll_events(vm_host_t *host);
int vm_sdl_is_quit(void);
int vm_sdl_is_active(void);

#endif /* VM_SDL_H */
