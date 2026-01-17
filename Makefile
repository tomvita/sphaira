#---------------------------------------------------------------------------------
# Sphaira Pure Makefile (No CMake)
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment.")
endif

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
#---------------------------------------------------------------------------------
# Project Settings
#---------------------------------------------------------------------------------
TARGET		:=	sphaira
BUILD		:=	build_make
SOURCES		:=	sphaira/source
INCLUDES	:=	sphaira/include
TOPDIR		:=	$(CURDIR)
ICON		:=	assets/icon.jpg
ROMFS		:=	$(BUILD)/romfs

#---------------------------------------------------------------------------------
# compiler / tools
#---------------------------------------------------------------------------------
PREFIX  := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
CC		:=	$(PREFIX)gcc
CXX		:=	$(PREFIX)g++
LD		:=	$(PREFIX)g++
ELF2NRO :=  $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool

# Fork Branding
APP_NAME    :=  "sphaira (Tomvita's Fork)"
APP_AUTHOR  :=  "Tomvita"
APP_VERSION :=  "1.0.0a"

# Fetch Git info if available
GIT_REV		:=	$(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
DISPLAY_VER :=  $(APP_VERSION)_$(GIT_REV)

#---------------------------------------------------------------------------------
# Compiler Flags
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ftls-model=local-exec

# Include paths for dependencies (using existing build/Release folders)
DEPS_DIR := $(TOPDIR)/build/Release/_deps
DEP_INCLUDES := \
	-I$(DEPS_DIR)/libpulsar-src/include \
	-I$(DEPS_DIR)/nanovg-src/include \
	-I$(DEPS_DIR)/stb-src \
	-I$(DEPS_DIR)/yyjson-src/src \
	-I$(DEPS_DIR)/minini-src/include \
	-I$(DEPS_DIR)/zstd-src/lib \
	-I$(DEPS_DIR)/libnxtc-src/include \
	-I$(DEPS_DIR)/dr_libs-src \
	-I$(DEPS_DIR)/pugixml-src/src \
	-I$(DEPS_DIR)/ftpsrv-src/src/platform \
	-I$(DEPS_DIR)/ftpsrv-src/src \
	-I$(DEPS_DIR)/ftpsrv-src/src/platform/nx \
	-I$(DEPS_DIR)/ftpsrv-src/src \
	-I$(DEPS_DIR)/libhaze-src/include \
	-I$(DEPS_DIR)/id3v2lib-src/include \
	-I$(DEPS_DIR)/libusbhsfs-src/include \
	-I$(DEPS_DIR)/libusbdvd-src/include \
	-I$(DEPS_DIR)/libnfs-src/include \
	-I$(DEPS_DIR)/libnfs-src/include/nfsc \
	-I$(DEPS_DIR)/libnfs-src/nfs \
	-I$(DEPS_DIR)/libsmb2-src/include

# Final Includes
PORTLIBS := $(DEVKITPRO)/portlibs/switch
INCLUDE	:=	$(DEP_INCLUDES) -I$(TOPDIR)/$(INCLUDES) -I$(TOPDIR)/$(SOURCES) -I$(TOPDIR)/$(SOURCES)/ff16 -I$(TOPDIR)/$(INCLUDES)/yati/nx/nxdumptool -I$(LIBNX)/include -I$(PORTLIBS)/include -I$(TOPDIR)/assets/embed -I$(TOPDIR)/build/Release/hbl/exefs

# Macros
# We manually escape double quotes for the shell
FTPSRV_DIR := $(TOPDIR)/build/Release/_deps/ftpsrv-src
BASE_DEFINES := -D__SWITCH__ -DAPP_VERSION=\"$(APP_VERSION)\" -DAPP_DISPLAY_VERSION=\"$(DISPLAY_VER)\" \
			-DCURL_NO_OLDIES=1 -DZSTD_STATIC_LINKING_ONLY=1 \
			-DENABLE_NVJPG -DENABLE_NSZ -DENABLE_LIBUSBHSFS -DENABLE_LIBUSBDVD \
			-DENABLE_FTPSRV -DENABLE_LIBHAZE -DENABLE_DEVOPTAB_HTTP -DENABLE_DEVOPTAB_NFS \
			-DENABLE_DEVOPTAB_SMB2 -DENABLE_DEVOPTAB_FTP -DENABLE_DEVOPTAB_WEBDAV \
			-DENABLE_AUDIO_MP3 -DENABLE_AUDIO_OGG -DENABLE_AUDIO_WAV -DENABLE_AUDIO_FLAC \
			-DFTP_VFS_HEADER=\"$(FTPSRV_DIR)/src/platform/nx/vfs_nx.h\" \
			-DFTP_SOCKET_HEADER=\"$(FTPSRV_DIR)/src/platform/nx/socket_nx.h\"

# Optimization / Mode selection
ifeq ($(filter dev,$(MAKECMDGOALS)),dev)
OPT     := -O2
DEFINES := $(BASE_DEFINES) -DDEV_BUILD=1
else
OPT     := -Oz -flto=auto -fno-fat-lto-objects
DEFINES := $(BASE_DEFINES) -DNDEBUG
endif

CFLAGS	:=	-g $(OPT) -Wall -Wextra -ffunction-sections -fdata-sections $(ARCH) $(DEFINES) $(INCLUDE) \
			--embed-dir=$(TOPDIR)/assets/embed --embed-dir=$(TOPDIR)/build/Release/hbl
CXXFLAGS:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++26

# Linker flags
LDFLAGS	:=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) $(OPT) -Wl,--gc-sections -Wl,-Map,$(TARGET).map \
			--embed-dir=$(TOPDIR)/assets/embed --embed-dir=$(TOPDIR)/build/Release/hbl

# Libraries from build folder
LIBDIRS	:= -L$(LIBNX)/lib -L$(PORTLIBS)/lib
LIBS	:= -ldeko3d -lnx -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lpthread -lm -lminizip

# Pre-built dependency archives from CMake build
DEP_A_DIR := $(TOPDIR)/build/Release
DEP_LIBS := \
	$(DEP_A_DIR)/_deps/id3v2lib-build/src/libid3v2lib.a \
	$(DEP_A_DIR)/_deps/libhaze-build/liblibhaze.a \
	$(DEP_A_DIR)/_deps/libnfs-build/lib/libnfs.a \
	$(DEP_A_DIR)/_deps/libpulsar-build/liblibpulsar.a \
	$(DEP_A_DIR)/_deps/libsmb2-build/lib/libsmb2.a \
	$(DEP_A_DIR)/_deps/libusbhsfs-build/liblibusbhsfs.a \
	$(DEP_A_DIR)/_deps/minini-build/libminIni.a \
	$(DEP_A_DIR)/_deps/nanovg-build/libnanovg.a \
	$(DEP_A_DIR)/_deps/pugixml-build/libpugixml.a \
	$(DEP_A_DIR)/_deps/yyjson-build/libyyjson.a \
	$(DEP_A_DIR)/_deps/zstd-build/lib/libzstd.a \
	$(DEP_A_DIR)/sphaira/binary_dir/libftpsrv.a \
	$(DEP_A_DIR)/sphaira/libfatfs.a \
	$(DEP_A_DIR)/sphaira/libftpsrv_helper.a \
	$(DEP_A_DIR)/sphaira/liblibnxtc.a \
	$(DEP_A_DIR)/sphaira/liblibusbdvd.a

#---------------------------------------------------------------------------------
# Source Discovery
#---------------------------------------------------------------------------------
SRCDIRS := $(SOURCES) $(SOURCES)/ui $(SOURCES)/ui/menus $(SOURCES)/usb \
           $(SOURCES)/yati $(SOURCES)/yati/container $(SOURCES)/yati/nx \
           $(SOURCES)/yati/source $(SOURCES)/utils $(SOURCES)/ff16 \
           $(SOURCES)/yati/nx/nxdumptool

CFILES   := $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.c))
CPPFILES := $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.cpp))
CPPFILES := $(filter-out %devoptab_sftp.cpp,$(CPPFILES))

OFILES   := $(addprefix $(BUILD)/,$(notdir $(CFILES:.c=.o)) $(notdir $(CPPFILES:.cpp=.o)))

VPATH := $(SRCDIRS)

#---------------------------------------------------------------------------------
# Targets
#---------------------------------------------------------------------------------
all: $(TARGET).nro

release: all

dev: all

$(BUILD):
	@mkdir -p $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp $(ROMFS)
	@echo "Creating NRO: $@"
	@$(ELF2NRO) $< $@ --icon=$(ICON) --nacp=$(TARGET).nacp --romfsdir=$(ROMFS)

$(ROMFS):
	@echo "Populating RomFS..."
	@mkdir -p $(ROMFS)
	@cp -rf $(TOPDIR)/assets/romfs/* $(ROMFS)/
	@mkdir -p $(ROMFS)/shaders
	@cp -f $(TOPDIR)/build/Release/_deps/nanovg-build/*.dksh $(ROMFS)/shaders/

$(TARGET).elf: $(BUILD) $(OFILES) Makefile
	@echo "Linking ELF: $@"
	@$(LD) $(LDFLAGS) $(LIBDIRS) $(OFILES) $(DEP_LIBS) $(LIBS) -o $@

$(TARGET).nacp: Makefile
	@echo "Generating NACP: $@"
	@$(NACPTOOL) --create $(APP_NAME) $(APP_AUTHOR) $(APP_VERSION) $@

# Rules for compiling
$(BUILD)/%.o: %.cpp
	@echo "CXX: $<"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.c
	@echo "CC:  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OFILES:.o=.d)

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp $(TARGET).map
	@echo Cleaned.

.PHONY: all clean
