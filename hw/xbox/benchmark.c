/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties-system.h"
#include "hw/xbox/benchmark-clock.h"
#include "migration/vmstate.h"
#include "system/runstate.h"

#define TYPE_XBOX_BENCHMARK "xbox-benchmark"
OBJECT_DECLARE_SIMPLE_TYPE(XboxBenchmarkState, XBOX_BENCHMARK)

struct XboxBenchmarkState {
    ISADevice parent_obj;
    MemoryRegion io;
    CharFrontend chr;
    VMChangeStateEntry *vm_change;
    XemuBenchmarkClock clock;
    uint32_t session[2];
};

static void benchmark_state_change(void *opaque, bool running, RunState state)
{
    XboxBenchmarkState *s = opaque;
    s->clock.valid = false;
}

static void benchmark_reset(DeviceState *dev)
{
    XboxBenchmarkState *s = XBOX_BENCHMARK(dev);
    memset(&s->clock, 0, sizeof(s->clock));
    memset(s->session, 0, sizeof(s->session));
}

static uint64_t benchmark_read(void *opaque, hwaddr addr, unsigned size)
{
    return addr == 0 ? XEMU_BENCHMARK_MAGIC : 0;
}

static void benchmark_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    XboxBenchmarkState *s = opaque;
    uint32_t token = value;
    if (addr == 4 || addr == 8) {
        s->clock.valid = false;
        s->session[addr / 4 - 1] = token;
        return;
    }
    if (addr != 0) {
        s->clock.valid = false;
        return;
    }
    /* No output or allocation on BEGIN. END output is outside the interval. */
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (token & XEMU_BENCHMARK_BEGIN) {
        xemu_benchmark_begin(&s->clock, token & ~XEMU_BENCHMARK_BEGIN, now);
        s->clock.valid &= runstate_is_running();
    } else {
        int64_t ns = xemu_benchmark_end(&s->clock, token, now);
        char line[160];
        int len = snprintf(line, sizeof(line), "%08x%08x,%u,%" PRId64 "\n",
                           s->session[1], s->session[0], token, ns);
        qemu_chr_fe_write_all(&s->chr, (uint8_t *)line, len);
    }
}

static const MemoryRegionOps benchmark_ops = {
    .read = benchmark_read,
    .write = benchmark_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void benchmark_realize(DeviceState *dev, Error **errp)
{
    XboxBenchmarkState *s = XBOX_BENCHMARK(dev);
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        error_setg(
            errp,
            "xbox-benchmark requires a chardev; use a file for measurements");
        return;
    }
    memory_region_init_io(&s->io, OBJECT(dev), &benchmark_ops, s,
                          TYPE_XBOX_BENCHMARK, 16);
    isa_register_ioport(ISA_DEVICE(dev), &s->io, XEMU_BENCHMARK_PORT);
    s->vm_change = qemu_add_vm_change_state_handler(benchmark_state_change, s);
}

static void benchmark_unrealize(DeviceState *dev)
{
    XboxBenchmarkState *s = XBOX_BENCHMARK(dev);
    qemu_del_vm_change_state_handler(s->vm_change);
}

/* Lab runs must start from a cold boot, never a restored measurement. */
static const VMStateDescription benchmark_vmstate = {
    .name = TYPE_XBOX_BENCHMARK,
    .unmigratable = 1,
};

static const Property benchmark_properties[] = {
    DEFINE_PROP_CHR("chardev", XboxBenchmarkState, chr),
};

static void benchmark_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = benchmark_realize;
    dc->unrealize = benchmark_unrealize;
    dc->hotpluggable = false;
    dc->vmsd = &benchmark_vmstate;
    device_class_set_legacy_reset(dc, benchmark_reset);
    device_class_set_props(dc, benchmark_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo benchmark_info = {
    .name = TYPE_XBOX_BENCHMARK,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(XboxBenchmarkState),
    .class_init = benchmark_class_init,
};

static void benchmark_register_types(void)
{
    type_register_static(&benchmark_info);
}
type_init(benchmark_register_types)
