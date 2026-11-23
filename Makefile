# Lookout build.
#   make cross    -> device binary cross-compiled on a PC with Zig: build/cross/lookout
#   make package  -> Application Store .deb: build/pkg/lookout_<version>_arm64.deb
#   make deploy   -> build the .deb and install it on the device over ssh (DEVICE=cardputer)
#   make native   -> host build (compile check only; needs a Linux framebuffer to run)
#
# LVGL v9.5.0 is expected in deps/lvgl:
#   git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git deps/lvgl

LVGL_DIR ?= deps/lvgl
CC  ?= gcc
CXX ?= g++
COMMON   := -O2 -g0 -Isrc -Isrc/config -I$(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE -MMD -MP
CFLAGS   += $(COMMON)
CXXFLAGS += $(COMMON) -std=c++17 -Wall -Wextra

LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name '*.c' 2>/dev/null)
APP_SRCS  := src/platform/device.cpp src/media/media.cpp src/net/server.cpp src/app/preview.cpp \
             src/app/app.cpp src/main.cpp

define objs
$(patsubst %.c,$(1)/%.o,$(filter %.c,$(2))) $(patsubst %.cpp,$(1)/%.o,$(filter %.cpp,$(2)))
endef

OUT  ?= build/native
OBJS := $(call objs,$(OUT),$(LVGL_SRCS) $(APP_SRCS))

# The version lives in app-builder.json only (tools/bump_version.sh updates it).
VERSION ?= $(shell sed -n 's/^  "version": *"\([^"]*\)".*/\1/p' app-builder.json)
MAINTAINER ?= Bryan Feller <6282600+bfeller@users.noreply.github.com>
DEB := build/pkg/lookout_$(VERSION)_arm64.deb

.PHONY: all native cross package deploy version clean
all: native
native: $(OUT)/lookout

$(OUT)/lookout: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ -lm -lpthread -ldl

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Zig targets glibc 2.36 (Cardputer has 2.41) and links libc++ statically.
ZIG ?= tools/zig
ZFLAGS ?= -target aarch64-linux-gnu.2.36 -mcpu=cortex_a53
cross:
	$(MAKE) native OUT=build/cross CC="$(ZIG) cc $(ZFLAGS)" CXX="$(ZIG) c++ $(ZFLAGS)" LDFLAGS=-s

package: cross
	rm -rf build/pkg/root && mkdir -p build/pkg/root/DEBIAN
	install -D -m 755 build/cross/lookout build/pkg/root/usr/share/APPLaunch/bin/lookout
	install -D -m 644 packaging/lookout.desktop build/pkg/root/usr/share/APPLaunch/applications/lookout.desktop
	install -D -m 644 assets/images/lookout_100.png build/pkg/root/usr/share/APPLaunch/share/images/lookout.png
	install -D -m 644 LICENSE build/pkg/root/usr/share/doc/lookout/copyright
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@MAINTAINER@/$(MAINTAINER)/' \
	    -e "s/@SIZE@/$$(du -sk build/pkg/root/usr | cut -f1)/" packaging/control.in > build/pkg/root/DEBIAN/control
	chmod -R go-w build/pkg/root && chmod 755 build/pkg/root
	dpkg-deb --root-owner-group -Zxz -b build/pkg/root $(DEB)
	@echo "package: $(DEB)"

DEVICE ?= cardputer
deploy: package
	scp -q $(DEB) $(DEVICE):/tmp/lookout.deb
	ssh $(DEVICE) 'sudo apt-get install -y --reinstall /tmp/lookout.deb >/dev/null && rm /tmp/lookout.deb && \
	  systemctl --user restart APPLaunch && echo deployed'

version:
	@echo $(VERSION)

clean:
	rm -rf build

-include $(OBJS:.o=.d)
