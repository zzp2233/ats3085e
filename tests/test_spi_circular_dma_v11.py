"""Production ARM circular RX driver, finite TX and hardware HF/TC model."""
from pathlib import Path
import argparse
import subprocess
import sys
import unittest
import test_spi2_dma_v5 as old
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ
SPI,DMA=old.SPI,old.DMA
ROOT=Path(__file__).resolve().parents[1]

CONFIG=r'''
uint32_t start_counts[2], lengths[2], position, progressed, hardware_blocks;
void sys_write32(uint32_t v,uint32_t a) { wr(a,sys_read32(a)&~v); }
int dma_config(const struct device *d,uint32_t ch,struct dma_config *c) {
 (void)d;++configs;event(200+ch);
 if ((fault==3 && ch==0)||(fault==4 && ch==1)) return -EIO;
 if(c->block_count!=1||c->dma_slot!=9||c->source_data_size!=1||c->dest_data_size!=1)
   invariant_error=1;
 source[ch]=c->head_block->source_address;dest[ch]=c->head_block->dest_address;
 lengths[ch]=c->head_block->block_size;
 wr(reg(ch,0),(fault!=21&&(c->head_block->source_reload_en||c->head_block->dest_reload_en))?1u<<18:0);
 wr(reg(ch,0x1c),lengths[ch]);return 0;
}
int dma_start(const struct device *d,uint32_t ch) {
 (void)d;++starts;++start_counts[ch];event(300+ch);
 if(!(dev_cfg.spi->ctrl&SPI_CTL_SS)||(dev_cfg.spi->ctrl&SPI_CTL_TX_DRQ_EN))invariant_error=1;
 if((fault==5&&ch==0)||(fault==6&&ch==1))return -EIO;
 wr(DMA_REG_BASE,sys_read32(DMA_REG_BASE)&~((1u<<ch)|(1u<<(ch+16))));
 wr(reg(ch,4),1);if(ch==1)progressed=0;return 0;
}
int dma_get_status(const struct device *d,uint32_t ch,struct dma_status *s) {
 uint32_t sta=SPI_STATUS_TX_EMPTY|SPI_STATUS_RX_EMPTY,n,i,pending=0;
 (void)d;if(fault==15)return -EIO;
 if(ch==1 && !progressed && fault!=7) {
  n=fault==19?256:lengths[1];
  for(i=0;i<n;++i) {
   ((uint8_t*)dest[0])[position]=((uint8_t*)source[1])[i]^0x55;
   ++position;
   if(position==lengths[0]/2)pending|=1u<<16;
   if(position==lengths[0]) {pending|=1u;position=0;}
  }
  wr(reg(0,0x1c),lengths[0]-position);
  if(fault==20)pending^=(1u|(1u<<16));
  wr(DMA_REG_BASE,sys_read32(DMA_REG_BASE)|pending);
  wr(reg(1,0x1c),fault==8?3:0);
  wr(reg(1,4),fault==9?1:0);
  ++hardware_blocks;progressed=1;
 }
 if(fault==10)sta|=SPI_STATUS_RX_FIFO_WERR;
 if(fault==11)sta&=~SPI_STATUS_TX_EMPTY;
 if(fault==12)sta|=SPI_STATUS_BUSY;
 if(fault==13)sta&=~SPI_STATUS_RX_EMPTY;
 dev_cfg.spi->status=sta;
 s->busy=sys_read32(reg(ch,4))!=0;s->pending_length=spi2_dummy_pending;
 return 0;
}
'''.replace('spi2_dummy_pending','sys_read32(reg(ch,0x1c))')
EXTRA=r'''
#include "spi_acts_spi2_circular.h"
static struct spi_config circular_config;
int probe_open(void) {
 circular_config.frequency=input_hz;circular_config.operation=input_op;
 circular_config.slave=input_slave;circular_config.cs=(void*)input_cs;
 return spi_acts_spi2_circular_open(&dev,&circular_config);
}
int probe_transfer(void) { return spi_acts_spi2_circular_transfer(&dev,input,output); }
int probe_close(void) { return spi_acts_spi2_circular_close(&dev); }
int probe_active(void) { return spi2_circ.active; }
int probe_blocks(void) { return spi2_circ.blocks; }
'''

class Model(old.Model):
    def __init__(self):
        self.pending_status=None
        self.drain_trigger=None
        self.drain_final=None
        self.drain_due=False
        self.drain_last_block=0
        self.drain_injected=0
        self.wrap_run_drop=False
        self.wrap_run_stuck=False
        super().__init__()
        self.uc.hook_add(UC_HOOK_CODE,self.on_code)
        self.uc.hook_add(UC_HOOK_MEM_READ,self.on_read)
        self.set('input_len',512)
    def on_write(self,uc,access,address,size,value,data):
        if address==SPI+4 and value==0xb00:self.pending_status=self.read(SPI+4)&~value
        if (address==SPI+4 and value==0x50 and self.drain_trigger is not None and
                self.get('hardware_blocks')>self.drain_last_block):
            # TX/wire finished, but the last eight RX bytes are still in FIFO.
            # Deliver their DMA completion at a selected later CPU MMIO read,
            # independently of dma_get_status(), as real hardware can do.
            self.drain_last_block=self.get('hardware_blocks')
            self.drain_final=(self.read(DMA),self.read(DMA+0x11c))
            pending,remaining=self.drain_final
            self.uc.mem_write(DMA,old.struct.pack('<I',pending&~((1<<16)|1)))
            self.uc.mem_write(DMA+0x11c,old.struct.pack('<I',(remaining%1024)+8))
            if self.wrap_run_drop and remaining == 1024:
                self.uc.mem_write(DMA+0x104,old.struct.pack('<I',0))
            self.pending_status=value&~0x40
        super().on_write(uc,access,address,size,value,data)
    def on_read(self,uc,access,address,size,value,data):
        if self.drain_final is not None and address==self.drain_trigger:
            # Hooks run before the load; finish after this instruction so the
            # CPU keeps the pre-completion value that it actually sampled.
            self.drain_due=True
    def on_code(self,uc,addr,size,data):
        if self.pending_status is not None:
            self.uc.mem_write(SPI+4,old.struct.pack('<I',self.pending_status));self.pending_status=None
        if self.drain_due:
            pending,remaining=self.drain_final
            self.uc.mem_write(DMA,old.struct.pack('<I',pending))
            self.uc.mem_write(DMA+0x11c,old.struct.pack('<I',remaining))
            self.uc.mem_write(SPI+4,old.struct.pack('<I',0x50))
            if self.wrap_run_drop and remaining == 1024 and not self.wrap_run_stuck:
                self.uc.mem_write(DMA+0x104,old.struct.pack('<I',1))
            self.drain_final=None;self.drain_due=False;self.drain_injected+=1

class RingTests(unittest.TestCase):
    def setUp(self):self.m=Model()
    def check_delayed_rx_drain(self,address):
        m=self.m;self.assertEqual(m.call('probe_open'),0)
        m.drain_trigger=address
        for i in range(4):
            self.assertEqual(m.call('probe_transfer'),0,'block '+str(i))
            self.assertEqual(m.output(),bytes((j^(j>>8)^0x55)&255 for j in range(512)))
            self.assertEqual(m.read(DMA+0x11c),1024 if i&1 else 512)
        self.assertEqual(m.drain_injected,4)
        self.assertEqual(m.call('probe_blocks'),4)
        self.assertEqual(m.read(m.sym['start_counts']),1)
        self.assertEqual(m.call('probe_close'),0)
        self.assertEqual(m.get('invariant_error'),0)
    def test_last_rx_burst_after_spi_status_read(self):
        self.check_delayed_rx_drain(SPI+4)
    def test_last_rx_burst_after_pending_read(self):
        self.check_delayed_rx_drain(DMA)
    def test_last_rx_burst_after_remaining_read(self):
        self.check_delayed_rx_drain(DMA+0x11c)
    def test_sampled_run_drop_at_ring_wrap_recovers(self):
        self.m.wrap_run_drop=True
        self.check_delayed_rx_drain(DMA+0x104)
    def test_persistent_run_drop_at_ring_wrap_is_fatal(self):
        m=self.m
        self.assertEqual(m.call('probe_open'),0)
        self.assertEqual(m.call('probe_transfer'),0)
        m.drain_trigger=DMA+0x104
        m.wrap_run_drop=m.wrap_run_stuck=True
        self.assertEqual(m.call('probe_transfer'),-5)
        self.assertEqual(m.call('probe_blocks'),1)
        self.assertEqual(m.call('probe_transfer'),-1)
        self.assertEqual(m.call('probe_close'),0)
    def test_many_halves_and_wraps_start_rx_once(self):
        m=self.m;self.assertEqual(m.call('probe_open'),0)
        for i in range(10):
            self.assertEqual(m.call('probe_transfer'),0)
            self.assertEqual(m.output(),bytes((j^(j>>8)^0x55)&255 for j in range(512)))
            self.assertEqual(m.read(DMA+0x11c),1024 if i&1 else 512)
            self.assertEqual(m.read(DMA+0x104),1)
            self.assertEqual(m.read(DMA),0)
        self.assertEqual(m.read(m.sym['start_counts']),1)
        self.assertEqual(m.read(m.sym['start_counts']+4),10)
        self.assertEqual(m.get('configs'),11)
        self.assertEqual(m.call('probe_blocks'),10)
        self.assertEqual(m.clock_starts,10)
        self.assertEqual(m.call('probe_close'),0)
        self.assertEqual(m.get('locks'),0);self.assertEqual(m.get('frees'),2)
        self.assertEqual(m.get('invariant_error'),0)
    def test_tx_never_reloads_while_rx_does(self):
        m=self.m;self.assertEqual(m.call('probe_open'),0);self.assertEqual(m.call('probe_transfer'),0)
        self.assertTrue(m.read(DMA+0x100)&(1<<18));self.assertFalse(m.read(DMA+0x200)&(1<<18))
    def test_open_allocation_config_start_faults_unwind(self):
        for fault,free in ((1,0),(2,1),(3,2),(5,2),(18,2),(21,2)):
            m=Model();m.set('fault',fault)
            self.assertLess(m.call('probe_open'),0)
            self.assertEqual(m.clock_starts,0);self.assertEqual(m.get('locks'),0)
            self.assertEqual(m.get('frees'),free)
    def test_transfer_faults_disable_clocks_and_require_close(self):
        for fault in (4,6,7,8,9,10,11,12,13,15,19,20):
            m=Model();self.assertEqual(m.call('probe_open'),0);m.set('fault',fault)
            self.assertLess(m.call('probe_transfer'),0)
            self.assertEqual(m.call('probe_transfer'),-1)
            self.assertEqual(m.call('probe_blocks'),0)
            self.assertEqual(m.output(),b'\x77'*512)
            self.assertTrue(m.read(SPI)&8);self.assertFalse(m.read(SPI)&0xc3)
            self.assertEqual(m.call('probe_close'),0);self.assertEqual(m.get('locks'),0)
    def test_wrong_event_or_unexpected_pending_cannot_be_a_completion(self):
        m=self.m;m.call('probe_open');m.uc.mem_write(DMA,old.struct.pack('<I',1))
        self.assertEqual(m.call('probe_transfer'),-5);self.assertEqual(m.clock_starts,0)
    def test_other_channels_pending_bits_are_preserved(self):
        m=self.m;m.call('probe_open');m.uc.mem_write(DMA,old.struct.pack('<I',(1<<5)|(1<<21)))
        self.assertEqual(m.call('probe_transfer'),0)
        self.assertEqual(m.read(DMA),(1<<5)|(1<<21))
    def test_stop_fault_quarantines_both_buffers_and_channels(self):
        m=self.m;m.call('probe_open');m.set('fault',16)
        self.assertLess(m.call('probe_close'),0)
        self.assertEqual(m.get('frees'),0);self.assertEqual(m.get('locks'),0)
        self.assertEqual(m.call('probe_open'),-5)
    def test_invalid_configuration_before_lock_and_allocation(self):
        for key,val in [('input_hz',0),('input_hz',8000001),('input_op',0),('input_slave',1),('input_cs',1)]:
            m=Model();m.set(key,val);self.assertEqual(m.call('probe_open'),-22)
            self.assertEqual(m.get('allocations'),0);self.assertEqual(m.get('locks'),0)
    def test_nested_session_rejected_and_closed_transfer_rejected(self):
        m=self.m;self.assertEqual(m.call('probe_open'),0)
        self.assertEqual(m.call('probe_open'),-16);self.assertEqual(m.get('locks'),1)
        self.assertEqual(m.call('probe_close'),0);self.assertEqual(m.call('probe_transfer'),-1)
    def test_timeout_survives_counter_wrap(self):
        m=self.m;m.call('probe_open');m.set('fault',7);m.set('cycles',0xfffff000)
        self.assertEqual(m.call('probe_transfer'),m.call('probe_timeout_errno'))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--driver-dir',type=Path)
    p.add_argument('--opt',choices=('O0','Os'),default='O0');a=p.parse_args()
    out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    driver=(a.driver_dir or ROOT/'TAG_ZS3085E_241230/zephyr/drivers/spi').resolve()
    raw=(driver/'spi_acts.c').read_text(encoding='utf-8')
    begin=raw.index('#define SPI_CTL_CLK_SEL_MASK');end=raw.rfind('/**',0,raw.index('bool spi_acts_transfer_ongoing'))
    (out/'spi_types.h').write_text(raw[begin:end],encoding='utf-8')
    h=old.HARNESS.replace('uint32_t block_size, source_address, dest_address;',
        'uint32_t block_size, source_address, dest_address, source_reload_en, dest_reload_en;')
    lo=h.index('int dma_config(');hi=h.index('int dma_stop(',lo)
    h=h[:lo]+CONFIG+h[hi:]+EXTRA
    source=out/'circular_master.c';source.write_text(h,encoding='utf-8');old.ELF=out/'circular_master.elf'
    compiler=Path('D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/startup/app_release/app_tools/cross_tools/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe')
    # Unicorn 2.1.4's memory-write hook can leak Thumb IT state across a
    # function return. Disable if-conversion in this optional model only;
    # production sources and SDK compiler flags are not changed.
    model_flags=['-fno-if-conversion','-fno-if-conversion2'] if a.opt=='Os' else []
    subprocess.run([str(compiler),'-mcpu=cortex-m4','-mthumb','-'+a.opt,'-Wall','-Wextra','-Werror',
        '-fno-builtin','-ffunction-sections','-fdata-sections','-nostdlib','-Wl,--gc-sections']+model_flags+
        ['-Wl,--undefined='+n for n in ('probe_reset','probe_open','probe_transfer','probe_close','probe_blocks','probe_timeout_errno')]+
        ['-Wl,-e,probe_reset','-Wl,-Ttext=0x100000','-Wl,-Tdata=0x2ff70000','-Wl,--section-start=.bss=0x2ff71000',
        '-I',str(driver),'-I',str(out),str(source),'-o',str(old.ELF)],check=True)
    unittest.main(argv=[sys.argv[0]],verbosity=2)
