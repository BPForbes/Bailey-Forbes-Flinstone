#include "fl/driver/drivers.h"
#include "drivers.h"
#include "fl_cstr.h"
#include "fl/mm.h"
#include "fl/mem_asm.h"
#ifndef DRIVERS_BAREMETAL
#include <stdio.h>
#endif

#define FL_MODEL_MAX_DEVICES   32
#define FL_MODEL_MAX_DRIVERS   32
#define FL_MODEL_MAX_DEVFS     32
#define FL_MODEL_MAX_IRQ       64
#define FL_MODEL_MAX_RESOURCES 64
#define FL_MODEL_MAX_DMA       32

/* Halt-style panic used when a hard limit is exceeded at registration time. */
static void model_overflow_panic(const char *msg) {
#ifdef DRIVERS_BAREMETAL
    (void)msg;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
#else
    for (;;) {}
#endif
#else
    fprintf(stderr, "[driver_model] PANIC: %s\n", msg);
    for (;;) {}
#endif
}

typedef struct {
    fl_device_t *dev;
    const fl_driver_desc_t *driver;
    fl_driver_state_t state;
} fl_bound_device_t;

typedef struct fl_devfs_node {
    char path[32];
    fl_driver_class_t class;
    fl_device_t *dev;
    fl_devfs_ops_t ops;
} fl_devfs_node_t;

typedef struct {
    fl_irq_handler_t handler;
    void *ctx;
    fl_device_t *owner;
    int enabled;
    uint64_t dispatch_count;
    uint64_t eoi_count;
} fl_irq_slot_t;

typedef struct {
    fl_resource_t resource;
    fl_device_t *owner;
} fl_resource_claim_t;

typedef struct {
    void *ptr;
    size_t size;
    fl_device_t *owner;
} fl_dma_record_t;

static const fl_driver_desc_t *s_drivers[FL_MODEL_MAX_DRIVERS];
static int s_driver_count;
static fl_bound_device_t s_devices[FL_MODEL_MAX_DEVICES];
static int s_device_count;
static fl_devfs_node_t s_devfs[FL_MODEL_MAX_DEVFS];
static int s_devfs_count;
static fl_irq_slot_t s_irq[FL_MODEL_MAX_IRQ];
static fl_resource_claim_t s_resources[FL_MODEL_MAX_RESOURCES];
static int s_resource_count;
static fl_dma_record_t s_dma[FL_MODEL_MAX_DMA];
static int s_dma_count;
static int s_model_initialized;

static int driver_matches(const fl_driver_desc_t *driver, const fl_device_desc_t *dev) {
    if (!driver || !dev)
        return 0;
    if (driver->synth_id && dev->bus_type == FL_BUS_SYNTH)
        return fl_cstr_eq(driver->synth_id, dev->synth_id);
    if (driver->dt_compatible && dev->bus_type == FL_BUS_DT)
        return fl_cstr_eq(driver->dt_compatible, dev->dt_compatible);
    if (dev->bus_type == FL_BUS_PCI) {
        if (driver->pci_class && driver->pci_class != dev->class_code)
            return 0;
        if (driver->pci_subclass && driver->pci_subclass != dev->subclass)
            return 0;
        return driver->pci_class || driver->pci_subclass;
    }
    return 0;
}

int fl_driver_registry_register(const fl_driver_desc_t *desc) {
    if (!desc || !desc->name || !desc->ops)
        return -1;
    if (s_driver_count >= FL_MODEL_MAX_DRIVERS)
        model_overflow_panic("driver table full (FL_MODEL_MAX_DRIVERS)");
    s_drivers[s_driver_count++] = desc;
    return 0;
}

int fl_driver_registry_match(const fl_device_desc_t *dev, const fl_driver_desc_t **out) {
    if (!dev || !out)
        return -1;
    for (int i = 0; i < s_driver_count; i++) {
        if (driver_matches(s_drivers[i], dev)) {
            *out = s_drivers[i];
            return 0;
        }
    }
    return -1;
}

int fl_device_count(void) {
    return s_device_count;
}

int fl_device_get_info(int index, fl_device_info_t *out) {
    if (!out || index < 0 || index >= s_device_count)
        return -1;
    fl_bound_device_t *bound = &s_devices[index];
    out->dev = bound->dev;
    out->desc = fl_device_get_desc(bound->dev);
    out->driver_name = bound->driver ? bound->driver->name : NULL;
    out->driver_class = bound->driver ? bound->driver->class : FL_DRV_CLASS_NONE;
    out->state = bound->state;
    return 0;
}

fl_device_t *fl_device_at(int index) {
    if (index < 0 || index >= s_device_count)
        return NULL;
    return s_devices[index].dev;
}

fl_device_t *fl_device_find_synth(const char *synth_id) {
    if (!synth_id)
        return NULL;
    for (int i = 0; i < s_device_count; i++) {
        const fl_device_desc_t *desc = fl_device_get_desc(s_devices[i].dev);
        if (desc && desc->bus_type == FL_BUS_SYNTH && fl_cstr_eq(desc->synth_id, synth_id))
            return s_devices[i].dev;
    }
    return NULL;
}

const fl_driver_desc_t *fl_device_get_driver(fl_device_t *dev) {
    if (!dev)
        return NULL;
    for (int i = 0; i < s_device_count; i++)
        if (s_devices[i].dev == dev)
            return s_devices[i].driver;
    return NULL;
}

fl_driver_state_t fl_device_get_state(fl_device_t *dev) {
    if (!dev)
        return FL_DRV_STATE_NONE;
    for (int i = 0; i < s_device_count; i++)
        if (s_devices[i].dev == dev)
            return s_devices[i].state;
    return FL_DRV_STATE_NONE;
}

static int resource_overlaps(const fl_resource_t *a, const fl_resource_t *b) {
    if (!a || !b || a->type != b->type)
        return 0;
    if (a->type == FL_RESOURCE_IRQ)
        return a->start == b->start;
    if (a->size == 0 || b->size == 0)
        return 0;
    uintptr_t a_end = a->start + a->size - 1;
    uintptr_t b_end = b->start + b->size - 1;
    return a->start <= b_end && b->start <= a_end;
}

static int claim_resource_desc(fl_device_t *dev, const fl_resource_t *resource) {
    if (!dev || !resource || s_resource_count >= FL_MODEL_MAX_RESOURCES)
        return -1;
    for (int i = 0; i < s_resource_count; i++)
        if (resource_overlaps(&s_resources[i].resource, resource))
            return -1;
    s_resources[s_resource_count].resource = *resource;
    s_resources[s_resource_count].owner = dev;
    s_resource_count++;
    return 0;
}

int fl_resource_claim(fl_device_t *dev, fl_resource_type_t type, int index) {
    const fl_device_desc_t *desc = fl_device_get_desc(dev);
    if (!desc || index < 0)
        return -1;
    int seen = 0;
    for (int i = 0; i < desc->resource_count; i++) {
        if (desc->resources[i].type != type)
            continue;
        if (seen++ == index)
            return claim_resource_desc(dev, &desc->resources[i]);
    }
    return -1;
}

void fl_resource_release_all(fl_device_t *dev) {
    if (!dev)
        return;
    for (int i = 0; i < s_resource_count; ) {
        if (s_resources[i].owner == dev) {
            s_resources[i] = s_resources[s_resource_count - 1];
            s_resource_count--;
        } else {
            i++;
        }
    }
}

int fl_resource_count(const fl_device_t *dev) {
    int count = 0;
    for (int i = 0; i < s_resource_count; i++)
        if (!dev || s_resources[i].owner == dev)
            count++;
    return count;
}

int fl_resource_get(const fl_device_t *dev, int index, fl_resource_t *out) {
    if (!out || index < 0)
        return -1;
    int seen = 0;
    for (int i = 0; i < s_resource_count; i++) {
        if (dev && s_resources[i].owner != dev)
            continue;
        if (seen++ == index) {
            *out = s_resources[i].resource;
            return 0;
        }
    }
    return -1;
}

static int claim_device_resources(fl_device_t *dev) {
    const fl_device_desc_t *desc = fl_device_get_desc(dev);
    if (!desc)
        return -1;
    for (int i = 0; i < desc->resource_count; i++) {
        if (claim_resource_desc(dev, &desc->resources[i]) != 0)
            return -1;
    }
    return 0;
}

static int block_devfs_read(void *dev, uint32_t unit, void *buf, size_t len, size_t *read_out) {
    (void)dev;
    if (!g_block_driver || !buf || len < FL_SECTOR_SIZE)
        return -1;
    if (g_block_driver->read_sector(g_block_driver, unit, buf) != 0)
        return -1;
    if (read_out)
        *read_out = FL_SECTOR_SIZE;
    return 0;
}

static int block_devfs_write(void *dev, uint32_t unit, const void *buf, size_t len, size_t *written_out) {
    (void)dev;
    if (!g_block_driver || !buf || len < FL_SECTOR_SIZE)
        return -1;
    if (g_block_driver->write_sector(g_block_driver, unit, buf) != 0)
        return -1;
    if (written_out)
        *written_out = FL_SECTOR_SIZE;
    return 0;
}

static int block_devfs_ioctl(void *dev, unsigned long request, void *arg) {
    (void)dev;
    if (!g_block_driver)
        return -1;
    if (request == FL_DEVFS_IOCTL_BLOCK_CAPS)
        return g_block_driver->get_caps(g_block_driver, (fl_block_caps_t *)arg);
    return -1;
}

static int block_probe(fl_device_t *dev) {
    const fl_device_desc_t *desc = fl_device_get_desc(dev);
    return (desc && desc->bus_type == FL_BUS_SYNTH && fl_cstr_eq(desc->synth_id, "host_blk")) ? 0 : -1;
}

static int block_attach(fl_device_t *dev) {
    const fl_devfs_ops_t ops = { block_devfs_read, block_devfs_write, block_devfs_ioctl };
    dev->driver_data = g_block_driver;
    return fl_devfs_register("/dev/blk0", FL_DRV_CLASS_BLOCK, dev, &ops);
}

static void block_start(fl_device_t *dev) {
    (void)dev;
}

static void block_stop(fl_device_t *dev) {
    (void)dev;
}

static void block_detach(fl_device_t *dev) {
    (void)dev;
    fl_devfs_unregister("/dev/blk0");
}

static const fl_driver_ops_t block_model_ops = {
    block_probe, block_attach, block_start, block_stop, block_detach,
};

static const fl_driver_desc_t block_model_driver = {
    FL_DRV_CLASS_BLOCK, "host-block", &block_model_ops, 0, 0, NULL, "host_blk",
};

void fl_driver_registry_register_all(void) {
    fl_driver_registry_register(&block_model_driver);
}

static void fl_dma_release_all(void) {
    while (s_dma_count > 0)
        fl_dma_free(s_dma[s_dma_count - 1].ptr);
}

void fl_drivers_init(void) {
    if (s_model_initialized)
        return;
    fl_device_desc_t descs[FL_MODEL_MAX_DEVICES];
    int count = fl_bus_enumerate(descs, FL_MODEL_MAX_DEVICES);
    if (count > FL_MODEL_MAX_DEVICES)
        model_overflow_panic("device table full (FL_MODEL_MAX_DEVICES)");
    for (int i = 0; i < count && s_device_count < FL_MODEL_MAX_DEVICES; i++) {
        fl_device_t *dev = fl_device_create(&descs[i]);
        if (!dev)
            continue;
        for (int d = 0; d < s_driver_count; d++) {
            const fl_driver_desc_t *driver = s_drivers[d];
            if (!driver_matches(driver, &descs[i]))
                continue;
            if (driver->ops->probe && driver->ops->probe(dev) != 0)
                continue;
            fl_bound_device_t *bound = &s_devices[s_device_count++];
            bound->dev = dev;
            bound->driver = driver;
            bound->state = FL_DRV_STATE_PROBED;
            if (claim_device_resources(dev) != 0) {
                fl_resource_release_all(dev);
                s_device_count--;
                continue;
            }
            if (driver->ops->attach && driver->ops->attach(dev) != 0) {
                fl_resource_release_all(dev);
                s_device_count--;
                continue;
            }
            bound->state = FL_DRV_STATE_ATTACHED;
            if (driver->ops->start)
                driver->ops->start(dev);
            bound->state = FL_DRV_STATE_STARTED;
            dev = NULL;
            break;
        }
        if (dev)
            fl_device_destroy(dev);
    }
    s_model_initialized = 1;
}

void fl_drivers_shutdown(void) {
    fl_dma_release_all();
    for (int i = s_device_count - 1; i >= 0; i--) {
        fl_bound_device_t *bound = &s_devices[i];
        if (bound->driver && bound->driver->ops) {
            if (bound->state == FL_DRV_STATE_STARTED && bound->driver->ops->stop)
                bound->driver->ops->stop(bound->dev);
            if (bound->driver->ops->detach)
                bound->driver->ops->detach(bound->dev);
        }
        fl_resource_release_all(bound->dev);
        fl_device_destroy(bound->dev);
    }
    s_device_count = 0;
    s_driver_count = 0;
    s_devfs_count = 0;
    s_resource_count = 0;
    s_dma_count = 0;
    asm_mem_zero(s_devices, sizeof(s_devices));
    asm_mem_zero(s_resources, sizeof(s_resources));
    asm_mem_zero(s_devfs, sizeof(s_devfs));
    asm_mem_zero(s_dma, sizeof(s_dma));
    fl_irq_init();
    s_model_initialized = 0;
}

int fl_devfs_register(const char *path, int class_id, void *dev, const fl_devfs_ops_t *ops) {
    if (!path || !dev || !ops)
        return -1;
    if (s_devfs_count >= FL_MODEL_MAX_DEVFS)
        model_overflow_panic("devfs table full (FL_MODEL_MAX_DEVFS)");
    if (fl_cstr_len(path, sizeof(s_devfs[0].path)) >= sizeof(s_devfs[0].path))
        return -1;
    for (int i = 0; i < s_devfs_count; i++)
        if (fl_cstr_eq(s_devfs[i].path, path))
            return -1;
    fl_devfs_node_t *node = &s_devfs[s_devfs_count++];
    fl_cstr_copy(node->path, sizeof(node->path), path);
    node->class = (fl_driver_class_t)class_id;
    node->dev = (fl_device_t *)dev;
    node->ops = *ops;
    return 0;
}

void fl_devfs_unregister(const char *path) {
    if (!path)
        return;
    for (int i = 0; i < s_devfs_count; i++) {
        if (fl_cstr_eq(s_devfs[i].path, path)) {
            s_devfs[i] = s_devfs[s_devfs_count - 1];
            s_devfs_count--;
            return;
        }
    }
}

int fl_devfs_open(const char *path, int flags, fl_devfs_file_t *out) {
    if (!path || !out)
        return -1;
    for (int i = 0; i < s_devfs_count; i++) {
        if (fl_cstr_eq(s_devfs[i].path, path)) {
            out->node = &s_devfs[i];
            out->pos = 0;
            out->flags = flags;
            return 0;
        }
    }
    return -1;
}

int fl_devfs_read(fl_devfs_file_t *file, void *buf, size_t size, size_t *read_out) {
    if (!file || !file->node || !(file->flags & FL_DEVFS_O_READ))
        return -1;
    fl_devfs_node_t *node = (fl_devfs_node_t *)file->node;
    int rc;
    /* Character device: byte_read preferred over unit-based read */
    if (node->ops.byte_read) {
        rc = node->ops.byte_read(node->dev, buf, size, read_out);
    } else if (node->ops.read) {
        uint32_t unit = (uint32_t)(file->pos / FL_SECTOR_SIZE);
        rc = node->ops.read(node->dev, unit, buf, size, read_out);
    } else {
        return -1;
    }
    if (rc == 0)
        file->pos += read_out ? *read_out : 0;
    return rc;
}

int fl_devfs_write(fl_devfs_file_t *file, const void *buf, size_t size, size_t *written_out) {
    if (!file || !file->node || !(file->flags & FL_DEVFS_O_WRITE))
        return -1;
    fl_devfs_node_t *node = (fl_devfs_node_t *)file->node;
    int rc;
    /* Character device: byte_write preferred over unit-based write */
    if (node->ops.byte_write) {
        rc = node->ops.byte_write(node->dev, buf, size, written_out);
    } else if (node->ops.write) {
        uint32_t unit = (uint32_t)(file->pos / FL_SECTOR_SIZE);
        rc = node->ops.write(node->dev, unit, buf, size, written_out);
    } else {
        return -1;
    }
    if (rc == 0)
        file->pos += written_out ? *written_out : 0;
    return rc;
}

int fl_devfs_ioctl(fl_devfs_file_t *file, unsigned long request, void *arg) {
    if (!file || !file->node)
        return -1;
    fl_devfs_node_t *node = (fl_devfs_node_t *)file->node;
    if (!node->ops.ioctl)
        return -1;
    return node->ops.ioctl(node->dev, request, arg);
}

int fl_devfs_close(fl_devfs_file_t *file) {
    if (!file)
        return -1;
    file->node = NULL;
    file->pos = 0;
    file->flags = 0;
    return 0;
}

void fl_irq_init(void) {
    asm_mem_zero(s_irq, sizeof(s_irq));
}

static int device_irq_at(const fl_device_t *dev, int irq_resource_index, int *irq_out) {
    fl_resource_t res;
    int seen = 0;
    if (!dev || irq_resource_index < 0 || !irq_out)
        return -1;
    for (int i = 0; fl_resource_get(dev, i, &res) == 0; i++) {
        if (res.type != FL_RESOURCE_IRQ)
            continue;
        if (seen++ == irq_resource_index) {
            *irq_out = (int)res.start;
            return 0;
        }
    }
    return -1;
}

static fl_device_t *irq_owner_from_resources(int irq) {
    for (int i = 0; i < s_resource_count; i++) {
        if (s_resources[i].resource.type == FL_RESOURCE_IRQ &&
            s_resources[i].resource.start == (uintptr_t)irq)
            return s_resources[i].owner;
    }
    return NULL;
}

int fl_irq_register(int irq, fl_irq_handler_t handler, void *ctx) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ || !handler)
        return -1;
    fl_device_t *owner = irq_owner_from_resources(irq);
    if (!owner)
        return -1;
    if (s_irq[irq].handler)
        return -1;
    s_irq[irq].handler = handler;
    s_irq[irq].ctx = ctx;
    s_irq[irq].owner = owner;
    return 0;
}

int fl_irq_register_device(fl_device_t *dev, int irq_resource_index, fl_irq_handler_t handler, void *ctx) {
    int irq = -1;
    if (device_irq_at(dev, irq_resource_index, &irq) != 0)
        return -1;
    if (fl_irq_register(irq, handler, ctx) != 0)
        return -1;
    return 0;
}

void fl_irq_unregister(int irq) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return;
    asm_mem_zero(&s_irq[irq], sizeof(s_irq[irq]));
}

void fl_irq_unregister_device(fl_device_t *dev) {
    if (!dev)
        return;
    for (int irq = 0; irq < FL_MODEL_MAX_IRQ; irq++) {
        if (s_irq[irq].owner == dev)
            asm_mem_zero(&s_irq[irq], sizeof(s_irq[irq]));
    }
}

void fl_irq_enable(int irq) {
    if (irq >= 0 && irq < FL_MODEL_MAX_IRQ)
        s_irq[irq].enabled = 1;
}

void fl_irq_disable(int irq) {
    if (irq >= 0 && irq < FL_MODEL_MAX_IRQ)
        s_irq[irq].enabled = 0;
}

void fl_irq_eoi(int irq) {
    if (irq >= 0 && irq < FL_MODEL_MAX_IRQ)
        s_irq[irq].eoi_count++;
    if (g_pic_driver && g_pic_driver->eoi)
        g_pic_driver->eoi(g_pic_driver, irq);
}

const fl_device_t *fl_irq_owner(int irq) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return NULL;
    return s_irq[irq].owner;
}

uint64_t fl_irq_dispatch_count(int irq) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return 0;
    return s_irq[irq].dispatch_count;
}

int fl_irq_get_info(int irq, fl_irq_info_t *out) {
    if (!out || irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return -1;
    out->irq = irq;
    out->enabled = s_irq[irq].enabled;
    out->dispatch_count = s_irq[irq].dispatch_count;
    out->eoi_count = s_irq[irq].eoi_count;
    out->owner = s_irq[irq].owner;
    return 0;
}

int fl_irq_dispatch(int irq) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return -1;
    if (!s_irq[irq].enabled || !s_irq[irq].handler)
        return -1;
    s_irq[irq].handler(irq, s_irq[irq].ctx);
    s_irq[irq].dispatch_count++;
    fl_irq_eoi(irq);
    return 0;
}

void *fl_dma_alloc_device(fl_device_t *dev, size_t size) {
    if (size == 0 || s_dma_count >= FL_MODEL_MAX_DMA)
        return NULL;
    void *ptr = mem_domain_alloc(MEM_DOMAIN_DRIVER, size);
    if (!ptr)
        return NULL;
    asm_mem_zero(ptr, size);
    s_dma[s_dma_count].ptr = ptr;
    s_dma[s_dma_count].size = size;
    s_dma[s_dma_count].owner = dev;
    s_dma_count++;
    return ptr;
}

void *fl_dma_alloc(size_t size) {
    return fl_dma_alloc_device(NULL, size);
}

void fl_dma_free(void *ptr) {
    if (!ptr)
        return;
    for (int i = 0; i < s_dma_count; i++) {
        if (s_dma[i].ptr == ptr) {
            mem_domain_zero(ptr, s_dma[i].size);
            mem_domain_free(MEM_DOMAIN_DRIVER, ptr);
            s_dma[i] = s_dma[s_dma_count - 1];
            s_dma_count--;
            return;
        }
    }
}

int fl_dma_get_info(void *ptr, fl_dma_info_t *out) {
    if (!ptr || !out)
        return -1;
    for (int i = 0; i < s_dma_count; i++) {
        if (s_dma[i].ptr == ptr) {
            out->ptr = s_dma[i].ptr;
            out->size = s_dma[i].size;
            out->owner = s_dma[i].owner;
            return 0;
        }
    }
    return -1;
}

int fl_dma_allocation_count(void) {
    return s_dma_count;
}

void fl_dma_zero(void *ptr, size_t size) {
    if (ptr && size)
        asm_mem_zero(ptr, size);
}

void fl_dma_copy(void *dst, const void *src, size_t size) {
    if (dst && src && size)
        asm_mem_copy(dst, src, size);
}
