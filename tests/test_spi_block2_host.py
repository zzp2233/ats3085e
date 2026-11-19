"""Execute the production host command with a modeled peer, not a copied loop.

Independent tests cover real DMA MMIO and READY IRQ behavior in other suites.
These tests exercise block counting, paired ACK rejection, and command safety.
"""
from pathlib import Path
import argparse
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
MAIN = ROOT / 'TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main'
HARNESS = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include "spi_link_block.h"
#define EINVAL 22
#define EPERM 1
#define ENODEV 19
#define EIO 5
#define SPI_OP_MODE_MASTER 0u
#define SPI_MODE_CPOL 2u
#define SPI_MODE_CPHA 4u
#define SPI_WORD_SET(x) ((x)<<8)
#define FULL_WAKE_LOCK 1
#define CONFIG_SPI_2_NAME "SPI2"
#define SPI_ACTS_SPI2_BURST_DEFAULT 8u
#define SPI_ACTS_SPI2_BURST_MAX 16u
struct shell { int unused; };
struct device { int unused; };
struct spi_config { uint32_t frequency, operation; };
struct spi_buf { void *buf; size_t len; };
struct spi_buf_set { struct spi_buf *buffers; size_t count; };
static struct device device;
static struct shell shell;
static bool s_ready_used, s_ready_legacy_used, s_ready_faulted;
static unsigned int s_ready_expected = 1;
static uint32_t s_ready_irq_count, s_ready_block_count;
static uint64_t s_ready_block_us;
static struct { uint64_t pre_us, post_us; uint32_t n, pre_max, post_max, held, unpaired; } s_ready_diag;
uint32_t now_us, transfers, waits, consumed, token=1, locks, invariant_error;
uint32_t fail_wait_at, bad_reply_at, bad_reply_kind, driver_fail_at;
uint32_t logged_good, logged_logical, logged_attempts, logged_target, logged_planned;
static uint8_t reply[512];
char arg_count[16]="1", arg_gap[16]="0", arg_hz[16]="4000000";
void *memset(void *p,int c,size_t n){unsigned char *s=p;while(n--)*s++=c;return p;}
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
static int prefix(const char *a,const char *b){while(*b)if(*a++!=*b++)return 0;return 1;}
static unsigned long strtoul(const char *s,char **end,int base){unsigned long v=0;(void)base;while(*s>='0'&&*s<='9')v=v*10+*s++-'0';*end=(char*)s;return v;}
static void shell_print(const struct shell *s,const char *fmt,...){
    va_list a;(void)s;va_start(a,fmt);
    if(prefix(fmt,"SPI10 RESULT")){
        (void)va_arg(a,unsigned int);logged_good=va_arg(a,unsigned int);
        logged_logical=va_arg(a,unsigned int);logged_attempts=va_arg(a,unsigned int);
    } else if(prefix(fmt,"SPI SWAP total=")){
        (void)va_arg(a,unsigned int);logged_target=va_arg(a,unsigned int);
        logged_planned=va_arg(a,unsigned int);
    }
    va_end(a);
}
static void shell_error(const struct shell *s,const char *fmt,...){(void)s;(void)fmt;}
static void shell_hexdump(const struct shell *s,const void *p,size_t n){(void)s;(void)p;(void)n;}
static const struct device *device_get_binding(const char *s){(void)s;return &device;}
static void sys_wake_lock(int n){(void)n;++locks;}
static void sys_wake_unlock(int n){(void)n;--locks;}
static uint32_t k_uptime_get_32(void){return now_us/1000;}
static uint32_t k_cycle_get_32(void){return now_us;}
static uint32_t k_cyc_to_us_floor32(uint32_t n){return n;}
static void k_msleep(uint32_t n){now_us+=n*1000;}
static int swap_ready_begin(void){if(s_ready_faulted)return -EPERM;s_ready_used=true;return 0;}
static int swap_ready_wait(uint32_t *waited){
    *waited=0;++waits;if(waits==fail_wait_at)return -110;
    if(!token){++invariant_error;return -110;}return 0;
}
static void swap_ready_consume(void){if(!token)++invariant_error;token=0;++consumed;s_ready_expected^=1;}
static int swap_ready_end(int ret){if(ret)s_ready_faulted=true;return ret;}
static int spi_acts_spi2_transceive_dma(const struct device *d,const struct spi_config *c,
                                       const uint8_t *tx,uint8_t *rx,size_t len){
    (void)d;(void)c;++transfers;
    if(token||consumed!=transfers||len!=512||swap_block_check(tx,SWAP_MASTER))++invariant_error;
    if(transfers==driver_fail_at)return -EIO;
    if(transfers==1)swap_block_frame(reply,SWAP_SLAVE,0,SWAP_BLOCK_IDLE);
    memcpy(rx,reply,512);
    if(transfers==bad_reply_at){
        if(bad_reply_kind==1)rx[256+100]^=1;
        else if(bad_reply_kind==2)swap_frame(rx+256,SWAP_SLAVE,swap_u16(rx+2)+1,swap_u16(rx+260));
        else if(bad_reply_kind==3)swap_frame(rx+256,SWAP_SLAVE,swap_u16(rx+2),swap_u16(rx+260)+2);
        else if(bad_reply_kind==4)swap_block_frame(rx,SWAP_SLAVE,swap_u16(rx+2),swap_block_sequence(rx)+1);
        else if(bad_reply_kind==5)rx[10]^=1;
    }
    swap_block_frame(reply,SWAP_SLAVE,swap_u16(tx+2),swap_block_sequence(tx));
    token=1;now_us+=1024;return 0;
}
static int spi_acts_spi2_gpio_transceive(const struct device *d,const void *t,void *r,size_t n,uint32_t hz){(void)d;(void)t;(void)r;(void)n;(void)hz;return -EIO;}
static int spi_acts_spi2_transceive_paced(const struct device *d,const struct spi_config *c,const struct spi_buf_set *t,const struct spi_buf_set *r,unsigned int gap,unsigned int bytes){(void)d;(void)c;(void)t;(void)r;(void)gap;(void)bytes;return -EIO;}
static int spi_transceive(const struct device *d,const struct spi_config *c,const struct spi_buf_set *t,const struct spi_buf_set *r){(void)d;(void)c;(void)t;(void)r;return -EIO;}
/* PRODUCTION_COMMAND */
int probe_block(void){char *a[]={"spi2swapblock2",arg_count,arg_gap,arg_hz};return spi2swap_run(&shell,4,a,false,true,true,true);}
int probe_legacy(void){char *a[]={"spi2swapready",arg_count,arg_gap,arg_hz};return spi2swap_run(&shell,4,a,false,true,true,false);}
'''


class Model:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(0x100000, 0x20000)
        self.uc.mem_map(0x20000000, 0x10000)
        with ELF.open('rb') as f:
            e = ELFFile(f)
            for s in e.iter_sections():
                if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS':
                    self.uc.mem_write(s['sh_addr'], s.data())
            self.sym = {s.name: s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}

    def get(self, name):
        return struct.unpack('<I', self.uc.mem_read(self.sym[name], 4))[0]

    def set(self, name, value):
        self.uc.mem_write(self.sym[name], struct.pack('<I', value))

    def arg(self, name, value):
        self.uc.mem_write(self.sym[name], str(value).encode() + b'\0')

    def call(self, name='probe_block', instruction_limit=10000000):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2000f000)
        self.uc.reg_write(UC_ARM_REG_LR, 0x110001)
        self.uc.emu_start(self.sym[name] | 1, 0x110000, count=instruction_limit)
        value = self.uc.reg_read(UC_ARM_REG_R0)
        return value - 2**32 if value & 2**31 else value


class HostBlockTests(unittest.TestCase):
    def setUp(self):
        self.m = Model()

    def test_one_block_confirms_two_logical_frames_and_waits_for_final_rearm(self):
        self.assertEqual(self.m.call(), 0)
        for name, expected in [('transfers', 2), ('consumed', 2), ('waits', 3),
                               ('logged_good', 1), ('logged_logical', 2), ('logged_attempts', 2),
                               ('logged_planned', 2), ('locks', 0), ('invariant_error', 0)]:
            self.assertEqual(self.m.get(name), expected, name)

    def test_multiple_blocks_and_commands_keep_pipeline_and_token(self):
        self.m.arg('arg_count', 4)
        for _ in range(2):
            self.assertEqual(self.m.call(), 0)
            self.assertEqual(self.m.get('logged_good'), 4)
            self.assertEqual(self.m.get('logged_logical'), 8)
        self.assertEqual(self.m.get('transfers'), 10)
        self.assertEqual(self.m.get('waits'), 12)
        self.assertEqual(self.m.get('invariant_error'), 0)

    def test_second_frame_crc_session_sequence_and_stale_block_fail_without_partial_ack(self):
        for kind in range(1, 6):
            self.m = Model(); self.m.arg('arg_count', 4)
            self.m.set('bad_reply_at', 2); self.m.set('bad_reply_kind', kind)
            self.assertEqual(self.m.call(), -5)
            self.assertEqual(self.m.get('logged_good'), 0)
            self.assertEqual(self.m.get('logged_logical'), 0)
            self.assertEqual(self.m.get('transfers'), 2)
            self.assertEqual(self.m.get('locks'), 0)
            self.assertEqual(self.m.call(), -1)
            self.assertEqual(self.m.get('transfers'), 2)

    def test_invalid_second_frame_in_initial_reply_is_rejected(self):
        self.m.set('bad_reply_at', 1); self.m.set('bad_reply_kind', 1)
        self.assertEqual(self.m.call(), -5)
        self.assertEqual(self.m.get('transfers'), 1)

    def test_missing_initial_ready_issues_no_clocks(self):
        self.m.set('fail_wait_at', 1)
        self.assertEqual(self.m.call(), -110)
        self.assertEqual(self.m.get('transfers'), 0)

    def test_missing_final_rearm_is_not_reported_as_pass(self):
        self.m.set('fail_wait_at', 3)
        self.assertEqual(self.m.call(), -110)
        self.assertEqual(self.m.get('logged_good'), 1)
        self.assertEqual(self.m.get('transfers'), 2)
        self.assertEqual(self.m.get('locks'), 0)

    def test_driver_failure_does_not_count_an_ack_or_retry(self):
        self.m.set('driver_fail_at', 2)
        self.assertEqual(self.m.call(), -5)
        self.assertEqual(self.m.get('logged_good'), 0)
        self.assertEqual(self.m.get('transfers'), 2)

    def test_switching_transport_size_requires_reboot(self):
        self.assertEqual(self.m.call(), 0)
        self.assertEqual(self.m.call('probe_legacy'), -1)
        self.assertEqual(self.m.get('transfers'), 2)

    def test_invalid_counts_gaps_and_clocks_fail_before_hardware(self):
        for name, value in [('arg_count', 0), ('arg_count', 10001), ('arg_count', '1x'),
                            ('arg_gap', 1001), ('arg_hz', 99999), ('arg_hz', 8000001)]:
            self.m = Model(); self.m.arg(name, value)
            self.assertEqual(self.m.call(), -22)
            self.assertEqual(self.m.get('transfers'), 0)
            self.assertEqual(self.m.get('locks'), 0)

    def test_maximum_block_count_is_accepted_without_sequence_overflow(self):
        self.m.arg('arg_count', 10000); self.m.set('fail_wait_at', 1)
        self.assertEqual(self.m.call(), -110)
        self.assertEqual(self.m.get('logged_target'), 10000)
        self.assertEqual(self.m.get('logged_planned'), 10001)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    out = args.build_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    source = (MAIN / 'system_app_shell.c').read_text(encoding='utf-8')
    start = source.index('static uint8_t s_swap_tx[')
    end = source.index('\n/**', source.index('    return ret;\n}', source.index('static int spi2swap_run')))
    harness = out / 'block2_host.c'
    harness.write_text(HARNESS.replace('/* PRODUCTION_COMMAND */', source[start:end]), encoding='utf-8')
    ELF = out / 'block2_host.elf'
    compiler = Path('D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/startup/app_release/app_tools/cross_tools/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe')
    subprocess.run([str(compiler), '-mcpu=cortex-m4', '-mthumb', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-fno-builtin', '-nostdlib', '-Wl,-e,probe_block', '-Wl,-Ttext=0x100000',
                    '-Wl,-Tdata=0x20000800', '-Wl,--section-start=.bss=0x20002000',
                    '-I', str(MAIN), str(harness), '-lgcc', '-o', str(ELF)], check=True)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
