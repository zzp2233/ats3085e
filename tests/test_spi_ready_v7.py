# 本文件在 PC 上编译并仿真运行 ATS 的 GPIO READY 握手代码，通过可控边沿、错误和时间验证每个授权能启动几笔 SPI 事务。
"""在 ARM 仿真中运行 V7 就绪令牌、V8 中断和 V9 计时逻辑，检查令牌归属与超时行为。实际线路电平需要在板上测量。
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
# HARNESS 是传给 ARM GCC 的 C 源码。它模拟 GPIO 输入、回调、信号量和时间，在同一编译单元中包含实际的 spi_link_ready.h。
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


# 每个 Model 拥有独立的仿真内存和符号表，用于从 Python 驱动一次实验并读取结果。
class Model:
    # 装载 ELF 中可分配的节，按符号地址定位 C 变量和探针，最后在模拟器中执行复位。
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

    # 按 ELF 符号大小读取模型内存，按小端序解码无符号整数。
    def get(self, name):
        addr, size = self.symbols[name]
        return int.from_bytes(self.uc.mem_read(addr, size), 'little')

    # 把给定值写入同名符号，使下次调用遇到测试指定的输入电平、延迟或错误。
    def set(self, name, value):
        addr, size = self.symbols[name]
        self.uc.mem_write(addr, value.to_bytes(size, 'little'))

    # 设置栈和 Thumb 返回地址，从目标符号开始仿真，用 R0 取回有符号的 C 返回值。
    def call(self, name):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2000F000)
        self.uc.reg_write(UC_ARM_REG_LR, 0x110001)
        self.uc.emu_start(self.symbols[name][0] | 1, 0x110000, count=3000000)
        v = self.uc.reg_read(UC_ARM_REG_R0)
        return v - 0x100000000 if v & 0x80000000 else v


# 下列用例通过 clocks 验证授权，通过时间与诊断计数区分 IRQ 到达和任务被调度。
class ReadyTests(unittest.TestCase):
    # 每个用例重新创建模型，避免上一个故障或挂起的 READY 状态影响本次断言。
    def setUp(self):
        self.m = Model()

    # 从机延迟 30 ms，每次交还一个有效授权后主机才能再发一笔。
    def test_delayed_slave_30ms_allows_exactly_one_frame_per_token(self):
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.get('clocks'), 3)
        self.assertGreaterEqual(self.m.get('now_us'), 90000)
        self.assertEqual(self.m.get('s_ready_expected'), 0)

    # 跨连续命令保留引脚期望值，验证同一个电平不会被重复消费。
    def test_repeated_commands_preserve_pending_token(self):
        for _ in range(5):
            self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.get('clocks'), 15)
        self.assertEqual(self.m.get('s_ready_expected'), 0)

    # 断开时输入由下拉保持，超时前后都不得发时钟。
    def test_disconnected_pulldown_never_clocks(self):
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 0)
        self.assertGreaterEqual(self.m.get('now_us'), 200000)
        self.assertEqual(self.m.call('probe_command'), -1)

    # 上一授权已消耗后，即便引脚卡在高电平也不能凭它再次启动。
    def test_stuck_high_cannot_reuse_consumed_token(self):
        self.m.set('automatic', 0)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 1)

    # 最后一次重新授权超时应向调用者报错，并阻止后续命令带病继续。
    def test_final_rearm_timeout_fails_command_and_blocks_later_commands(self):
        self.m.set('missing_final', 1)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 3)
        self.assertEqual(self.m.call('probe_command'), -1)

    # 模拟读 GPIO 失败，确认没有把错误返回值视为授权电平。
    def test_gpio_read_error_never_clocks(self):
        self.m.set('gpio_read_error', 1)
        self.assertEqual(self.m.call('probe_command'), -5)
        self.assertEqual(self.m.get('clocks'), 0)

    # 审核引脚复用与方向，已有外设或输出占用时应拒绝接管。
    def test_pinmux_and_output_conflicts_are_rejected(self):
        for ctl in (1, 3, 64, 128 | 64):
            self.m = Model(); self.m.set('pin_ctl', ctl)
            self.assertEqual(self.m.call('probe_begin'), -16)
            self.assertEqual(self.m.get('pin_ctl'), ctl)

    # 对比进入和退出时的引脚设置，覆盖执行成功和失败两条还原路径。
    def test_pin_restored_after_success_and_failure(self):
        for fail in (0, 1):
            self.m = Model(); self.m.set('pin_ctl', 0x1000)
            self.m.set('missing_final', fail)
            self.assertEqual(self.m.call('probe_command'), -110 if fail else 0)
            self.assertEqual(self.m.get('pin_ctl'), 0x1000)

    # GPIO 设备缺失、配置失败或回读不符时都应在发生时钟前退出。
    def test_missing_gpio_or_bad_configuration_never_clocks(self):
        for name, error in [('gpio_missing', -19), ('gpio_config_error', -5), ('gpio_bad_readback', -5)]:
            self.m = Model(); self.m.set(name, 1)
            self.assertEqual(self.m.call('probe_command'), error)
            self.assertEqual(self.m.get('clocks'), 0)
            self.assertEqual(self.m.get('pin_ctl'), 0)

    # 在毫秒计数接近回绕时开始等待，检验相对超时仍有界。
    def test_timeout_survives_millisecond_wrap(self):
        self.m.set('offset_ms', 0xFFFFFFFE)
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 0)

    # 文件传输能等待较慢的 READY；从机始终不交还授权仍会超时。
    def test_file_wait_allows_slow_ready_but_never_grants_a_missing_token(self):
        for processing, expected in ((350000, 0), (2100000, -110)):
            m = Model()
            m.set('processing_us', processing)
            self.assertEqual(m.call('probe_begin'), 0)
            self.assertEqual(m.call('probe_frame'), 0)
            self.assertEqual(m.call('probe_file_wait'), expected)
            self.assertEqual(m.get('clocks'), 1)

    # 传统路径使用后，检查驱动要求重启的限制确实挡住握手命令。
    def test_legacy_use_requires_reboot_before_handshake(self):
        self.m.set('s_ready_legacy_used', 1)
        self.assertEqual(self.m.call('probe_command'), -1)
        self.assertEqual(self.m.get('clocks'), 0)

    # 确认未配置 GPIO 时不能直接等待授权。
    def test_wait_without_configuration_is_rejected(self):
        self.assertEqual(self.m.call('probe_wait'), -1)
        self.assertEqual(self.m.get('clocks'), 0)

    # 读取应用源码核对 READY 门控、驱动调用和最后一次重新授权的顺序。
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

    # 在读取引脚与开始睡眠之间注入边沿，确认已送达的唤醒提示不会丢失。
    def test_edge_between_read_and_sleep_is_retained(self):
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.m.set('race_on_take', 1)
        self.assertEqual(self.m.call('probe_wait'), 0)
        self.assertEqual(self.m.get('now_us'), 0)

    # 注册 IRQ 前已有的有效电平也能被消费，避免要求对端人为再翻转一次。
    def test_held_token_before_irq_registration_does_not_need_an_edge(self):
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.assertEqual(self.m.call('probe_wait'), 0)
        self.assertEqual(self.m.get('s_ready_block_count'), 0)

    # 信号量的旧提示不能代替本次所需的引脚电平。
    def test_stale_hint_never_authorizes_clocks(self):
        self.m.set('pin_level', 0)
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.m.call('probe_hint')
        self.assertEqual(self.m.call('probe_frame'), -110)
        self.assertEqual(self.m.get('clocks'), 0)

    # 连续注入伪 IRQ 来验证等待沿用同一绝对截止时间。
    def test_spurious_irqs_do_not_restart_absolute_deadline(self):
        self.m.set('pin_level', 0); self.m.set('spurious', 1)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('now_us'), 200000)
        self.assertEqual(self.m.get('clocks'), 0)

    # 让边沿在一毫秒内到来，检查中断路径无须一直等到轮询周期。
    def test_submillisecond_edge_wakes_without_tick_rounding(self):
        self.m.set('processing_us', 530)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.get('now_us'), 1590)

    # 人为延迟任务恢复，以原有期限判断是否超时。
    def test_late_task_resume_is_still_bounded_by_original_deadline(self):
        self.m.set('late_resume', 250000)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.get('clocks'), 1)

    # 分别注入 IRQ 配置失败，测试不发时钟且还原引脚。
    def test_irq_setup_errors_restore_pin_without_clocking(self):
        for name in ('irq_setup_error', 'add_error'):
            self.m=Model(); self.m.set(name,1)
            self.assertEqual(self.m.call('probe_command'), -5)
            self.assertEqual(self.m.get('clocks'),0)
            self.assertEqual(self.m.get('pin_ctl'),0)
            self.assertEqual(self.m.get('registered'),0)

    # 在清理回调或禁用 IRQ 时产生错误，验证后续命令受故障保护。
    def test_cleanup_errors_poison_future_commands(self):
        for name in ('remove_error', 'disable_error'):
            self.m=Model(); self.m.set(name,1)
            self.assertEqual(self.m.call('probe_command'), -5)
            self.assertEqual(self.m.call('probe_begin'), -1)

    # 模拟引脚已有中断使用者，检查任务拒绝覆盖其配置。
    def test_existing_gpio_irq_owner_is_rejected(self):
        self.m.set('pin_ctl', 1<<20)
        self.assertEqual(self.m.call('probe_begin'), -16)

    # 产生其他引脚的回调，确认不会授予当前 READY。
    def test_unrelated_pin_does_not_notify(self):
        self.assertEqual(self.m.call('probe_begin'),0)
        self.m.call('probe_unrelated_irq')
        self.assertEqual(self.m.get('s_ready_irq_count'),0)

    # 丢失中断通知，验证截止时仍能读取保持的有效电平。
    def test_interrupt_loss_uses_held_level_at_deadline(self):
        self.m.set('suppress_irq',1)
        self.assertEqual(self.m.call('probe_command'),0)
        self.assertEqual(self.m.get('clocks'),3)
        self.assertEqual(self.m.get('s_ready_irq_count'),0)

    # 正常退出后检查回调移除与中断禁用。
    def test_success_removes_callback_and_disables_irq(self):
        self.assertEqual(self.m.call('probe_command'),0)
        self.assertEqual(self.m.get('registered'),0)
        self.assertEqual(self.m.get('irq_enabled'),0)

    # 在边沿和任务恢复之间加入延迟，检查诊断独立记录两个时间点。
    def test_v9_splits_irq_arrival_from_delayed_task_resume(self):
        self.m.set('processing_us', 600)
        self.m.set('late_resume', 4000)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 3)
        self.assertEqual(self.m.call('probe_diag_pre'), 1800)
        self.assertEqual(self.m.call('probe_diag_post'), 12000)
        self.assertEqual(self.m.call('probe_diag_held'), 1)

    # 授权在等待前已经存在，此次没有可用的 IRQ 到任务延迟样本。
    def test_v9_held_token_has_no_invented_irq_latency(self):
        self.assertEqual(self.m.call('probe_begin'), 0)
        self.assertEqual(self.m.call('probe_wait'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 0)
        self.assertEqual(self.m.call('probe_diag_held'), 1)

    # IRQ 丢失时可以按电平消费授权，该笔不纳入成对 IRQ 耗时统计。
    def test_v9_missing_irq_uses_level_but_excludes_timing_pair(self):
        self.m.set('suppress_irq', 1)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 0)
        self.assertEqual(self.m.call('probe_diag_unpaired'), 3)

    # 多次 IRQ 合并后，无法归属到唯一一次唤醒的样本不能充作调度耗时。
    def test_v9_coalesced_irqs_do_not_fake_scheduler_measurement(self):
        self.m.set('extra_irq', 1)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 0)
        self.assertEqual(self.m.call('probe_diag_unpaired'), 3)

    # 设置周期计数接近回绕，验证两段时间差仍按无符号差值计算。
    def test_v9_cycle_counter_wrap_preserves_both_intervals(self):
        self.m.set('now_us', 42949000)
        self.m.set('processing_us', 1000)
        self.m.set('late_resume', 2000)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_pre'), 3000)
        self.assertEqual(self.m.call('probe_diag_post'), 6000)

    # 新命令清零本次诊断计数，仍继承链路尚未消费的授权状态。
    def test_v9_diagnostics_reset_per_command_without_resetting_token(self):
        self.m.set('processing_us', 500)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 3)
        self.assertEqual(self.m.call('probe_command'), 0)
        self.assertEqual(self.m.call('probe_diag_n'), 3)
        self.assertEqual(self.m.get('clocks'), 6)

    # 只有伪中断且最终超时的事务不会产生成功样本。
    def test_v9_spurious_irq_timeout_never_reports_successful_pair(self):
        self.m.set('pin_level', 0); self.m.set('spurious', 1)
        self.assertEqual(self.m.call('probe_command'), -110)
        self.assertEqual(self.m.call('probe_diag_n'), 0)


# 将固定的 C 测试夹具写入调用者指定的构建目录，编译为 Thumb ELF 后运行整组用例。
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
