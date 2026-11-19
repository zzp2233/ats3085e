"""Run the V7 token / V8 IRQ / V9 timing helper as ARM code.

This validates token ownership and timeout behavior, not actual wire levels.
"""
from pathlib import Path
import argparse
import re
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(tempfile.gettempdir()) / 'uma503_dma_validation_testdeps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdint.h>
#include <stdbool.h>
#define EPERM 1
#define ENODEV 19
#define EBUSY 16
#define EIO 5
#define ETIMEDOUT 110
#define IS_ENABLED(x) 0
#define CONFIG_GPIO_B_NAME "GPIOB"
#define GPIO_CTL_MFP_MASK 31u
#define GPIO_CTL_GPIO_OUTEN 64u
#define GPIO_CTL_GPIO_INEN 128u
#define GPIO_CTL_INTC_EN (1u << 20)
#define GPION_CTL(x) (0x40000000u + (x)*4u)
#define GPIO_INPUT 1u
#define GPIO_PULL_DOWN 2u
#define GPIO_INT_DISABLE 0u
#define GPIO_INT_EDGE_BOTH 3u
#define K_MSEC(x) (x)
struct device { int unused; };
struct gpio_callback {
    void (*handler)(const struct device *, struct gpio_callback *, uint32_t);
    uint32_t mask;
};
struct k_sem { uint32_t count; };
static struct device device;
static uint32_t now_us, offset_ms, pin_ctl, pin_level, change_at, change_pending;
static uint32_t gpio_missing, gpio_config_error, gpio_read_error, gpio_bad_readback;
static uint32_t clocks, automatic, processing_us, missing_final, count;
static uint32_t waited;
static struct gpio_callback *registered;
static uint32_t irq_enabled, irq_setup_error, add_error, remove_error, disable_error;
static uint32_t race_on_take, spurious, suppress_irq, late_resume, extra_irq;
static void k_sem_init(struct k_sem *s, unsigned int count, unsigned int limit) { (void)limit; s->count=count; }
static void k_sem_give(struct k_sem *s) { s->count=1; }
static void emit_edge(void) {
    if (registered && irq_enabled && !suppress_irq) registered->handler(&device, registered, 1u << 29);
}
static void gpio_init_callback(struct gpio_callback *cb,
    void (*handler)(const struct device *, struct gpio_callback *, uint32_t), uint32_t mask) {
    cb->handler=handler; cb->mask=mask;
}
static int gpio_add_callback(const struct device *d, struct gpio_callback *cb) {
    (void)d; if(add_error) return -5; registered=cb; return 0;
}
static int gpio_remove_callback(const struct device *d, struct gpio_callback *cb) {
    (void)d; (void)cb; if(remove_error) return -5; registered=0; return 0;
}
static int gpio_pin_interrupt_configure(const struct device *d, unsigned int pin, unsigned int mode) {
    (void)d; (void)pin;
    if(mode==GPIO_INT_EDGE_BOTH && irq_setup_error) return -5;
    if(mode==GPIO_INT_DISABLE && disable_error) return -5;
    irq_enabled=mode==GPIO_INT_EDGE_BOTH; return 0;
}
static int k_sem_take(struct k_sem *s, uint32_t ms) {
    uint32_t end=now_us+ms*1000u;
    if(race_on_take) { race_on_take=0; change_pending=0; pin_level^=1; emit_edge(); }
    if(spurious) { now_us+=1000; emit_edge(); }
    if(s->count) { s->count=0; return 0; }
    if(change_pending && change_at<=end) {
        now_us=change_at; pin_level^=1; change_pending=0; emit_edge();
        if(s->count) { s->count=0; now_us+=late_resume; if(extra_irq) emit_edge(); return 0; }
    }
    now_us=end; return -11;
}
static const struct device *device_get_binding(const char *n) { (void)n; return gpio_missing ? 0 : &device; }
static uint32_t sys_read32(uint32_t a) { (void)a; return pin_ctl; }
static void sys_write32(uint32_t v, uint32_t a) { (void)a; pin_ctl = v; }
static int gpio_pin_configure(const struct device *d, unsigned int p, unsigned int f) {
    (void)d; if (p != 29 || f != (GPIO_INPUT|GPIO_PULL_DOWN)) return -5;
    pin_ctl = gpio_bad_readback ? 64 : 128;
    return gpio_config_error ? -5 : 0;
}
static int gpio_pin_get_raw(const struct device *d, unsigned int p) {
    (void)d; (void)p;
    if (gpio_read_error) return -5;
    if (change_pending && now_us >= change_at) { pin_level ^= 1; change_pending = 0; }
    return pin_level;
}
static uint32_t k_uptime_get_32(void) { return offset_ms + now_us/1000; }
static uint32_t k_cycle_get_32(void) { return now_us*100u; }
static uint32_t k_cyc_to_us_floor32(uint32_t cycles) { return cycles/100u; }
#include "spi_link_ready.h"
void probe_reset(void) {
    s_ready_gpio=0; s_ready_saved_ctl=0; s_ready_expected=1;
    s_ready_used=s_ready_legacy_used=s_ready_faulted=s_ready_configured=false;
    now_us=offset_ms=pin_ctl=change_pending=clocks=0; pin_level=1;
    gpio_missing=gpio_config_error=gpio_read_error=gpio_bad_readback=0;
    automatic=1; processing_us=30000; missing_final=0; count=3;
    registered=0; irq_enabled=irq_setup_error=add_error=remove_error=disable_error=0;
    race_on_take=spurious=suppress_irq=late_resume=extra_irq=0; s_ready_callback_added=false;
}
int probe_begin(void) { return swap_ready_begin(); }
int probe_wait(void) { return swap_ready_wait(&waited); }
int probe_file_wait(void) { return swap_ready_wait_timeout(&waited, 2000u); }
int probe_frame(void) {
    int ret=swap_ready_wait(&waited);
    if(ret) return ret;
    swap_ready_consume();
    ++clocks;
    if(automatic) { change_at=now_us+processing_us; change_pending=1; }
    return 0;
}
int probe_command(void) {
    unsigned int i; int ret=swap_ready_begin();
    if(ret) return ret;
    for(i=0;i<count;i++) { ret=probe_frame(); if(ret) break; }
    if(!ret) {
        if(missing_final) change_pending=0;
        ret=swap_ready_wait(&waited);
    }
    return swap_ready_end(ret);
}
int probe_end(void) { return swap_ready_end(0); }
int probe_hint(void) { k_sem_give(&s_ready_sem); return 0; }
int probe_unrelated_irq(void) { swap_ready_irq(&device, &s_ready_callback, 1u<<28); return 0; }
int probe_diag_n(void) { return s_ready_diag.n; }
int probe_diag_pre(void) { return (uint32_t)s_ready_diag.pre_us; }
int probe_diag_post(void) { return (uint32_t)s_ready_diag.post_us; }
int probe_diag_held(void) { return s_ready_diag.held; }
int probe_diag_unpaired(void) { return s_ready_diag.unpaired; }
'''


class Model:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(0x100000, 0x20000)
        self.uc.mem_map(0x20000000, 0x10000)
        self.symbols = {}
        with ELF.open('rb') as f:
            e = ELFFile(f)
            for s in e.iter_sections():
                if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS':
                    self.uc.mem_write(s['sh_addr'], s.data())
            for s in e.get_section_by_name('.symtab').iter_symbols():
                self.symbols[s.name] = (s['st_value'], s['st_size'])
        self.call('probe_reset')

    def get(self, name):
        addr, size = self.symbols[name]
        return int.from_bytes(self.uc.mem_read(addr, size), 'little')

    def set(self, name, value):
        addr, size = self.symbols[name]
        self.uc.mem_write(addr, value.to_bytes(size, 'little'))

    def call(self, name):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2000F000)
        self.uc.reg_write(UC_ARM_REG_LR, 0x110001)
        self.uc.emu_start(self.symbols[name][0] | 1, 0x110000, count=3000000)
        v = self.uc.reg_read(UC_ARM_REG_R0)
        return v - 0x100000000 if v & 0x80000000 else v


class ReadyTests(unittest.TestCase):
    def setUp(self):
        self.m = Model()

    def test_delayed_slave_30ms_allows_exactly_one_frame_per_token(self):
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.get('clocks'), 3)
        self.assertGreaterEqual(self.m.get('now_us'), 90000)
        self.assertEqual(self.m.get('s_ready_expected'), 0)

    def test_repeated_commands_preserve_pending_token(self):
        for _ in range(5):
            self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.get('clocks'), 15)
        self.assertEqual(self.m.get('s_ready_expected'), 0)

    def test_disconnected_pulldown_never_clocks(self):
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 0)
        self.assertGreaterEqual(self.m.get('now_us'), 200000)
        self.assertEqual(self.m.call('probe_command'), -1)

    def test_stuck_high_cannot_reuse_consumed_token(self):
        self.m.set('automatic', 0)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 1)

    def test_final_rearm_timeout_fails_command_and_blocks_later_commands(self):
        self.m.set('missing_final', 1)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 3)
        self.assertEqual(self.m.call('probe_command'), -1)

    def test_gpio_read_error_never_clocks(self):
        self.m.set('gpio_read_error', 1)
        self.assertEqual(self.m.call('probe_command'), -5)
        self.assertEqual(self.m.get('clocks'), 0)

    def test_pinmux_and_output_conflicts_are_rejected(self):
        for ctl in (1, 3, 64, 128 | 64):
            self.m = Model(); self.m.set('pin_ctl', ctl)
            self.assertEqual(self.m.call('probe_begin'), -16)
            self.assertEqual(self.m.get('pin_ctl'), ctl)

    def test_pin_restored_after_success_and_failure(self):
        for fail in (0, 1):
            self.m = Model(); self.m.set('pin_ctl', 0x1000)
            self.m.set('missing_final', fail)
            self.assertEqual(self.m.call('probe_command'), -110 if fail else 0)
            self.assertEqual(self.m.get('pin_ctl'), 0x1000)

    def test_missing_gpio_or_bad_configuration_never_clocks(self):
        for name, error in [('gpio_missing', -19), ('gpio_config_error', -5), ('gpio_bad_readback', -5)]:
            self.m = Model(); self.m.set(name, 1)
            self.assertEqual(self.m.call('probe_command'), error)
            self.assertEqual(self.m.get('clocks'), 0)
            self.assertEqual(self.m.get('pin_ctl'), 0)

    def test_timeout_survives_millisecond_wrap(self):
        self.m.set('offset_ms', 0xFFFFFFFE)
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 0)

    def test_file_wait_allows_slow_ready_but_never_grants_a_missing_token(self):
        for processing, expected in ((350000, 0), (2100000, -110)):
            m = Model()
            m.set('processing_us', processing)
            self.assertEqual(m.call('probe_begin'), 0)
            self.assertEqual(m.call('probe_frame'), 0)
            self.assertEqual(m.call('probe_file_wait'), expected)
            self.assertEqual(m.get('clocks'), 1)

    def test_legacy_use_requires_reboot_before_handshake(self):
        self.m.set('s_ready_legacy_used', 1)
        self.assertEqual(self.m.call('probe_command'), -1)
        self.assertEqual(self.m.get('clocks'), 0)

    def test_wait_without_configuration_is_rejected(self):
        self.assertEqual(self.m.call('probe_wait'), -1)
        self.assertEqual(self.m.get('clocks'), 0)

    def test_shell_gates_before_driver_and_checks_final_rearm(self):
        shell = (ROOT/'TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main/system_app_shell.c').read_text(encoding='utf-8')
        run = shell[shell.index('static int spi2swap_run'):shell.index('static int shell_spi2swap(')]
        wait = run.index('ret = swap_ready_wait(&waited);')
        consume = run.index('swap_ready_consume();')
        transfer = run.index('ret = spi2swap_transfer', consume)
        self.assertLess(wait, consume)
        self.assertLess(consume, transfer)
        self.assertIn('break;', run[wait:consume])
        self.assertIn('if (ready_mode && !ret)', run)
        self.assertIn('swap_ready_end(ret)', run)
        self.assertIn('(!ready_mode && s_ready_used)', run)

    def test_edge_between_read_and_sleep_is_retained(self):
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.m.set('race_on_take', 1)
        self.assertEqual(self.m.call('probe_wait'), 0)
        self.assertEqual(self.m.get('now_us'), 0)

    def test_held_token_before_irq_registration_does_not_need_an_edge(self):
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.assertEqual(self.m.call('probe_wait'), 0)
        self.assertEqual(self.m.get('s_ready_block_count'), 0)

    def test_stale_hint_never_authorizes_clocks(self):
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.m.call('probe_hint')
        self.assertEqual(self.m.call('probe_frame'), -110)
        self.assertEqual(self.m.get('clocks'), 0)

    def test_spurious_irqs_do_not_restart_absolute_deadline(self):
        self.m.set('pin_level', 0); self.m.set('spurious', 1)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('now_us'), 200000)
        self.assertEqual(self.m.get('clocks'), 0)

    def test_submillisecond_edge_wakes_without_tick_rounding(self):
        self.m.set('processing_us', 530)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.get('now_us'), 1590)

    def test_late_task_resume_is_still_bounded_by_original_deadline(self):
        self.m.set('late_resume', 250000)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 1)

    def test_irq_setup_errors_restore_pin_without_clocking(self):
        for name in ('irq_setup_error', 'add_error'):
            self.m=Model(); self.m.set(name,1)
            self.assertEqual(self.m.call('probe_command'), -5)
            self.assertEqual(self.m.get('clocks'),0)
            self.assertEqual(self.m.get('pin_ctl'),0)
            self.assertEqual(self.m.get('registered'),0)

    def test_cleanup_errors_poison_future_commands(self):
        for name in ('remove_error', 'disable_error'):
            self.m=Model(); self.m.set(name,1)
            self.assertEqual(self.m.call('probe_command'), -5)
            self.assertEqual(self.m.call('probe_begin'), -1)

    def test_existing_gpio_irq_owner_is_rejected(self):
        self.m.set('pin_ctl', 1<<20)
        self.assertEqual(self.m.call('probe_begin'), -16)

    def test_unrelated_pin_does_not_notify(self):
        self.assertEqual(self.m.call('probe_begin'),0)
        self.m.call('probe_unrelated_irq')
        self.assertEqual(self.m.get('s_ready_irq_count'),0)

    def test_interrupt_loss_uses_held_level_at_deadline(self):
        self.m.set('suppress_irq',1)
        self.assertEqual(self.m.call('probe_command'),0)
        self.assertEqual(self.m.get('clocks'),3)
        self.assertEqual(self.m.get('s_ready_irq_count'),0)

    def test_success_removes_callback_and_disables_irq(self):
        self.assertEqual(self.m.call('probe_command'),0)
        self.assertEqual(self.m.get('registered'),0)
        self.assertEqual(self.m.get('irq_enabled'),0)

    def test_v9_splits_irq_arrival_from_delayed_task_resume(self):
        self.m.set('processing_us', 600)
        self.m.set('late_resume', 4000)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 3)
        self.assertEqual(self.m.call('probe_diag_pre'), 1800)
        self.assertEqual(self.m.call('probe_diag_post'), 12000)
        self.assertEqual(self.m.call('probe_diag_held'), 1)

    def test_v9_held_token_has_no_invented_irq_latency(self):
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.assertEqual(self.m.call('probe_wait'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 0)
        self.assertEqual(self.m.call('probe_diag_held'), 1)

    def test_v9_missing_irq_uses_level_but_excludes_timing_pair(self):
        self.m.set('suppress_irq', 1)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 0)
        self.assertEqual(self.m.call('probe_diag_unpaired'), 3)

    def test_v9_coalesced_irqs_do_not_fake_scheduler_measurement(self):
        self.m.set('extra_irq', 1)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 0)
        self.assertEqual(self.m.call('probe_diag_unpaired'), 3)

    def test_v9_cycle_counter_wrap_preserves_both_intervals(self):
        self.m.set('now_us', 42949000)
        self.m.set('processing_us', 1000)
        self.m.set('late_resume', 2000)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_pre'), 3000)
        self.assertEqual(self.m.call('probe_diag_post'), 6000)

    def test_v9_diagnostics_reset_per_command_without_resetting_token(self):
        self.m.set('processing_us', 500)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 3)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 3)
        self.assertEqual(self.m.get('clocks'), 6)

    def test_v9_spurious_irq_timeout_never_reports_successful_pair(self):
        self.m.set('pin_level', 0); self.m.set('spurious', 1)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.call('probe_diag_n'), 0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    out = args.build_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    source = out/'ready_harness.c'; source.write_text(HARNESS, encoding='utf-8')
    ELF = out/'ready_harness.elf'
    gcc = Path('D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/startup/app_release/app_tools/cross_tools/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe')
    subprocess.run([str(gcc), '-mcpu=cortex-m4', '-mthumb', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-nostdlib', '-Wl,-e,probe_reset', '-Wl,-Ttext=0x100000',
                    '-Wl,-Tdata=0x20000800', '-Wl,--section-start=.bss=0x20001000',
                    '-I',str(ROOT/'TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main'),
                    str(source), '-o', str(ELF)], check=True)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
