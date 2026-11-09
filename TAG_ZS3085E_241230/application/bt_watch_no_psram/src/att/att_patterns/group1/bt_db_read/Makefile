ATT_ROOT := $(CURDIR)/../../..
include $(ATT_ROOT)/att_patterns/scripts/config.mk

TARGET ?= p02_bta.$(OUTBINSUFFIX)

all:
	rm -rf link.ld
	cd $(IC_TYPE) && make || exit "$$?"
	$(CPF) $(IC_TYPE)/$(TARGET) ../../../att_bin/outbin

clean:
	cd $(IC_TYPE) && make clean || exit "$$?"
	#rm -rf $(ATT_ROOT)/att_bin/outbin/$(TARGET)
