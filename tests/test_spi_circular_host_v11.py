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
uint32_t file_stream_fail, file_aborts, file_http_status;
static bool file_https, file_http;
uint32_t http_meta, http_written;
uint32_t http_expected_size=540, http_response_size=805, http_ram_spec;
uint8_t http_expected_spec[3072];
uint32_t file_ram_started, file_delivery_busy, file_delivery_queries;
static bool file_delivery_mode;
uint32_t file_pairs, file_source_size, file_source_offset;
static uint8_t file_source_byte(uint32_t offset) { return (uint8_t)(offset ^ (offset >> 8)); }
static unsigned int file_pair_arm, file_half;
static bool file_paired;
static uint8_t file_pair_scratch[512];
static uint32_t file_bad_index;
static uint8_t file_seen[SFILE_BITMAP], file_reply[512];
static void file_peer(const uint8_t *tx, uint8_t *slot) {
 uint8_t cmd=tx[2], status=SFILE_OK;
 uint32_t index=sfile_u32(tx+8);
 unsigned int i, ack_len=SFILE_ACK_BYTES;
 if(cmd==SFILE_DATA) {
  unsigned int b=index-file_first;
  if((file_http && http_meta!=http_expected_size) || b>=SFILE_GROUP || index>=file_blocks) {++invariant_error;return;}
  if(!sfile_valid(tx,SFILE_MASTER)) {++file_bad;file_bad_index=index;return;}
  if(file_delivery_busy)++invariant_error;
  for(i=0;i<swap_u16(tx+14);++i) {
   uint8_t expected=file_source_size ? file_source_byte(file_source_offset+index*480u+i) :
                                     (uint8_t)(index^(index>>8)^(i*29u));
   if(tx[SFILE_HEADER+i]!=expected)++invariant_error;
  }
  if(file_bad && index==file_bad_index)++file_repaired;
  file_seen[b/8u]|=1u<<(b%8u);
  return;
 }
 if(!sfile_valid(tx,SFILE_MASTER)) {++invariant_error;return;}
 if(cmd==SFILE_QUERY) {
  if(file_delivery_busy) {
   ++file_delivery_queries;
   if(!--file_delivery_busy) {
    if (!file_stream_fail) {
     file_first+=sfile_group_count(file_blocks,file_first);
     memset(file_seen,0,SFILE_BITMAP);
    }
    file_reply[SFILE_HEADER+1]=file_stream_fail ? SFILE_SEND_FAILED : SFILE_OK;
    sfile_frame(file_reply,SFILE_SLAVE,SFILE_ACK,sfile_u32(tx+4),index,swap_u16(tx+12),SFILE_ACK_BYTES);
   }
  }
  memcpy(slot,file_reply,512);return;
 }
 memset(file_reply+SFILE_HEADER,0,SFILE_ACK_BYTES);
 if(cmd==SFILE_START || cmd==SFILE_START_RAM || cmd==SFILE_START_PAIR || cmd==SFILE_START_DELIVERY || cmd==SFILE_START_STREAM || cmd==SFILE_START_HTTPS || cmd==SFILE_START_HTTP) {
  file_https=cmd==SFILE_START_HTTPS;
  file_http=cmd==SFILE_START_HTTP;
  file_delivery_mode=cmd==SFILE_START_DELIVERY || cmd==SFILE_START_STREAM || file_https || file_http;
  if (cmd==SFILE_START_STREAM) file_stream_fail=sfile_u32(tx+SFILE_HEADER+8)!=UINT32_MAX;
  file_blocks=(sfile_u32(tx+SFILE_HEADER)+SFILE_DATA_BYTES-1)/SFILE_DATA_BYTES;
  if(cmd!=SFILE_START) {
   ++file_ram_started;
   if (file_http) {
    if (swap_u16(tx+14)!=SFILE_HTTPS_START_FIXED+2 ||
        swap_u16(tx+SFILE_HEADER+SFILE_HTTPS_START_FIXED)!=http_expected_size) ++invariant_error;
   } else if (file_https) {
    static const char url[]="https://recorder.example/put";
    if(swap_u16(tx+14)!=SFILE_HTTPS_START_FIXED+sizeof(url)-1)++invariant_error;
    for(i=0;i<sizeof(url)-1;++i)
     if(tx[SFILE_HEADER+SFILE_HTTPS_START_FIXED+i]!=(uint8_t)url[i])++invariant_error;
   } else if(swap_u16(tx+14)!=(cmd==SFILE_START_STREAM ? 12 : (cmd==SFILE_START_DELIVERY ? 8 : 4)))
    ++invariant_error;
  }
  if((cmd==SFILE_START_PAIR || cmd==SFILE_START_DELIVERY || cmd==SFILE_START_STREAM || cmd==SFILE_START_HTTPS || cmd==SFILE_START_HTTP) && !file_paired)file_pair_arm=3;
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
 if(cmd==SFILE_HTTP_META) {
  if(index!=http_meta)++invariant_error;
  for(i=0;i<swap_u16(tx+14);++i)
   if(tx[SFILE_HEADER+i]!=(http_ram_spec ? http_expected_spec[index+i] : (uint8_t)(index+i)))++invariant_error;
  http_meta+=swap_u16(tx+14);
 }
 if(cmd==SFILE_HTTP_READ) {
  unsigned int count=http_response_size-index;
  if(count>SFILE_HTTP_PAGE_BYTES) count=SFILE_HTTP_PAGE_BYTES;
  for(i=0;i<count;++i)file_reply[SFILE_HEADER+SFILE_ACK_BYTES+i]=(uint8_t)(index+i);
  ack_len+=count;
 }
 if(cmd==SFILE_ABORT) ++file_aborts;
 if(cmd==SFILE_FINISH) {
  if(file_first!=file_blocks || sfile_u32(tx+SFILE_HEADER)!=file_expected_crc)status=SFILE_CHECKSUM;
  else {
   ++file_finished;
   if (file_http) {
    unsigned int code=file_http_status ? file_http_status : 200;
    status=(code<200 || code>=300) ? SFILE_HTTP_FAILED : SFILE_OK;
    sfile_put16(file_reply+SFILE_HEADER+SFILE_ACK_BYTES,code);
    sfile_put16(file_reply+SFILE_HEADER+SFILE_HTTP_RESULT_HEAD,http_response_size);
    ack_len=SFILE_HTTP_RESULT_HEAD+2;
   } else if (file_https) {
    unsigned int code=file_http_status ? file_http_status : 200;
    status=(code<200 || code>=300) ? SFILE_HTTP_FAILED : SFILE_OK;
    sfile_put16(file_reply+SFILE_HEADER+SFILE_ACK_BYTES,code);
    memcpy(file_reply+SFILE_HEADER+SFILE_HTTP_RESULT_HEAD,"{}",2);
    ack_len=SFILE_HTTP_RESULT_HEAD+2;
   }
  }
 }
 file_reply[SFILE_HEADER]=cmd;file_reply[SFILE_HEADER+1]=status;
 sfile_frame(file_reply,SFILE_SLAVE,SFILE_ACK,sfile_u32(tx+4),index,swap_u16(tx+12),ack_len);
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
uint32_t file_ready_timeout_ms, network_hold_ms, network_wait_budget, network_hold_used;
static int swap_ready_wait_timeout(uint32_t *waited,uint32_t timeout_ms) {
 file_ready_timeout_ms=timeout_ms;
 int ret=swap_ready_wait(waited);
 if(!ret && file_https && file_delivery_busy && network_hold_ms && !network_hold_used) {
  network_hold_used=1;network_wait_budget=timeout_ms;
  uint32_t delay=network_hold_ms<timeout_ms?network_hold_ms:timeout_ms;
  *waited=delay*1000u;now_us+=*waited;
  if(network_hold_ms>=timeout_ms)return -110;
 }
 return ret;
}
#define ENOMEM 12
uint8_t probe_heap[3072];
uint32_t probe_allocated, probe_alloc_fail;
uint8_t request_heap[480];
uint32_t request_allocated, request_alloc_fail;
#define K_NO_WAIT 0
struct k_heap {int unused;};
struct k_heap sys_mem_pool;
static void *k_heap_alloc(struct k_heap *h,size_t n,int timeout) {
 if(h!=&sys_mem_pool || timeout!=K_NO_WAIT)++invariant_error;
 if(probe_alloc_fail || (n==480 && request_alloc_fail))return NULL;
 if(n==480 && probe_allocated) {
  if(request_allocated)return NULL;
  request_allocated=n;memset(request_heap,0xcc,n);return request_heap;
 }
 if(probe_alloc_fail || probe_allocated || n>sizeof(probe_heap))return NULL;
 probe_allocated=n;memset(probe_heap,0xcc,n);return probe_heap;
}
static void k_heap_free(struct k_heap *h,void *p) {
 if(h!=&sys_mem_pool)++invariant_error;
 if(p==request_heap) {
  for(unsigned int i=0;i<request_allocated;++i)if(request_heap[i])++invariant_error;
  request_allocated=0;return;
 }
 unsigned int i;if(p!=probe_heap)++invariant_error;
 for(i=0;i<probe_allocated;++i)if(probe_heap[i])++invariant_error;
 probe_allocated=0;
}
/* Hardware register model; production entropy code executes unchanged. */
#define EBUSY 16
#define CMU_DEVCLKEN0 1
#define CMU_SECCLK 2
#define CLOCK_ID_SE 3
#define RESET_ID_SE 3
#define TRNG_CTRL 4
#define TRNG_LR 5
#define TRNG_MR 6
uint32_t rng_clock, rng_control, rng_samples, rng_resets, rng_tick_start;
uint32_t rng_timeout_at, rng_stuck, rng_lock_fail, rng_locked, irq_locked;
uint32_t rng_clock_writes, rng_low_reads, rng_high_reads;
static unsigned int irq_lock(void) {++irq_locked;return 0;}
static void irq_unlock(unsigned int key) {(void)key;--irq_locked;}
static uint32_t sys_read32(uint32_t reg) {
 if(!irq_locked || !rng_locked)++invariant_error;
 if(reg==CMU_DEVCLKEN0)return rng_clock;
 if(reg==TRNG_CTRL) {
  if(rng_control&1u) {
   now_us+=100;
   if(rng_samples!=rng_timeout_at) return rng_control|(1u<<31);
  }
  return rng_control;
 }
 if(reg==TRNG_LR) {++rng_low_reads;return rng_stuck==2?0:(rng_stuck==3?UINT32_MAX:0x10203000u+(rng_stuck?1:rng_samples));}
 if(reg==TRNG_MR) {++rng_high_reads;return rng_stuck==2?0:(rng_stuck==3?UINT32_MAX:0x50607080u);}
 ++invariant_error;return 0;
}
static void sys_write32(uint32_t value,uint32_t reg) {
 if(!irq_locked || !rng_locked)++invariant_error;
 if(reg==CMU_SECCLK) {++rng_clock_writes;return;}
 if(reg!=TRNG_CTRL) {++invariant_error;return;}
 rng_control=value;
 if(value&1u) {++rng_samples;rng_tick_start=now_us;}
}
static void acts_clock_peripheral_enable(unsigned int id) {(void)id;rng_clock|=1u<<CLOCK_ID_SE;}
static void acts_clock_peripheral_disable(unsigned int id) {(void)id;rng_clock&=~(1u<<CLOCK_ID_SE);}
static void acts_reset_peripheral(unsigned int id) {(void)id;++rng_resets;}
#include "spi_link_file_host.h"
uint8_t entropy_output[48];
int probe_entropy(void) {return sfile_entropy_fill(entropy_output,sizeof(entropy_output));}
char arg_crc[16], arg_hex[129];
int probe_cfg_begin(void) {
 char *a[]={"spi2putcfg","begin",arg_count,arg_crc};
 return shell_spi2putcfg(&shell,4,a);
}
int probe_cfg_data(void) {
 char *a[]={"spi2putcfg","data",arg_gap,arg_hex};
 return shell_spi2putcfg(&shell,4,a);
}
int probe_cfg_clear(void) {
 char *a[]={"spi2putcfg","clear"};return shell_spi2putcfg(&shell,2,a);
}
int probe_putgen(void) {
 char *a[]={"spi2putgen",arg_count};file_mode=1;
 return shell_spi2putgen(&shell,2,a);
}
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
int probe_put(void) {
 char *a[]={"spi2put",arg_count,"https://recorder.example/put"};
 file_mode=1;
 return shell_spi2put(&shell,3,a);
}
int probe_putfile(void) {
 char *a[]={"spi2putfile","test.msbc",arg_gap,arg_count,"https://recorder.example/put"};
 file_mode=1;
 return shell_spi2putfile(&shell,5,a);
}
int probe_http(void) {
 char *a[]={"spi2http","spec.txt","test.msbc","0","1","result.json"};
 file_mode=1;
 return shell_spi2http(&shell,6,a);
}
int probe_stream(void) {
 char *a[]={"spi2stream",arg_count,"20",arg_gap};
 file_mode=1;
 return shell_spi2stream(&shell,arg_gap[0]?4:3,a);
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

    def test_https_clock_uses_real_soc_divider(self):
        # Execute the production clock selector as well as the mocked SPI peer.
        # A shell-only mock previously accepted the unsupported 1 MHz setting.
        m = Model()
        self.assertEqual(m.call('probe_https_clock'), 0)
        self.assertEqual(m.get('clock_register') & 0x30f, 7)  # HOSC /8
        self.assertEqual(m.get('clock_writes'), 1)
        m = Model()
        self.assertEqual(m.call('probe_1mhz_clock'), -34)
        self.assertEqual(m.get('clock_writes'), 0)

    def test_https_quiet_wait_uses_operation_deadline(self):
        for hold, expected in [(2500, 0), (36000, -110)]:
            with self.subTest(hold=hold):
                m = Model()
                m.arg('arg_count', 481)
                data = bytes((i * 29) & 255 for i in range(480)) + bytes((1,))
                m.set('file_expected_crc', zlib.crc32(data))
                m.set('network_hold_ms', hold)
                self.assertEqual(m.call('probe_put'), expected)
                self.assertEqual(m.get('network_hold_used'), 1)
                self.assertGreater(m.get('network_wait_budget'), 30000)
                self.assertLessEqual(m.get('network_wait_budget'), 35000)
                self.assertEqual(m.get('session_active'), 0)
                self.assertEqual(m.get('invariant_error'), 0)

    def test_entropy_uses_six_hardware_samples_and_preserves_shared_clock(self):
        for clock in (0, 1 << 3):
            m = Model(); m.set('rng_clock', clock)
            self.assertEqual(m.call('probe_entropy'), 0)
            self.assertEqual(m.get('rng_samples'), 6)
            self.assertEqual(m.get('rng_low_reads'), 6)
            self.assertEqual(m.get('rng_high_reads'), 6)
            self.assertEqual(m.get('rng_clock'), clock)
            self.assertEqual(m.get('rng_resets'), 0 if clock else 6)
            self.assertEqual(m.get('rng_clock_writes'), 0 if clock else 6)
            self.assertEqual(m.get('rng_control'), 0)
            self.assertEqual(m.get('rng_locked'), 0)
            self.assertEqual(m.get('irq_locked'), 0)
            expected = b''.join((0x10203000 + i).to_bytes(4, 'little') +
                                (0x50607080).to_bytes(4, 'little') for i in range(1, 7))
            self.assertEqual(bytes(m.uc.mem_read(m.sym['entropy_output'], 48)), expected)
            self.assertEqual(m.get('invariant_error'), 0)

    def test_entropy_failures_wipe_partial_seed_and_release_resources(self):
        for kind, value, expected in [('rng_timeout_at', 3, -110), ('rng_stuck', 1, -5),
                                      ('rng_stuck', 2, -5), ('rng_stuck', 3, -5),
                                      ('rng_lock_fail', 1, -16)]:
            m = Model(); m.set(kind, value)
            m.set('now_us', 0xfffffe00)  # Timeout comparison must survive wrap.
            m.uc.mem_write(m.sym['entropy_output'], b'\xcc' * 48)
            self.assertEqual(m.call('probe_entropy'), expected)
            self.assertEqual(bytes(m.uc.mem_read(m.sym['entropy_output'], 48)), bytes(48))
            for field in ('rng_clock', 'rng_control', 'rng_locked', 'irq_locked', 'invariant_error'):
                self.assertEqual(m.get(field), 0, field)

    def test_entropy_refuses_active_raw_trng_without_reset_or_register_change(self):
        m = Model(); m.set('rng_clock', 1 << 3); m.set('rng_control', 9)
        self.assertEqual(m.call('probe_entropy'), -16)
        self.assertEqual(m.get('rng_clock'), 1 << 3)
        self.assertEqual(m.get('rng_control'), 9)
        for field in ('rng_samples', 'rng_resets', 'rng_locked', 'irq_locked', 'invariant_error'):
            self.assertEqual(m.get(field), 0, field)

    def test_entropy_detects_repeated_sample_across_calls(self):
        m = Model()
        self.assertEqual(m.call('probe_entropy'), 0)
        m.set('rng_samples', 5)  # Next hardware value repeats the last value.
        self.assertEqual(m.call('probe_entropy'), -5)
        self.assertEqual(bytes(m.uc.mem_read(m.sym['entropy_output'], 48)), bytes(48))
        self.assertEqual(m.get('rng_locked'), 0)
        self.assertEqual(m.get('invariant_error'), 0)

    def test_seed_and_allocation_failure_before_spi_can_retry(self):
        for field, expected in [('rng_timeout_at', -110), ('rng_lock_fail', -16),
                                ('request_alloc_fail', -12)]:
            m = Model(); m.arg('arg_count', 1); m.set(field, 1)
            self.assertEqual(m.call('probe_put'), expected)
            for name in ('opens', 'transfers', 'locks', 'request_allocated', 'probe_allocated'):
                self.assertEqual(m.get(name), 0, name)
            m.set(field, 0); m.set('file_http_status', 200)
            m.set('file_expected_crc', zlib.crc32(b'\0'))
            self.assertEqual(m.call('probe_put'), 0)
            self.assertEqual(m.get('invariant_error'), 0)

    def stage(self, m, spec, crc=None):
        m.arg('arg_count', len(spec))
        m.arg('arg_crc', f'{zlib.crc32(spec) if crc is None else crc:08x}')
        self.assertEqual(m.call('probe_cfg_begin'), 0)
        for offset in range(0, len(spec), 64):
            m.arg('arg_gap', offset); m.arg('arg_hex', spec[offset:offset+64].hex())
            self.assertEqual(m.call('probe_cfg_data'), 0)
        m.set('http_expected_size', len(spec)); m.set('http_ram_spec', 1)
        m.uc.mem_write(m.sym['http_expected_spec'], spec)

    def test_staged_put_seed_failure_frees_both_buffers_and_requires_restage(self):
        m = Model()
        prefix = b'PUT\nhttps://example.test/p?x='
        spec = prefix + b'a' * (480 - len(prefix) - 1) + b'\n'
        for failure in ('rng_timeout_at', 'request_alloc_fail'):
            m = Model(); self.stage(m, spec)
            m.arg('arg_count', 1); m.set(failure, 1)
            self.assertEqual(m.call('probe_putgen'), -110 if failure == 'rng_timeout_at' else -12)
            for field in ('opens', 'transfers', 'locks', 'request_allocated', 'probe_allocated'):
                self.assertEqual(m.get(field), 0, field)
            self.assertEqual(m.call('probe_putgen'), -22)
            m.set(failure, 0); self.stage(m, spec)
            m.arg('arg_count', 1); m.set('file_http_status', 200)
            m.set('file_expected_crc', zlib.crc32(b'\0'))
            self.assertEqual(m.call('probe_putgen'), 0)
            self.assertEqual(m.get('invariant_error'), 0)

    def test_ram_put_long_url_headers_cross_group_and_response(self):
        spec = b'PUT\nhttps://example.test/put?sig=' + b'a' * 900 + b'\nContent-Type: application/octet-stream\r\nX-Test: exact\r\n'
        for code, response_size in ((200, 805), (503, 16384), (204, 0)):
            with self.subTest(code=code):
                m = Model(); self.stage(m, spec)
                m.set('file_http_status', code); m.set('http_response_size', response_size)
                m.arg('arg_count', 48001)
                data = bytes((b ^ (i * 29)) & 255 for b in range(100) for i in range(480)) + bytes((100,))
                m.set('file_expected_crc', zlib.crc32(data))
                self.assertEqual(m.call('probe_putgen'), 0 if code < 300 else -5)
                self.assertEqual(m.get('opened_hz'), 4000000)
                self.assertEqual(m.get('http_meta'), len(spec))
                self.assertEqual(m.get('logged_http_bytes'), response_size)
                self.assertEqual(m.get('logged_group_busy'), 6)
                self.assertGreater(m.get('logged_group_wait'), 0)
                self.assertEqual(m.get('fs_calls'), 0)
                self.assertEqual(m.get('probe_allocated'), 0)
                self.assertEqual(m.get('invariant_error'), 0)
                # Never reuse an expired/single-use request, even after HTTP failure.
                opens = m.get('opens')
                self.assertEqual(m.call('probe_putgen'), -22)
                self.assertEqual(m.get('opens'), opens)

    def test_ram_spec_rejects_truncated_corrupt_reordered_and_bad_hex(self):
        m = self.m
        m.arg('arg_count', 30); m.arg('arg_crc', '00000000')
        self.assertEqual(m.call('probe_cfg_begin'), 0)
        m.arg('arg_gap', 1); m.arg('arg_hex', '50')
        self.assertEqual(m.call('probe_cfg_data'), -22)
        m.arg('arg_gap', 0); m.arg('arg_hex', '50zz')
        self.assertEqual(m.call('probe_cfg_data'), -22)
        m.arg('arg_hex', '50')
        self.assertEqual(m.call('probe_cfg_data'), 0)
        self.assertEqual(m.call('probe_cfg_data'), -22)
        m.arg('arg_count', 1)
        self.assertEqual(m.call('probe_putgen'), -22)
        self.stage(m, b'PUT\nhttps://example.test/p\n', crc=0)
        m.arg('arg_count', 1)
        self.assertEqual(m.call('probe_putgen'), -74)
        self.assertEqual(m.get('opens'), 0)
        self.assertEqual(m.call('probe_cfg_clear'), 0)
        self.assertEqual(m.get('probe_allocated'), 0)
        self.assertEqual(m.get('invariant_error'), 0)

    def test_ram_spec_bounds_allocation_failure_and_transport_cleanup(self):
        m = self.m
        m.arg('arg_crc', '00000000')
        for size in ('0', '3073', '4294967297', '-1', '1x'):
            m.arg('arg_count', size)
            self.assertEqual(m.call('probe_cfg_begin'), -22)
        m.set('probe_alloc_fail', 1); m.arg('arg_count', 20)
        self.assertEqual(m.call('probe_cfg_begin'), -12)
        self.assertEqual(m.get('opens'), 0)
        m.set('probe_alloc_fail', 0)
        self.stage(m, b'PUT\nhttps://example.test/p\n')
        for size in ('0', '4800001', '4294967297'):
            m.arg('arg_count', size)
            self.assertEqual(m.call('probe_putgen'), -22)
        m.arg('arg_count', 1); m.set('open_error', 1)
        self.assertEqual(m.call('probe_putgen'), -5)
        self.assertEqual(m.get('probe_allocated'), 0)
        self.assertEqual(m.get('fs_calls'), 0)
        self.assertEqual(m.get('invariant_error'), 0)

    def test_ram_spec_exact_limit_and_plain_http_rejection(self):
        m = self.m
        prefix = b'PUT\nhttps://example.test/p?sig='
        spec = prefix + b'a' * (3072 - len(prefix) - 1) + b'\n'
        self.stage(m, spec)
        m.arg('arg_count', 1); m.set('file_expected_crc', zlib.crc32(b'\0'))
        self.assertEqual(m.call('probe_putgen'), 0)
        self.assertEqual(m.get('http_meta'), 3072)
        self.assertEqual(m.get('invariant_error'), 0)
        m = Model(); self.stage(m, b'PUT\nhttp://example.test/p\n')
        m.arg('arg_count', 1)
        self.assertEqual(m.call('probe_putgen'), -22)
        self.assertEqual(m.get('opens'), 0)
        self.assertEqual(m.call('probe_cfg_clear'), 0)
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
    def test_https_extended_finish_reply_and_http_status(self):
        expected = zlib.crc32(bytes((i * 29) & 255 for i in range(480)) + bytes((1,)))
        for code in (200, 422):
            with self.subTest(code=code):
                m = Model()
                m.arg('arg_count', 481)
                m.set('file_expected_crc', expected)
                m.set('file_http_status', code)
                self.assertEqual(m.call('probe_put'), 0 if code == 200 else -5)
                self.assertEqual(m.get('opened_hz'), 4000000)
                self.assertEqual(m.get('file_finished'), 1)
                self.assertEqual(m.get('invariant_error'), 0)

    def test_http_spec_and_response_span_multiple_frames(self):
        m = self.m
        m.set('file_source_size', 1)
        m.set('file_expected_crc', zlib.crc32(bytes((0,))))
        self.assertEqual(m.call('probe_http'), 0)
        self.assertEqual(m.get('opened_hz'), 4000000)
        self.assertEqual(m.get('http_meta'), 540)
        self.assertEqual(m.get('http_written'), 805)
        self.assertEqual(m.get('invariant_error'), 0)

    def test_https_real_file_range_and_bounds(self):
        offset, length = 773, 481
        m = Model()
        m.arg('arg_count', length)
        m.arg('arg_gap', offset)
        m.set('file_source_size', 2000)
        m.set('file_source_offset', offset)
        data = bytes((i ^ (i >> 8)) & 255 for i in range(offset, offset + length))
        m.set('file_expected_crc', zlib.crc32(data))
        self.assertEqual(m.call('probe_putfile'), 0)
        self.assertEqual(m.get('opened_hz'), 4000000)
        self.assertEqual(m.get('file_finished'), 1)
        self.assertEqual(m.get('invariant_error'), 0)

        m = Model()
        m.arg('arg_count', length)
        m.arg('arg_gap', offset)
        m.set('file_source_size', offset + length - 1)
        self.assertEqual(m.call('probe_putfile'), -22)
        self.assertEqual(m.get('opens'), 0)

    def test_stream_send_failure_aborts_without_finish(self):
        m = self.m
        m.arg('arg_count', 481)
        m.arg('arg_gap', 300)
        self.assertEqual(m.call('probe_stream'), -5)
        self.assertEqual(m.get('file_delivery_queries'), 6)
        self.assertEqual(m.get('file_finished'), 0)
        self.assertEqual(m.get('file_aborts'), 1)
        self.assertEqual(m.get('invariant_error'), 0)

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
    (out/'drivers/se').mkdir(parents=True,exist_ok=True)
    (out/'drivers/se/se.h').write_text(r'''
struct k_mutex {int unused;};
#define K_MUTEX_DEFINE(name) struct k_mutex name
static int k_mutex_lock(struct k_mutex *m,int timeout) {
 (void)m;if(timeout!=K_NO_WAIT || rng_locked)++invariant_error;
 if(rng_lock_fail)return -16;
 rng_locked=1;return 0;
}
static void k_mutex_unlock(struct k_mutex *m) {(void)m;rng_locked=0;}
''',encoding='utf-8')
    (out/'fs/fs.h').write_text(r'''
typedef int32_t off_t;
typedef int32_t ssize_t;
struct fs_file_t { uint32_t position; bool spec; };
struct fs_dirent { int unused; };
#define ENOENT 2
#define EEXIST 17
#define FS_O_CREATE 16
#define FS_O_WRITE 2
static inline int fs_stat(const char *p,struct fs_dirent *e){(void)p;(void)e;return -ENOENT;}
#define EFBIG 27
#define EBADMSG 74
#define ETIMEDOUT 110
#define FS_O_READ 1
#define FS_SEEK_SET 0
#define FS_SEEK_END 2
static inline void fs_file_t_init(struct fs_file_t *f) {(void)f;++fs_calls;}
static inline int fs_open(struct fs_file_t *f,const char *p,int m) {
    ++fs_calls;f->position=0;f->spec=p[0]=='s';return (m&FS_O_CREATE)||file_source_size?0:-1;
}
static inline int fs_seek(struct fs_file_t *f,off_t o,int w) {
    ++fs_calls;
    unsigned int size=f->spec?540u:file_source_size;
    if(o<0 || (uint32_t)o>size)return -1;
    f->position=w==FS_SEEK_END?size:(uint32_t)o;
    return 0;
}
static inline off_t fs_tell(struct fs_file_t *f) {++fs_calls;return f->position;}
static inline int fs_read(struct fs_file_t *f,void *p,size_t n) {
    uint8_t *bytes=p;
    size_t i;
    ++fs_calls;
    unsigned int size=f->spec?540u:file_source_size;
    if (n>size-f->position)return -1;
    for(i=0;i<n;++i) {
        bytes[i]=f->spec?(uint8_t)f->position:file_source_byte(f->position);
        ++f->position;
    }
    return n;
}
static inline ssize_t fs_write(struct fs_file_t *f,const void *p,size_t n) {
    const uint8_t *bytes=p;size_t i;(void)f;
    for(i=0;i<n;++i)
        if(bytes[i]!=(uint8_t)http_written++)++invariant_error;
    return n;
}
static inline int fs_close(struct fs_file_t *f) {(void)f;++fs_calls;return 0;}
''',encoding='utf-8')
    s=(MAIN/'system_app_shell.c').read_text(encoding='utf-8')
    start=s.index('static uint8_t s_swap_tx[')
    end=s.index('\n/**',s.index('    return ret;\n}',s.index('static int spi2swap_run')))
    h=old.HARNESS.replace('/* PRODUCTION_COMMAND */',STUBS+s[start:end])+EXTRA
    clock_source=(ROOT/'TAG_ZS3085E_241230/zephyr/soc/arm/actions/leopard/soc_clock.c').read_text(encoding='utf-8')
    clock_start=clock_source.index('#define SPI2_CLK_SRC_MASK')
    clock_end=clock_source.index('\n/**',clock_source.index('    return 0;\n}',clock_start))
    h += r'''
#define CONFIG_HOSC_CLK_MHZ 32
#define CMU_SPI2CLK 7u
#define ERANGE 34
uint32_t clock_register, clock_writes;
static uint32_t clock_read(uint32_t reg) {(void)reg;return clock_register;}
static void clock_write(uint32_t value,uint32_t reg) {(void)reg;clock_register=value;++clock_writes;}
#define sys_read32 clock_read
#define sys_write32 clock_write
#define printk(...) ((void)0)
''' + clock_source[clock_start:clock_end] + r'''
#undef sys_read32
#undef sys_write32
#undef printk
int probe_https_clock(void) {return acts_clk_set_rate_spi2(SFILE_HTTPS_HZ);}
int probe_1mhz_clock(void) {return acts_clk_set_rate_spi2(1000000u);}
'''
    h=h.replace('static void shell_print(', 'uint32_t logged_http_bytes, logged_group_busy, logged_group_wait;\nstatic void shell_print(', 1)
    h=h.replace('if(prefix(fmt,"SPI10 RESULT"))', '''if(prefix(fmt,"SPI HTTP DATA")) {
        unsigned int offset, i; const char *hex;
        (void)va_arg(a,unsigned int);offset=va_arg(a,unsigned int);hex=va_arg(a,const char *);
        if(offset!=logged_http_bytes)++invariant_error;
        for(i=0;hex[i];i+=2) {
            unsigned int high=hex[i]<='9'?hex[i]-'0':hex[i]-'a'+10;
            unsigned int low=hex[i+1]<='9'?hex[i+1]-'0':hex[i+1]-'a'+10;
            if((high*16+low)!=(uint8_t)logged_http_bytes)++invariant_error;
            ++logged_http_bytes;
        }
    } else if(prefix(fmt,"SPI HTTP FLOW")) {
        (void)va_arg(a,unsigned int);logged_group_busy=va_arg(a,unsigned int);
        logged_group_wait=va_arg(a,unsigned int);
    } else if(prefix(fmt,"SPI10 RESULT"))''', 1)
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
