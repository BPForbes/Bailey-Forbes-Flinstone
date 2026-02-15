/* SDL2 window: framebuffer blit, keyboard -> host queue */
#ifdef VM_SDL

#include "vm_sdl.h"
#include "vm_host.h"
#include "vm_mem.h"
#include "vm_font.h"
#include "mem_asm.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#define CELL_W  VM_FONT_W
#define CELL_H  (VM_FONT_HEIGHT * 2)
#define COLS    80
#define ROWS    25

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_font_atlas;
static int s_scale = 2;
static int s_quit;
static int s_active;

static const SDL_Color s_vga_colors[16] = {
    {0x00, 0x00, 0x00, 255},
    {0x00, 0x00, 0xAA, 255},
    {0x00, 0xAA, 0x00, 255},
    {0x00, 0xAA, 0xAA, 255},
    {0xAA, 0x00, 0x00, 255},
    {0xAA, 0x00, 0xAA, 255},
    {0xAA, 0x55, 0x00, 255},
    {0xAA, 0xAA, 0xAA, 255},
    {0x55, 0x55, 0x55, 255},
    {0x55, 0x55, 0xFF, 255},
    {0x55, 0xFF, 0x55, 255},
    {0x55, 0xFF, 0xFF, 255},
    {0xFF, 0x55, 0x55, 255},
    {0xFF, 0x55, 0xFF, 255},
    {0xFF, 0xFF, 0x55, 255},
    {0xFF, 0xFF, 0xFF, 255},
};

static void build_font_atlas(void) {
    int atlas_w = 128;
    int atlas_h = 64;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, atlas_w, atlas_h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return;
    SDL_FillRect(surf, NULL, 0);
    Uint32 *pixels = (Uint32 *)surf->pixels;
    int pitch = surf->pitch / 4;
    for (int ch = 0; ch < 128; ch++) {
        int gx = (ch % 16) * 8;
        int gy = (ch / 16) * 8;
        for (int row = 0; row < 8; row++) {
            unsigned char line = vm_font_8x8[ch][row];
            for (int col = 0; col < 8; col++) {
                if (line & (1 << (7 - col)))
                    pixels[(gy + row) * pitch + gx + col] = 0xFFFFFFFF;
                else
                    pixels[(gy + row) * pitch + gx + col] = 0x00000000;
            }
        }
    }
    s_font_atlas = SDL_CreateTextureFromSurface(s_renderer, surf);
    SDL_FreeSurface(surf);
    if (s_font_atlas)
        SDL_SetTextureBlendMode(s_font_atlas, SDL_BLENDMODE_BLEND);
}

int vm_sdl_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return -1;
    s_window = NULL;
    s_renderer = NULL;
    s_font_atlas = NULL;
    s_quit = 0;
    s_active = 0;
    return 0;
}

void vm_sdl_shutdown(void) {
    s_active = 0;
    if (s_font_atlas) { SDL_DestroyTexture(s_font_atlas); s_font_atlas = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window) { SDL_DestroyWindow(s_window); s_window = NULL; }
    SDL_Quit();
}

int vm_sdl_create_window(int scale) {
    if (scale <= 0) scale = 2;
    s_scale = scale;
    int w = COLS * CELL_W * scale;
    int h = ROWS * CELL_H * scale;
    s_window = SDL_CreateWindow("Flinstone VM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
    if (!s_window) return -1;
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        return -1;
    }
    build_font_atlas();
    s_active = 1;
    return 0;
}

void vm_sdl_present(vm_host_t *host) {
    if (!host || !s_window || !s_renderer || !s_font_atlas) return;
    vm_mem_t *mem = vm_host_mem(host);
    if (!mem || !mem->ram) return;
    if (GUEST_VGA_BASE + COLS * ROWS * 2 > mem->size) return;

    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            uint16_t word = (mem->ram[GUEST_VGA_BASE + (row * COLS + col) * 2]) |
                            (mem->ram[GUEST_VGA_BASE + (row * COLS + col) * 2 + 1] << 8);
            unsigned char ch = (unsigned char)(word & 0xFF);
            unsigned char attr = (unsigned char)(word >> 8);
            int fg = attr & 0x0F;
            int bg = (attr >> 4) & 0x0F;
            if (ch >= 128) ch = ' ';
            SDL_Rect src = {(ch % 16) * 8, (ch / 16) * 8, 8, 8};
            SDL_Rect dst = {col * CELL_W * s_scale, row * CELL_H * s_scale, CELL_W * s_scale, CELL_H * s_scale};
            SDL_SetRenderDrawColor(s_renderer, s_vga_colors[bg].r, s_vga_colors[bg].g, s_vga_colors[bg].b, 255);
            SDL_RenderFillRect(s_renderer, &(SDL_Rect){dst.x, dst.y, dst.w, dst.h});
            SDL_SetTextureColorMod(s_font_atlas, s_vga_colors[fg].r, s_vga_colors[fg].g, s_vga_colors[fg].b);
            SDL_RenderCopy(s_renderer, s_font_atlas, &src, &dst);
        }
    }
    SDL_RenderPresent(s_renderer);
}

int vm_sdl_poll_events(vm_host_t *host) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            s_quit = 1;
        if (e.type == SDL_KEYDOWN && host) {
            uint8_t sc = (uint8_t)(e.key.keysym.scancode & 0xFF);
            vm_host_kbd_push(host, sc);
        }
    }
    return 0;
}

int vm_sdl_is_quit(void) {
    return s_quit;
}

int vm_sdl_is_active(void) {
    return s_active;
}

#endif /* VM_SDL */
