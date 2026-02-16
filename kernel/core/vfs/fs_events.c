#include "fs_events.h"
#include <stdlib.h>
#include <string.h>

#define MAX_HANDLERS 16

typedef struct {
    fs_event_type_t type;
    fs_event_handler_t handler;
    void *userdata;
} handler_entry_t;

static handler_entry_t s_handlers[MAX_HANDLERS];
static int s_count;

void fs_events_init(void) {
    s_count = 0;
    memset(s_handlers, 0, sizeof(s_handlers));
}

void fs_events_subscribe(fs_event_type_t type, fs_event_handler_t handler, void *userdata) {
    if (s_count >= MAX_HANDLERS) return;
    s_handlers[s_count].type = type;
    s_handlers[s_count].handler = handler;
    s_handlers[s_count].userdata = userdata;
    s_count++;
}

void fs_events_publish(const fs_event_t *ev) {
    for (int i = 0; i < s_count; i++) {
        if (s_handlers[i].type == ev->type)
            s_handlers[i].handler(ev, s_handlers[i].userdata);
    }
}

void fs_events_shutdown(void) {
    s_count = 0;
}
