#!/bin/sh
# Enforce dependency direction: low layers must not include high layers.
# Layer 0: asm, common. Layer 1: disk, drivers, util. Layer 2: fs*. Layer 3: services. Layer 4: shell, VM.
cd "$(dirname "$0")/.."

layer() {
    case "$1" in
        mem_asm|alloc|common|port_io|driver_types|driver_caps) echo 0 ;;
        disk|disk_asm|cluster|dir_asm|util|terminal|path_log|mem_domain|block_driver|mmio|pci) echo 1 ;;
        fs|fs_types|fs_provider|fs_command|fs_events|fs_policy|fs_chain|fs_facade) echo 2 ;;
        fs_service_glue|priority_queue|vrt|vfs) echo 3 ;;
        interpreter|main|threadpool|vm|vm_decode|vm_cpu|vm_mem|vm_io|vm_loader|vm_display|vm_host|vm_font|vm_disk|vm_snapshot|vm_sdl|task_manager) echo 4 ;;
        *) echo 0 ;;
    esac
}

violations=0
for f in *.c VM/*.c drivers/*.c; do
    [ -f "$f" ] || continue
    case "$f" in *Test*.c|*test*.c) continue ;; esac
    base=$(echo "$f" | sed 's/\.c$//;s|.*/||')
    l=$(layer "$base")
    for inc_raw in $(grep -h '^#include "' "$f" 2>/dev/null | sed 's/.*"\([^"]*\)".*/\1/'); do
        inc=$(echo "$inc_raw" | sed 's/\.h$//;s|.*/||')
        [ -z "$inc" ] && continue
        inc_layer=$(layer "$inc")
        if [ "$inc_layer" -gt "$l" ]; then
            echo "VIOLATION: $f (layer $l) includes $inc (layer $inc_layer)"
            violations=$((violations + 1))
        fi
    done
done

if [ "$violations" -eq 0 ]; then
    echo "Layer check OK: no low->high includes."
else
    echo "Found $violations violation(s)."
fi
exit $violations
