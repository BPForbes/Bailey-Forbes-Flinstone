#include "fl/driver/drivers.h"
#include "drivers.h"
#include "fl/mm.h"
#include "fl/mem_asm.h"
#include <string.h>

#define FL_MODEL_MAX_DEVICES 16
#define FL_MODEL_MAX_DRIVERS 16
#define FL_MODEL_MAX_DEVFS   16
#define FL_MODEL_MAX_IRQ     32

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
    int enabled;
} fl_irq_slot_t;

static const fl_driver_desc_t *s_drivers[FL_MODEL_MAX_DRIVERS];
static int s_driver_count;
static fl_bound_device_t s_devices[FL_MODEL_MAX_DEVICES];
static int s_device_count;
static fl_devfs_node_t s_devfs[FL_MODEL_MAX_DEVFS];
static int s_devfs_count;
static fl_irq_slot_t s_irq[FL_MODEL_MAX_IRQ];

static int driver_matches(const fl_driver_desc_t *driver, const fl_device_desc_t *dev) {
    if (!driver || !dev)
        return 0;
    if (driver->synth_id && dev->bus_type == FL_BUS_SYNTH)
        return strcmp(driver->synth_id, dev->synth_id) == 0;
    if (driver->dt_compatible && dev->bus_type == FL_BUS_DT)
        return strcmp(driver->dt_compatible, dev->dt_compatible) == 0;
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
    if (!desc || !desc->name || !desc->ops || s_driver_count >= FL_MODEL_MAX_DRIVERS)
        return -1;
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
    return (desc && desc->bus_type == FL_BUS_SYNTH && strcmp(desc->synth_id, "host_blk") == 0) ? 0 : -1;
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

void fl_drivers_init(void) {
    fl_device_desc_t descs[FL_MODEL_MAX_DEVICES];
    int count = fl_bus_enumerate(descs, FL_MODEL_MAX_DEVICES);
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
            if (driver->ops->attach && driver->ops->attach(dev) != 0) {
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
}

void fl_drivers_shutdown(void) {
    for (int i = s_device_count - 1; i >= 0; i--) {
        fl_bound_device_t *bound = &s_devices[i];
        if (bound->driver && bound->driver->ops) {
            if (bound->state == FL_DRV_STATE_STARTED && bound->driver->ops->stop)
                bound->driver->ops->stop(bound->dev);
            if (bound->driver->ops->detach)
                bound->driver->ops->detach(bound->dev);
        }
        fl_device_destroy(bound->dev);
    }
    s_device_count = 0;
    s_driver_count = 0;
    s_devfs_count = 0;
}

int fl_devfs_register(const char *path, int class_id, void *dev, const fl_devfs_ops_t *ops) {
    if (!path || !dev || !ops || s_devfs_count >= FL_MODEL_MAX_DEVFS)
        return -1;
    for (int i = 0; i < s_devfs_count; i++)
        if (strcmp(s_devfs[i].path, path) == 0)
            return -1;
    fl_devfs_node_t *node = &s_devfs[s_devfs_count++];
    strncpy(node->path, path, sizeof(node->path) - 1);
    node->path[sizeof(node->path) - 1] = '\0';
    node->class = (fl_driver_class_t)class_id;
    node->dev = (fl_device_t *)dev;
    node->ops = *ops;
    return 0;
}

void fl_devfs_unregister(const char *path) {
    if (!path)
        return;
    for (int i = 0; i < s_devfs_count; i++) {
        if (strcmp(s_devfs[i].path, path) == 0) {
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
        if (strcmp(s_devfs[i].path, path) == 0) {
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
    if (!node->ops.read)
        return -1;
    uint32_t unit = (uint32_t)(file->pos / FL_SECTOR_SIZE);
    int rc = node->ops.read(node->dev, unit, buf, size, read_out);
    if (rc == 0)
        file->pos += read_out ? *read_out : 0;
    return rc;
}

int fl_devfs_write(fl_devfs_file_t *file, const void *buf, size_t size, size_t *written_out) {
    if (!file || !file->node || !(file->flags & FL_DEVFS_O_WRITE))
        return -1;
    fl_devfs_node_t *node = (fl_devfs_node_t *)file->node;
    if (!node->ops.write)
        return -1;
    uint32_t unit = (uint32_t)(file->pos / FL_SECTOR_SIZE);
    int rc = node->ops.write(node->dev, unit, buf, size, written_out);
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

int fl_irq_register(int irq, fl_irq_handler_t handler, void *ctx) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ || !handler)
        return -1;
    s_irq[irq].handler = handler;
    s_irq[irq].ctx = ctx;
    return 0;
}

void fl_irq_unregister(int irq) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return;
    asm_mem_zero(&s_irq[irq], sizeof(s_irq[irq]));
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
    if (g_pic_driver && g_pic_driver->eoi)
        g_pic_driver->eoi(g_pic_driver, irq);
}

int fl_irq_dispatch(int irq) {
    if (irq < 0 || irq >= FL_MODEL_MAX_IRQ)
        return -1;
    if (!s_irq[irq].enabled || !s_irq[irq].handler)
        return -1;
    s_irq[irq].handler(irq, s_irq[irq].ctx);
    fl_irq_eoi(irq);
    return 0;
}

void *fl_dma_alloc(size_t size) {
    return kmalloc(size);
}

void fl_dma_free(void *ptr) {
    kfree(ptr);
}
