#ifndef FS_EVENTS_H
#define FS_EVENTS_H

#define FS_EVENT_NAME_MAX 32

typedef enum {
    FS_EV_FILE_CREATED,
    FS_EV_FILE_DELETED,
    FS_EV_FILE_SAVED,
    FS_EV_DIR_CREATED,
    FS_EV_DIR_DELETED,
    FS_EV_DIR_CHANGED
} fs_event_type_t;

typedef struct {
    fs_event_type_t type;
    char path[256];
} fs_event_t;

typedef void (*fs_event_handler_t)(const fs_event_t *ev, void *userdata);

void fs_events_init(void);
void fs_events_subscribe(fs_event_type_t type, fs_event_handler_t handler, void *userdata);
void fs_events_publish(const fs_event_t *ev);
void fs_events_shutdown(void);

#endif /* FS_EVENTS_H */
