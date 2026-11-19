"""Run the actual V11 shell command with an independently modeled two-ACK peer."""
from pathlib import Path
import argparse
import subprocess
import sys
import unittest
import zlib
import test_spi_block2_host as old
ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main'
STUBS=r'''
#include "spi_link_file_protocol.h"
static uint8_t circular_reply[2][512];
uint32_t opens,closes,open_error,close_error,session_active,opened_hz;
uint32_t file_mode,file_first,file_blocks,file_expected_crc,file_bad,file_repaired,file_finished,fs_calls;
uint32_t file_ram_started, file_delivery_busy, file_delivery_queries;
static bool file_delivery_mode;
uint32_t file_pairs;
static unsigned int file_pair_arm, file_half;
static bool file_paired;
static uint8_t file_pair_scratch[512];
static uint32_t file_bad_index;
static uint8_t file_seen[SFILE_BITMAP], file_reply[512];
static void file_peer(const uint8_t *tx, uint8_t *slot) {
 uint8_t cmd=tx[2], status=SFILE_OK;
 uint32_t index=sfile_u32(tx+8);
 unsigned int i;
 if(cmd==SFILE_DATA) {
  unsigned int b=index-file_first;
  if(b>=SFILE_GROUP || index>=file_blocks) {++invariant_error;return;}
  if(!sfile_valid(tx,SFILE_MASTER)) {++file_bad;file_bad_index=index;return;}
  if(file_delivery_busy)++invariant_error;
  for(i=0;i<swap_u16(tx+14);++i)
   if(tx[SFILE_HEADER+i]!=(uint8_t)(index^(index>>8)^(i*29u)))++invariant_error;
  if(file_bad && index==file_bad_index)++file_repaired;
  file_seen[b/8u]|=1u<<(b%8u);
  return;
 }
 if(!sfile_valid(tx,SFILE_MASTER)) {++invariant_error;return;}
 if(cmd==SFILE_QUERY) {
  if(file_delivery_busy) {
   ++file_delivery_queries;
   if(!--file_delivery_busy) {
    file_first+=sfile_group_count(file_blocks,file_first);
    memset(file_seen,0,SFILE_BITMAP);
    file_reply[SFILE_HEADER+1]=SFILE_OK;
    sfile_frame(file_reply,SFILE_SLAVE,SFILE_ACK,sfile_u32(tx+4),index,swap_u16(tx+12),SFILE_ACK_BYTES);
   }
  }
  memcpy(slot,file_reply,512);return;
 }
 memset(file_reply+SFILE_HEADER,0,SFILE_ACK_BYTES);
 if(cmd==SFILE_START || cmd==SFILE_START_RAM || cmd==SFILE_START_PAIR || cmd==SFILE_START_DELIVERY) {
  file_delivery_mode=cmd==SFILE_START_DELIVERY;
  file_blocks=(sfile_u32(tx+SFILE_HEADER)+SFILE_DATA_BYTES-1)/SFILE_DATA_BYTES;
  if(cmd!=SFILE_START) {
   ++file_ram_started;
   if(swap_u16(tx+14)!=(cmd==SFILE_START_DELIVERY ? 8 : 4))++invariant_error;
  }
  if((cmd==SFILE_START_PAIR || cmd==SFILE_START_DELIVERY) && !file_paired)file_pair_arm=3;
 }
 if(cmd==SFILE_GROUP_END) {
  unsigned int count=sfile_group_count(file_blocks,file_first);
  if(index!=file_first)++invariant_error;
  for(i=0;i<count;++i)if(!sfile_bit(file_seen,i))status=SFILE_MISSING;
  if(status==SFILE_MISSING)memcpy(file_reply+SFILE_HEADER+2,file_seen,SFILE_BITMAP);
  else if (file_delivery_mode) {
   status=SFILE_BUSY;file_delivery_busy=6;
  } else {file_first+=count;memset(file_seen,0,SFILE_BITMAP);}
 }
 if(cmd==SFILE_FINISH) {
  if(file_first!=file_blocks || sfile_u32(tx+SFILE_HEADER)!=file_expected_crc)status=SFILE_CHECKSUM;
  else ++file_finished;
 }
 file_reply[SFILE_HEADER]=cmd;file_reply[SFILE_HEADER+1]=status;
 sfile_frame(file_reply,SFILE_SLAVE,SFILE_ACK,sfile_u32(tx+4),index,swap_u16(tx+12),SFILE_ACK_BYTES);
 memcpy(slot,file_reply,512);
}
static int spi_acts_spi2_circular_open(const struct device *d,const struct spi_config *c) {
 (void)d;opened_hz=c->frequency;++opens;if(open_error)return -EIO;session_active=1;return 0;
}
static int spi_acts_spi2_circular_close(const struct device *d) {
 (void)d;++closes;session_active=0;return close_error?-EIO:0;
}
static int spi_acts_spi2_circular_transfer(const struct device *d,const uint8_t *tx,uint8_t *rx) {
 unsigned int slot=transfers&1u;(void)d;
 if(file_mode) {
  if(!session_active||token)++invariant_error;
  if(file_paired && sfile_u32(tx+SFILE_PAIR_WITNESS_OFFSET)!=SFILE_PAIR_WITNESS)
   ++invariant_error;
  memcpy(rx,circular_reply[slot],512);
  if(tx[2]!=SFILE_IDLE)
   file_peer(tx,file_paired&&!file_half ? file_pair_scratch : circular_reply[slot]);
  if(file_paired) {
   file_half^=1u;
   token=!file_half;
   if(token)++file_pairs;
  } else {
   token=1;
   if(file_pair_arm && !--file_pair_arm)file_paired=true;
  }
  ++transfers;now_us+=1024;return 0;
 }
 if(!session_active||token||swap_block_check(tx,0xc3))++invariant_error;
 if(!transfers) {
  swap_block_frame(circular_reply[0],0x3c,0,SWAP_BLOCK_IDLE);
  swap_block_frame(circular_reply[1],0x3c,0,SWAP_BLOCK_IDLE);
 }
 ++transfers;if(transfers==driver_fail_at)return -EIO;
 memcpy(rx,circular_reply[slot],512);
 if(transfers==bad_reply_at) {
  if(bad_reply_kind==1)rx[400]^=1;
  if(bad_reply_kind==2)swap_frame(rx+256,0x3c,swap_u16(rx+2)+1,swap_u16(rx+260));
  if(bad_reply_kind==3)swap_block_frame(rx,0x3c,swap_u16(rx+2),swap_block_sequence(rx)+1);
  if(bad_reply_kind==4)swap_block_frame(rx,SWAP_SLAVE,swap_u16(rx+2),swap_block_sequence(rx));
  if(bad_reply_kind==5)swap_block_frame(rx,0x3c,swap_u16(rx+2)+1,swap_block_sequence(rx));
  if(bad_reply_kind==6)memset(rx,0xff,512);
 }
 swap_block_frame(circular_reply[slot],0x3c,swap_u16(tx+2),swap_block_sequence(tx));
 token=1;now_us+=1024;return 0;
}
'''
EXTRA=r'''
#include "spi_link_circular.h"
#include <string.h>
int strcmp(const char *a, const char *b) {
 while (*a && *a==*b) {++a;++b;}
 return (unsigned char)*a-(unsigned char)*b;
}
size_t strlen(const char *p) {const char *s=p;while(*p)++p;return (size_t)(p-s);}
static void shell_warn(const struct shell *s,const char *fmt,...) {(void)s;(void)fmt;}
uint32_t file_ready_timeout_ms;
static int swap_ready_wait_timeout(uint32_t *waited,uint32_t timeout_ms) {
 file_ready_timeout_ms=timeout_ms;
 return swap_ready_wait(waited);
}
#include "spi_link_file_host.h"
int probe_file(void) {
 char *a[]={"spi2filetest",arg_count,"test.bin",arg_gap};
 file_mode=1;
 return shell_spi2filetest(&shell,arg_gap[0]?4:3,a);
}
int probe_ram(void) {
 char *a[]={"spi2ram",arg_count,arg_gap};
 file_mode=1;
 return shell_spi2ram(&shell,arg_gap[0]?3:2,a);
}
int probe_pair(void) {
 char *a[]={"spi2rampair",arg_count,arg_gap};
 file_mode=1;
 return shell_spi2rampair(&shell,arg_gap[0]?3:2,a);
}
int probe_pair_rate(void) {
 char *a[]={"spi2rampair",arg_count,arg_gap,"--hz",arg_hz};
 file_mode=1;
 if (!arg_gap[0]) {a[2]=a[3];a[3]=a[4];}
 return shell_spi2rampair(&shell,arg_gap[0]?5:4,a);
}
int probe_delivery(void) {
 char *a[]={"spi2deliver",arg_count,"11000",arg_gap};
 file_mode=1;
 return shell_spi2deliver(&shell,arg_gap[0]?4:3,a);
}
int probe_pair_missing_rate(void) {
 char *a[]={"spi2rampair",arg_count,"--hz"};
 return shell_spi2rampair(&shell,3,a);
}
int probe_circle(void) {char *a[]={"spi2circ",arg_count,arg_gap,arg_hz};return shell_spi2circ(&shell,4,a);}
'''
class Model(old.Model):
    def call(self,name='probe_circle'):return super().call(name, instruction_limit=30000000)

class HostTests(unittest.TestCase):
    def setUp(self):self.m=Model()
    def check_file(self, blocks, bad='', ram=False, paired=False):
        m=self.m
        m.arg('arg_count',blocks);m.arg('arg_gap',bad)
        data=bytes((b^(b>>8)^(i*29))&255 for b in range(blocks) for i in range(480))
        m.set('file_expected_crc',zlib.crc32(data))
        self.assertEqual(m.call('probe_pair' if paired else ('probe_ram' if ram else 'probe_file')),0)
        self.assertEqual(m.get('file_ram_started'),int(ram))
        self.assertEqual(m.get('file_finished'),1)
        self.assertEqual(m.get('file_first'),blocks)
        self.assertEqual(m.get('file_bad'),int(bad != ''))
        self.assertEqual(m.get('file_repaired'),int(bad != ''))
        self.assertEqual(m.get('fs_calls'),0)
        self.assertEqual(m.get('file_ready_timeout_ms'),2000)
        self.assertEqual(m.get('invariant_error'),0)
    def test_delivery_waits_for_busy_and_resends_tail(self):
        m = Model()
        m.arg('arg_count', 48001); m.arg('arg_gap', '100')
        data = bytes((b ^ (i * 29)) & 255 for b in range(100) for i in range(480)) + bytes((100,))
        m.set('file_expected_crc', zlib.crc32(data))
        self.assertEqual(m.call('probe_delivery'), 0)
        self.assertEqual(m.get('file_delivery_queries'), 12)
        self.assertEqual(m.get('file_repaired'), 1)
        self.assertEqual(m.get('file_finished'), 1)
        self.assertEqual(m.get('invariant_error'), 0)

    def test_pair_rate_and_legacy_default(self):
        for request, bad, expected in [(None, '', 4000000), (100000, '', 100000),
                                       (4000000, '0', 4000000), (8000000, '', 8000000),
                                       (8000000, '0', 8000000)]:
            with self.subTest(request=request, bad=bad):
                m = Model()
                m.arg('arg_count', 1)
                m.arg('arg_gap', bad)
                if request is not None:
                    m.arg('arg_hz', request)
                m.set('file_expected_crc', zlib.crc32(bytes((i * 29) & 255 for i in range(480))))
                self.assertEqual(m.call('probe_pair' if request is None else 'probe_pair_rate'), 0)
                self.assertEqual(m.get('opened_hz'), expected)
                self.assertEqual(m.get('file_repaired'), int(bad != ''))
                self.assertEqual(m.get('file_finished'), 1)

    def test_invalid_pair_rate_never_opens_dma(self):
        for value in ('', '-1', '99999', '8000001', '8000000x'):
            with self.subTest(value=value):
                m = Model()
                m.arg('arg_count', 1)
                m.arg('arg_gap', '')
                m.arg('arg_hz', value)
                self.assertEqual(m.call('probe_pair_rate'), -22)
                self.assertEqual(m.call('probe_pair_missing_rate'), -22)
                self.assertEqual(m.get('opens'), 0)

    def test_generated_data_ends_at_full_group_without_fs(self):
        self.check_file(100)
    def test_generated_data_repairs_first_block_of_second_group(self):
        self.check_file(101,100)
    def test_ram_benchmark_negotiates_ram_and_repairs(self):
        self.check_file(101,17,ram=True)
    def test_pair_credits_send_two_per_ready_and_pad_odd_repair(self):
        self.check_file(101,17,ram=True,paired=True)
        pairs=self.m.get('file_pairs')
        self.assertGreaterEqual(pairs,51)
        self.assertEqual(self.m.get('consumed'),3+pairs)
        self.assertEqual(self.m.get('transfers'),3+2*pairs)
    def test_generated_data_rejects_out_of_range_bad_block(self):
        m=self.m;m.arg('arg_count',100);m.arg('arg_gap',100)
        self.assertEqual(m.call('probe_file'),-22)
        self.assertEqual(m.get('opens'),0)
    def test_one_ack_requires_three_transactions_and_final_credit(self):
        m=self.m;self.assertEqual(m.call(),0)
        for name,n in [('logged_good',1),('logged_target',1),('transfers',3),('waits',4),
                       ('consumed',3),('opens',1),('closes',1),('locks',0),('invariant_error',0)]:
            self.assertEqual(m.get(name),n,name)
    def test_repeated_sessions_both_slot_parities(self):
        m=self.m
        for count in (1,2,3,8):
            m.arg('arg_count',count);self.assertEqual(m.call(),0)
            self.assertEqual(m.get('logged_good'),count)
            self.assertEqual(m.get('logged_attempts'),count+2)
        self.assertEqual(m.get('invariant_error'),0)
    def test_bad_crc_wrong_session_wrong_sequence_and_old_peer(self):
        for kind in range(1,6):
            m=Model();m.arg('arg_count',4);m.set('bad_reply_at',3);m.set('bad_reply_kind',kind)
            self.assertEqual(m.call(),-5);self.assertEqual(m.get('logged_good'),0)
            self.assertEqual(m.get('transfers'),3);self.assertEqual(m.get('closes'),1)
            self.assertEqual(m.call(),-1)
    def test_first_two_replies_are_validated_but_not_counted(self):
        for at in (1,2):
            m=Model();m.set('bad_reply_at',at);m.set('bad_reply_kind',1)
            self.assertEqual(m.call(),-5);self.assertEqual(m.get('logged_good'),0)
    def test_all_high_first_reply_fails_with_both_headers_without_retry(self):
        m=self.m;m.arg('arg_count',5000);m.set('bad_reply_at',1);m.set('bad_reply_kind',6)
        self.assertEqual(m.call(),-5);self.assertEqual(m.get('logged_good'),0)
        self.assertEqual(m.get('transfers'),1);self.assertEqual(m.get('logged_headers'),2)
        self.assertEqual(m.get('closes'),1);self.assertEqual(m.get('locks'),0)
        self.assertEqual(m.call(),-1);self.assertEqual(m.get('transfers'),1)
    def test_missing_ready_first_middle_and_final_never_passes(self):
        for at,tx in ((1,0),(2,1),(4,3)):
            m=Model();m.set('fail_wait_at',at);self.assertEqual(m.call(),-110)
            self.assertEqual(m.get('transfers'),tx);self.assertEqual(m.get('closes'),1)
            self.assertEqual(m.get('locks'),0)
    def test_open_close_and_transfer_failure_unwind(self):
        for field in ('open_error','close_error','driver_fail_at'):
            m=Model();m.set(field,1);self.assertEqual(m.call(),-5)
            self.assertEqual(m.get('locks'),0);self.assertEqual(m.get('session_active'),0)
            self.assertEqual(m.get('closes'),0 if field=='open_error' else 1)
    def test_command_after_fault_logs_blocked_without_new_dma_or_clocks(self):
        m=self.m;m.set('driver_fail_at',1)
        self.assertEqual(m.call(),-5)
        m.arg('arg_count',5000)
        self.assertEqual(m.call(),-1)
        self.assertEqual(m.get('logged_blocked'),1)
        self.assertEqual(m.get('opens'),1);self.assertEqual(m.get('transfers'),1)
        self.assertEqual(m.get('locks'),0)
    def test_circular_and_old_commands_cannot_mix_in_either_order(self):
        m=self.m;self.assertEqual(m.call(),0);self.assertEqual(m.call('probe_block'),-1)
        m=Model();self.assertEqual(m.call('probe_block'),0);self.assertEqual(m.call(),-1)
    def test_invalid_parameters_do_not_open_hardware(self):
        for key,value in [('arg_count',0),('arg_count',10001),('arg_count','3x'),('arg_gap',1001),
                          ('arg_hz',99999),('arg_hz',8000001)]:
            m=Model();m.arg(key,value);self.assertEqual(m.call(),-22)
            self.assertEqual(m.get('opens'),0);self.assertEqual(m.get('locks'),0)
    def test_max_count_and_minimum_clock_are_accepted(self):
        m=self.m;m.arg('arg_count',10000);m.arg('arg_hz',100000);m.set('fail_wait_at',1)
        self.assertEqual(m.call(),-110);self.assertEqual(m.get('opens'),1)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
    out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    (out/'fs').mkdir(exist_ok=True)
    (out/'fs/fs.h').write_text(r'''
typedef int32_t off_t;
typedef int32_t ssize_t;
struct fs_file_t { int unused; };
#define EFBIG 27
#define EBADMSG 74
#define ETIMEDOUT 110
#define FS_O_READ 1
#define FS_SEEK_SET 0
#define FS_SEEK_END 2
static inline void fs_file_t_init(struct fs_file_t *f) {(void)f;++fs_calls;}
static inline int fs_open(struct fs_file_t *f,const char *p,int m) {(void)f;(void)p;(void)m;++fs_calls;return -1;}
static inline int fs_seek(struct fs_file_t *f,off_t o,int w) {(void)f;(void)o;(void)w;++fs_calls;return -1;}
static inline off_t fs_tell(struct fs_file_t *f) {(void)f;++fs_calls;return -1;}
static inline int fs_read(struct fs_file_t *f,void *p,size_t n) {(void)f;(void)p;(void)n;++fs_calls;return -1;}
static inline int fs_close(struct fs_file_t *f) {(void)f;++fs_calls;return -1;}
''',encoding='utf-8')
    s=(MAIN/'system_app_shell.c').read_text(encoding='utf-8')
    start=s.index('static uint8_t s_swap_tx[')
    end=s.index('\n/**',s.index('    return ret;\n}',s.index('static int spi2swap_run')))
    h=old.HARNESS.replace('/* PRODUCTION_COMMAND */',STUBS+s[start:end])+EXTRA
    h=h.replace('static void shell_error(const struct shell *s,const char *fmt,...){(void)s;(void)fmt;}',
        'uint32_t logged_blocked; static void shell_error(const struct shell *s,const char *fmt,...){'
        '(void)s;if(prefix(fmt,"SPI11 BLOCKED"))++logged_blocked;}')
    h=h.replace('static void shell_hexdump(const struct shell *s,const void *p,size_t n){(void)s;(void)p;(void)n;}',
        'uint32_t logged_headers; static void shell_hexdump(const struct shell *s,const void *p,size_t n){'
        '(void)s;(void)p;if(n==16)++logged_headers;}')
    find='if(prefix(fmt,"SPI10 RESULT"))'
    repl='''if(prefix(fmt,"SPI11 RESULT")){
        (void)va_arg(a,unsigned int);logged_good=va_arg(a,unsigned int);
        logged_target=va_arg(a,unsigned int);logged_attempts=va_arg(a,unsigned int);
    } else if(prefix(fmt,"SPI10 RESULT"))'''
    h=h.replace(find,repl)
    source=out/'circular_command.c';source.write_text(h,encoding='utf-8');old.ELF=out/'circular_command.elf'
    compiler=Path('D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/startup/app_release/app_tools/cross_tools/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe')
    subprocess.run([str(compiler),'-mcpu=cortex-m4','-mthumb','-O0','-Wall','-Wextra','-Werror',
        '-fno-builtin','-nostdlib','-Wl,-e,probe_circle','-Wl,-Ttext=0x100000','-Wl,-Tdata=0x20000800',
        '-Wl,--section-start=.bss=0x20002000','-I',str(MAIN),'-I',str(out),
        '-Wno-unused-function',str(source),'-lgcc','-o',str(old.ELF)],check=True)
    unittest.main(argv=[sys.argv[0]],verbosity=2)
