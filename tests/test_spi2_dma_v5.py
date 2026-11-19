"""Run the production SPI2 DMA entry as ARM code with modeled SDK/MMIO.

The SDK calls and peripheral timing are modeled: this is not a hardware test.
Run: python tests/test_spi2_dma_v5.py --build-dir artifacts/spi_speed_v5_20261118/tests
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

deps = Path(tempfile.gettempdir()) / "uma503_dma_validation_testdeps"
if deps.is_dir():
    sys.path.insert(0, str(deps))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

ROOT = Path(__file__).resolve().parents[1]
SPI = 0x40030000
DMA = 0x4001C000

HARNESS = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#define CONFIG_SOC_SERIES_LEOPARD 1
#define CONFIG_PDMA_ACTS 1
#define CONFIG_SRAM_BASE_ADDRESS 0x2ff60400u
#define CONFIG_SRAM_SIZE 911u
#define CONFIG_SPI_2_DMA_ID 9
#define CONFIG_DMA_0_NAME "DMA_0"
#define DMA_REG_BASE 0x4001c000u
#define CLOCK_ID_SPI2 2
#define SPI_OP_MODE_MASTER 0
#define SPI_MODE_CPOL 2
#define SPI_MODE_CPHA 4
#define SPI_WORD_SET(n) ((n) << 5)
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#define ARG_UNUSED(a) (void)(a)
struct spi_config { uint32_t frequency, operation, slave; const void *cs; };
struct k_sem { int unused; };
struct spi_context { const struct spi_config *config; struct k_sem lock; };
struct device { const void *config; void *data; };
#include "spi_types.h"
enum { MEMORY_TO_PERIPHERAL=1, PERIPHERAL_TO_MEMORY=2 };
struct dma_block_config { uint32_t block_size, source_address, dest_address; };
struct dma_config {
    uint32_t block_count, dma_slot, channel_direction;
    uint32_t source_data_size, dest_data_size, source_burst_length, dest_burst_length;
    struct dma_block_config *head_block;
};
struct dma_status { bool busy; uint32_t dir, pending_length; };
static struct device dma_dev;
static struct acts_spi_config dev_cfg = { .spi=(void *)0x40030000, .clock_id=2 };
static struct acts_spi_data dev_data;
static struct device dev = { &dev_cfg, &dev_data };
static uint8_t input[512], output[512];
int fault, allocations, frees, stops, starts, configs, locks, invariant_error;
uint32_t cycles, events[64], event_count, source[2], dest[2];
uint32_t input_len, input_hz, input_op, input_slave, input_cs;
static void event(uint32_t e) { if (event_count < 64) events[event_count++] = e; }
static uint32_t reg(int ch, int off) { return DMA_REG_BASE + (ch+1)*0x100 + off; }
uint32_t sys_read32(uint32_t a) { return *(volatile uint32_t *)a; }
static void wr(uint32_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
uint32_t k_cycle_get_32(void) { cycles += 8000; return cycles; }
uint32_t k_cyc_to_us_floor32(uint32_t c) { return c / 32u; }
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; return d;
}
void *memset(void *d, int v, size_t n) {
    uint8_t *a=d; while(n--) *a++=v; return d;
}
int printk(const char *s, ...) { (void)s; return 0; }
void spi_context_lock(struct spi_context *c, bool a, void *b) {
    (void)c; (void)a; (void)b; ++locks;
}
void k_sem_give(struct k_sem *s) { (void)s; --locks; }
const struct device *device_get_binding(const char *s) { (void)s; return fault == 17 ? NULL : &dma_dev; }
void acts_reset_peripheral(uint32_t id) { (void)id; event(500); }
int spi_acts_configure(const struct acts_spi_config *c, struct acts_spi_data *d,
                       const struct spi_config *s) {
    if (fault == 18) return -EIO;
    d->ctx.config=s;
    c->spi->ctrl=SPI_CTL_SS | SPI_CTL_MODE_CPHA | SPI_CTL_MODE_CPOL;
    c->spi->status=SPI_STATUS_TX_EMPTY | SPI_STATUS_RX_EMPTY;
    return 0;
}
int dma_request(const struct device *d, uint32_t wanted) {
    int n=allocations++;
    (void)d;
    if (wanted != 0xff) invariant_error=1;
    event(100+n);
    if ((fault==1 && n==0) || (fault==2 && n==1)) return -ENOMEM;
    return n;
}
int dma_config(const struct device *d, uint32_t ch, struct dma_config *c) {
    (void)d; ++configs; event(200+ch);
    if ((fault==3 && ch==0) || (fault==4 && ch==1)) return -EIO;
    if (c->block_count!=1 || c->dma_slot!=9 || c->head_block->block_size!=input_len ||
        c->source_data_size!=1 || c->dest_data_size!=1 ||
        c->source_burst_length!=8 || c->dest_burst_length!=8 ||
        c->channel_direction!=(ch==0 ? PERIPHERAL_TO_MEMORY : MEMORY_TO_PERIPHERAL)) invariant_error=1;
    source[ch]=c->head_block->source_address; dest[ch]=c->head_block->dest_address;
    if ((ch==0 && source[ch]!=0x4003000c) || (ch==1 && dest[ch]!=0x40030008)) invariant_error=1;
    wr(reg(ch,0x1c),input_len);
    return 0;
}
int dma_start(const struct device *d, uint32_t ch) {
    (void)d; ++starts; event(300+ch);
    if (!(dev_cfg.spi->ctrl & SPI_CTL_SS) ||
        (dev_cfg.spi->ctrl & (SPI_CTL_TX_DRQ_EN|SPI_CTL_RX_DRQ_EN))) invariant_error=1;
    if ((fault==5 && ch==0) || (fault==6 && ch==1)) return -EIO;
    wr(reg(ch,4),1); return 0;
}
int dma_get_status(const struct device *d, uint32_t ch, struct dma_status *s) {
    uint32_t sta=SPI_STATUS_TX_EMPTY | SPI_STATUS_RX_EMPTY;
    unsigned int i;
    (void)d;
    if ((fault==14 && ch==0) || (fault==15 && ch==1)) return -EIO;
    if (fault!=7 && !(fault==9 && ch==1)) {
        wr(reg(ch,4),0); wr(reg(ch,0x1c),(fault==8 && ch==0) ? 3 : ((fault==19 && ch==0) ? 256 : 0));
        if (ch==0) for(i=0;i<input_len;++i) ((uint8_t *)dest[0])[i]=((uint8_t *)source[1])[i]^0x55;
    }
    if (fault==10) sta|=SPI_STATUS_RX_FIFO_WERR;
    if (fault==11) sta&=~SPI_STATUS_TX_EMPTY;
    if (fault==12) sta|=SPI_STATUS_BUSY;
    if (fault==13) sta&=~SPI_STATUS_RX_EMPTY;
    dev_cfg.spi->status=sta;
    s->busy=sys_read32(reg(ch,4))!=0;
    s->pending_length=s->busy ? sys_read32(reg(ch,0x1c)) : 0;
    return 0;
}
int dma_stop(const struct device *d, uint32_t ch) {
    (void)d; ++stops; event(400+ch);
    if (dev_cfg.spi->ctrl & (SPI_CTL_RX_DRQ_EN|SPI_CTL_TX_DRQ_EN)) invariant_error=1;
    wr(reg(ch,4),fault==16 ? 1 : 0); return 0;
}
void dma_free(const struct device *d, uint32_t ch) {
    (void)d; ++frees; event(450+ch);
    if (sys_read32(reg(ch,4))) invariant_error=1;
}
#include "spi_acts_spi2_dma.h"
void probe_reset(void) {
    unsigned int i;
    fault=allocations=frees=stops=starts=configs=locks=invariant_error=0;
    event_count=cycles=0; spi2_dma_faulted=false;
    memset(&dev_data,0,sizeof(dev_data));
    dev_cfg.clock_id=2; dev_cfg.spi_3wire=dev_cfg.flag_use_dma=0;
    input_len=256; input_hz=2300000; input_op=SPI_MODE_CPOL|SPI_MODE_CPHA|SPI_WORD_SET(8);
    input_slave=input_cs=0;
    for(i=0;i<512;++i) input[i]=(uint8_t)(i ^ (i >> 8));
    memset(output,0x77,sizeof(output));
    for(i=0;i<2;++i) { wr(reg(i,4),0); wr(reg(i,0x1c),256); }
    dev_cfg.spi->ctrl=SPI_CTL_SS; dev_cfg.spi->status=0x50;
}
int probe_run(void) {
    struct spi_config c={input_hz,input_op,input_slave,(void *)input_cs};
    return spi_acts_spi2_transceive_dma(&dev,&c,input,output,input_len);
}
int probe_timeout_errno(void) { return -ETIMEDOUT; }
'''


class Model:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(0x100000, 0x20000)
        self.uc.mem_map(0x2FF60000, 0x100000)
        self.uc.mem_map(SPI, 0x1000)
        self.uc.mem_map(DMA, 0x1000)
        self.sym = {}
        with ELF.open("rb") as f:
            elf = ELFFile(f)
            for s in elf.iter_sections():
                if s["sh_flags"] & 2 and s["sh_type"] != "SHT_NOBITS":
                    self.uc.mem_write(s["sh_addr"], s.data())
            self.sym = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
        self.clock_starts = 0
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)
        self.call("probe_reset")

    def read(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def get(self, name):
        return self.read(self.sym[name])

    def set(self, name, value):
        self.uc.mem_write(self.sym[name], struct.pack("<I", value & 0xffffffff))

    def call(self, name):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2FFEF000)
        self.uc.reg_write(UC_ARM_REG_LR, 0x110001)
        self.uc.emu_start(self.sym[name] | 1, 0x110000, count=2000000)
        value = self.uc.reg_read(UC_ARM_REG_R0)
        return value - 0x100000000 if value & 0x80000000 else value

    def on_write(self, uc, access, address, size, value, data):
        if address == SPI and value & 0xc0 == 0xc0 and value & 3 == 3:
            assert not value & 8, "CS must be active before clocks"
            assert self.read(DMA + 0x104) == self.read(DMA + 0x204) == 1
            self.clock_starts += 1

    def output(self):
        return bytes(self.uc.mem_read(self.sym["output"], self.get("input_len")))


class DmaTests(unittest.TestCase):
    def setUp(self):
        self.m = Model()

    def clean(self, channels):
        self.assertEqual(self.m.get("locks"), 0)
        self.assertEqual(self.m.get("invariant_error"), 0)
        self.assertEqual(self.m.get("frees"), channels)
        self.assertTrue(self.m.read(SPI) & 8)
        self.assertFalse(self.m.read(SPI) & 0xc0)

    def test_success_copies_full_duplex_data_after_both_completions(self):
        self.assertEqual(self.m.call("probe_run"), 0)
        self.assertEqual(self.m.output(), bytes(i ^ 0x55 for i in range(256)))
        self.assertEqual(self.m.clock_starts, 1)
        events = [self.m.read(self.m.sym["events"] + i * 4) for i in range(self.m.get("event_count"))]
        self.assertLess(events.index(300), events.index(301))
        self.assertLess(events.index(401), events.index(450))
        self.clean(2)

    def test_alloc_failures_do_not_start_clocks_or_leak(self):
        for fault, channels in [(1, 0), (2, 1)]:
            with self.subTest(fault=fault):
                self.m = Model(); self.m.set("fault", fault)
                self.assertLess(self.m.call("probe_run"), 0)
                self.assertEqual(self.m.clock_starts, 0)
                self.clean(channels)

    def test_configuration_start_and_status_errors_propagate(self):
        for fault in (3, 4, 5, 6, 14, 15, 18):
            with self.subTest(fault=fault):
                self.m = Model(); self.m.set("fault", fault)
                self.assertLess(self.m.call("probe_run"), 0)
                self.assertEqual(self.m.output(), b"\x77" * 256)
                self.clean(2)

    def test_stalled_dma_and_nonidle_spi_have_bounded_timeouts(self):
        for fault in (7, 9, 11, 12):
            with self.subTest(fault=fault):
                self.m = Model(); self.m.set("fault", fault)
                self.assertEqual(self.m.call("probe_run"), self.m.call("probe_timeout_errno"))
                self.assertGreaterEqual(self.m.get("cycles") // 32, 40000)
                self.assertLessEqual(self.m.get("cycles") // 32, 42000)
                self.assertEqual(self.m.output(), b"\x77" * 256)
                self.clean(2)

    def test_stopped_with_nonzero_hardware_count_is_rejected(self):
        self.m.set("fault", 8)
        self.assertLess(self.m.call("probe_run"), 0)
        self.assertEqual(self.m.output(), b"\x77" * 256)
        self.clean(2)

    def test_fifo_error_and_extra_rx_fail_without_copy(self):
        for fault in (10, 13):
            with self.subTest(fault=fault):
                self.m = Model(); self.m.set("fault", fault)
                self.assertLess(self.m.call("probe_run"), 0)
                self.assertEqual(self.m.output(), b"\x77" * 256)
                self.clean(2)

    def test_stop_failure_quarantines_buffers_and_channels(self):
        self.m.set("fault", 16)
        self.assertLess(self.m.call("probe_run"), 0)
        self.assertEqual(self.m.get("frees"), 0)
        self.assertEqual(self.m.get("locks"), 0)
        self.assertEqual(self.m.output(), b"\x77" * 256)
        allocations = self.m.get("allocations")
        self.m.set("fault", 0)
        self.assertLess(self.m.call("probe_run"), 0)
        self.assertEqual(self.m.get("allocations"), allocations)

    def test_invalid_parameters_are_rejected_before_hardware(self):
        for field, value in [("input_len", 0), ("input_len", 255), ("input_len", 257),
                             ("input_hz", 99999), ("input_hz", 8000001),
                             ("input_op", 0), ("input_slave", 1), ("input_cs", 1)]:
            with self.subTest(field=field, value=value):
                self.m = Model(); self.m.set(field, value)
                self.assertEqual(self.m.call("probe_run"), -22)
                self.assertEqual(self.m.get("allocations"), 0)

    def test_missing_dma_device_is_rejected(self):
        self.m.set("fault", 17)
        self.assertEqual(self.m.call("probe_run"), -19)
        self.assertEqual(self.m.get("allocations"), 0)

    def test_timeout_survives_cycle_counter_wrap(self):
        self.m.set("fault", 7); self.m.set("cycles", 0xfffff000)
        self.assertEqual(self.m.call("probe_run"), self.m.call("probe_timeout_errno"))
        self.clean(2)

    def test_block2_512_bytes_use_one_cs_and_one_dma_pair(self):
        self.m.set('input_len', 512)
        self.assertEqual(self.m.call('probe_run'), 0)
        self.assertEqual(self.m.output(), bytes((i ^ (i >> 8) ^ 0x55) & 255 for i in range(512)))
        self.assertEqual(self.m.clock_starts, 1)
        self.assertEqual(self.m.get('configs'), 2)
        self.assertEqual(self.m.get('starts'), 2)
        self.clean(2)

    def test_block2_only_first_half_complete_is_rejected_without_copy(self):
        self.m.set('input_len', 512); self.m.set('fault', 19)
        self.assertLess(self.m.call('probe_run'), 0)
        self.assertEqual(self.m.output(), b'\x77' * 512)
        self.clean(2)

    def test_block2_stop_failure_quarantines_full_size_buffers(self):
        self.m.set('input_len', 512); self.m.set('fault', 16)
        self.assertLess(self.m.call('probe_run'), 0)
        self.assertEqual(self.m.get('frees'), 0)
        self.assertEqual(self.m.output(), b'\x77' * 512)

    def test_block2_rejects_nearby_unsupported_lengths_before_clocks(self):
        for n in (511, 513, 1024):
            self.m = Model(); self.m.set('input_len', n)
            self.assertEqual(self.m.call('probe_run'), -22)
            self.assertEqual(self.m.clock_starts, 0)
            self.assertEqual(self.m.get('allocations'), 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=Path(
        "D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/startup/app_release/app_tools/"
        "cross_tools/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe"))
    args = parser.parse_args()
    out = args.build_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    driver = ROOT / "TAG_ZS3085E_241230/zephyr/drivers/spi"
    original = (driver / "spi_acts.c").read_text(encoding="utf-8")
    begin = original.index("#define SPI_CTL_CLK_SEL_MASK")
    end = original.rfind("/**", 0, original.index("bool spi_acts_transfer_ongoing"))
    (out / "spi_types.h").write_text(original[begin:end], encoding="utf-8")
    source = out / "master_harness.c"; source.write_text(HARNESS, encoding="utf-8")
    ELF = out / "master_harness.elf"
    subprocess.run([str(args.compiler), "-mcpu=cortex-m4", "-mthumb", "-O0", "-Wall", "-Wextra",
                    "-Werror", "-fno-builtin", "-ffunction-sections", "-fdata-sections", "-nostdlib",
                    "-Wl,--gc-sections", "-Wl,--undefined=probe_reset", "-Wl,--undefined=probe_run",
                    "-Wl,--undefined=probe_timeout_errno",
                    "-Wl,-e,probe_reset", "-Wl,-Ttext=0x100000", "-Wl,-Tdata=0x2ff70000",
                    "-Wl,--section-start=.bss=0x2ff71000", "-I", str(driver), "-I", str(out),
                    str(source), "-o", str(ELF)], check=True)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
