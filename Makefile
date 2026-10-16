ifeq ($(HOST),1)
CC := cc
TARGET := build/xavatar-host
INSTALLER := build/xavatar-installer-host
PAYLOAD_RESOURCE := build/payload-host.c
else
ifndef PS4_PAYLOAD_SDK
$(error Set PS4_PAYLOAD_SDK or use make HOST=1)
endif
include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
TARGET := build/XAVATAR.elf
INSTALLER := build/XAVATAR-installer.elf
PAYLOAD_RESOURCE := build/payload.c
LDLIBS := -lSceNet -lSceUserService -lSceSystemService
endif

CPPFLAGS := -Iinclude -Ivendor -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
ifeq ($(HOST),1)
CPPFLAGS += -DXAVATAR_HOST
LDLIBS += -lm
endif
CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-function
SOURCES := source/main.c source/platform.c source/avatar.c build/assets.c vendor/miniz.c vendor/miniz_zip.c vendor/miniz_tdef.c vendor/miniz_tinfl.c

.PHONY: all clean package
all: $(TARGET) $(INSTALLER)
build/assets.c: ui.html styles.css app.js i18n.js assets/logo.png tools/embed_assets.py
	python3 tools/embed_assets.py $@
$(TARGET): $(SOURCES) $(wildcard include/*.h vendor/*.h) Makefile
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@ $(LDFLAGS) $(LDLIBS)
ifneq ($(HOST),1)
	$(STRIP) --strip-debug $@
endif
$(PAYLOAD_RESOURCE): $(TARGET) tools/embed_payload.py
	python3 tools/embed_payload.py $< $@
$(INSTALLER): $(SOURCES) source/installer.c $(PAYLOAD_RESOURCE) $(wildcard include/*.h vendor/*.h) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -DXAVATAR_INSTALLER $(SOURCES) source/installer.c $(PAYLOAD_RESOURCE) -o $@ $(LDFLAGS) $(LDLIBS)
ifneq ($(HOST),1)
	$(STRIP) --strip-debug $@
endif
package: all
	python3 tools/package.py
clean:
	rm -f build/XAVATAR.elf build/XAVATAR-installer.elf build/xavatar-host build/xavatar-installer-host build/assets.c build/payload.c build/payload-host.c
