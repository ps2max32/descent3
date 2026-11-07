# Descent 3 (v1.5.0) on maxsdk: the PS2 port.
#
#   make [OUT=<dir>]     $(OUT)/descent3.elf
#   make cxxlib          picolibc (with POSIX I/O) and libc++ for this port,
#                        into $(OUT) (once; the ELF depends on them)
#   make run             descent3.elf in PCSX2 with the game data in
#                        build/pcsx2-test/d3 (D3DATA: a retail Descent 3
#                        directory, its hogs and missions copied there once)
#
# The game is C++17 (std::filesystem): libc++ and libc++abi from the LLVM
# tree, without exceptions and threads, on a picolibc built with POSIX I/O;
# ps2/ps2_posix.c puts open, read, stat, opendir ... on the IOP file server.
# SDL2 is not ported: ps2/ps2_sdl.cpp has the calls Descent 3 makes (pad as
# joystick, the GL calls from libs/libgl). The level scripts (OSIRIS
# DLLs) are linked in, each with its symbols made local but its exports.

HERE     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PS2MAX   ?= $(abspath $(HERE)/../..)
MAXSDK   ?= $(PS2MAX)/libs/maxsdk
MAXSDK_OUT ?= $(PS2MAX)/build/libs/maxsdk
# DEMO=1: the Descent 3 demo (its data: d3demo.hog), built apart
DEMO     ?= 0
LIBOUT   := $(PS2MAX)/build/apps/descent3
# MEMLOG=1 (below) builds apart too
# the game in virtual memory by default (started by mxlink), as the Quakes
MX_MEMORY ?= vm
OUT      ?= $(LIBOUT)$(if $(filter 1,$(DEMO)),-demo)$(if $(filter vm,$(MX_MEMORY)),-vm)$(if $(filter 1,$(MEMLOG)),-memlog)
all: $(OUT)/descent3.elf
MXGL     ?= $(PS2MAX)/libs/libgl
# frame arenas of 256 KB (the frames of the demo take up to 160 KB)
MXGL_ARENA_BYTES ?= 262144
# beam racing's records: a frame of Polaris is up to 191 KB of libgl's
# default 192 (three quarters of 256 KB); 384 KB each leaves room
MXGL_REC_BYTES ?= 393216
include $(MXGL)/mk/mxgl.mk
include $(MAXSDK)/mk/maxsdk.mk
# MX_MEMORY=vm (started by mxlink): the game in VM, libgl in core
MX_CORE_OBJS = $(MXGL_OBJS)

LLVM_SRC ?= $(PS2MAX_LLVM_SRC)
PICOLIBC_SRC ?= $(PS2MAX)/libs/picolibc
# this port's picolibc (POSIX I/O) and libc++
PICOLIBC := $(LIBOUT)/picolibc/install
LIBCXX   := $(LIBOUT)/libcxx/install
PICOLIBC_A := $(PICOLIBC)/lib/libc.a
LIBCXX_A   := $(LIBCXX)/lib/libc++.a
CXX      := $(LLVM)/clang++

LIBRT    = $(MAXSDK_OUT)/librt.a
# libmxc's malloc (TLSF), not picolibc's: that one clears what it hands out
# and walks the heap on free, which over virtual memory means swap traffic
LIBMXC_OBJS = $(OUT)/libc/glue.o $(MAXSDK_OUT)/libc/compat.c.o $(MAXSDK_OUT)/libc/atomic.c.o \
              $(MAXSDK_OUT)/libc/malloc.c.o
LIBC_LIBS = $(LIBMXC_OBJS) $(LIBCXX)/lib/libc++.a $(LIBCXX)/lib/libc++abi.a $(PICOLIBC_A) \
            $(MX_LIB) $(LIBRT)

TARGET   := --target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls -G0
SYSINC   := -isystem $(HERE)/ps2/include -isystem $(PICOLIBC)/include
# libc++ and picolibc for this port: same target flags as the game
LIB_FLAGS := $(TARGET) -nostdlibinc $(SYSINC) -ffunction-sections -fdata-sections -D_GNU_SOURCE -D_POSIX_TIMERS=1 -D_POSIX_MONOTONIC_CLOCK=200112L \
             -Wno-c++11-narrowing

ZLIB_SRC ?= $(PS2MAX)/libs/zlib
# OPT: the optimization of the game's code (-Os: 0.5 MiB less), HOT_OPT that
# of its frame's hot paths (HOT)
OPT ?= -Os
HOT_OPT ?= -O2
HOT := renderer/% physics/% vecmat/% fix/% model/% Descent3/render Descent3/terrainrender \
       Descent3/TerrainSearch Descent3/object Descent3/AImain Descent3/aipath Descent3/robotfire \
       Descent3/WeaponFire Descent3/fireball Descent3/viseffect Descent3/lighting \
       Descent3/object_lighting Descent3/postrender Descent3/renderobject Descent3/GameLoop \
       Descent3/room Descent3/BOA Descent3/spew Descent3/weather Descent3/procedurals \
       Descent3/splinter Descent3/special_face Descent3/gameevent ps2/%
# LOG=1: the game's log (mprintf) on the console
LOG ?= 1
# MEMLOG=1: the large allocations in the log (ps2/ps2_mem.c)
MEMLOG ?= 0
# SWAP: the swap file on the file server
SWAP ?= d3/descent3.swp
MEMWRAP := $(if $(filter 1,$(MEMLOG)),$(foreach f,malloc calloc realloc memalign free,--wrap=$(f)))

# the game's flags: hosted C++17, no exceptions; fast float math (the FPU
# has no doubles; float literals)
D3DEFS   := -DPOSIX -D__LINUX__ -D__PS2__ -DPRIMARY_HOG=\"d3-linux.hog\" -DRELEASE -DNDEBUG \
            $(if $(filter 1,$(LOG)),-DLOGGER) $(if $(filter 1,$(DEMO)),-DDEMO) $(if $(filter 1,$(MEMLOG)),-DPS2_MEMLOG) -DMEM_USE_RTL -DCHECKSUM=2273873307UL -D_GNU_SOURCE -D_USE_OGL_ACTIVE_TEXTURES -DSDL_MAIN_HANDLED
D3INC    := -I$(HERE)/cfile -I$(HERE)/ddebug -I$(HERE)/fix -I$(HERE)/lib -I$(HERE)/linux \
            -I$(HERE)/Descent3 -I$(HERE)/lib/linux -I$(HERE)/ps2/SDL2 -I$(OUT)/gen \
            $(foreach d,2dlib AudioEncode bitmap ddio grtext mem misc model music physics \
              renderer rtperformance sndlib stream_audio ui unzip libmve md5 third_party/libacm \
              third_party/stb netcon/includes,-I$(HERE)/$(d)) -I$(ZLIB_SRC) -I$(MXGL_INC)
COMMON   := $(TARGET) $(OPT) -G0 -fno-common -ffunction-sections -fdata-sections -MMD \
            -ffinite-math-only -fno-math-errno -ffast-math -fapprox-func -mvu0 \
            -Wno-multichar -w $(D3DEFS) $(D3INC)
D3CXXFLAGS := $(COMMON) -std=gnu++17 -fno-exceptions -nostdinc++ -nostdlibinc \
              -isystem $(LIBCXX)/include/c++/v1 $(SYSINC) \
              -include $(HERE)/ps2/ps2_port.h
D3CFLAGS   := $(COMMON) -std=gnu11 -nostdlibinc $(SYSINC)


# ---- the game's sources (the CMake lists of v1.5.0) ---------------------------
D3_SRCS := \
  $(addprefix 2dlib/,font hardsurf memsurf pen pentext screen surface viewport) \
  $(addprefix AudioEncode/,adecode aencode encoder) \
  $(addprefix bitmap/,NewBitmap bitmain bumpmap iff lightmap pcx tga) \
  $(addprefix cfile/,cfile hogfile inffile) \
  $(addprefix ddebug/,debug lnxdebug lnxmono) \
  $(addprefix linux/,lnxcon lnxcon_raw lnxcontroller lnxapp lnxcon_null lnxdata registry) \
  $(addprefix ddio/,chrono_timer ddio file key lnxforcefeedback lnxio sdljoy lnxkey lnxkey_null \
    lnxmouse lnxkey_sdl lnxfile) \
  dd_video/video_lnx fix/fix \
  $(addprefix manage/,doorpage gamefilepage generic manage megapage pagelock shippage soundpage \
    texpage weaponpage) \
  $(addprefix grtext/,grfont grtext textaux) \
  mem/mem \
  $(addprefix misc/,error logfile log psglob psrand pstring) \
  $(addprefix model/,newstyle polymodel) \
  module/module \
  $(addprefix music/,omflex sequencer streamer tracklist) \
  networking/networking \
  $(addprefix physics/,collide findintersection newstyle_fi physics) \
  $(addprefix renderer/,HardwareClipper HardwareDraw HardwareGlobalVars HardwareInstance \
    HardwareOpenGL HardwareBaseGPU HardwarePoints HardwareSetup HardwareTransforms lnxscreenmode) \
  rtperformance/rtperformance \
  $(addprefix sndlib/,hlsoundlib sndrender soundload ddsoundload ssl_lib mixer sdlgeometry sdlsound) \
  $(addprefix stream_audio/,osfarchive streamaudio) \
  $(addprefix ui/,UIButton UICombo UIConsole UIDraw UIEdit UIGadget UIGroup UIHotspot UIListBox \
    UIObject UIRes UISlider UIStatic UISystem UIWindow) \
  unzip/unzip vecmat/vector \
  $(addprefix libmve/,decoder8 decoder16 movie_sound mve_audio mvelib mveplay) \
  md5/md5 third_party/stb/stb \
  $(addprefix Descent3/,aiambient AIGoal AImain aipath aiterrain ambient args attach audiotaunts \
    bnode BOA Briefing BriefingParse bsp buddymenu cinematics cockpit config ConfigItem Controls \
    credits CtlCfgElem ctlconfig D3ForceFeedback d3movie d3music d3serial damage debuggraph \
    dedicated_server demofile descent difficulty DllWrappers door doorway fireball game Game2DLL \
    GameCheat gamecinematics gameevent gamefile gamefont GameLoop gamepath gamesave gamesequence \
    gametexture gauges help hotspotmap hud huddisplay hudmessage init Inventory levelgoal lighting \
    lightmap_info list LoadLevel loadstate localization marker matcen megacell menu Mission \
    mission_download mmItem multi multisafe multisafe_server multi_client multi_connect \
    multi_dll_mgr multi_save_setting multi_server multi_ui NewPyroGauges newui newui_core \
    newui_filedlg object object_lighting objinfo ObjInit ObjScript OsirisLoadandBind \
    osiris_predefs pilot PilotPicsAPI pilot_class Player postrender procedurals program \
    render renderobject robotfire room scorch screens ship SLEW SmallViews soar soar_helpers \
    special_face spew splinter subtitles TelCom TelComAutoMap TelComCargo TelComEffects \
    TelcomEffectsRender TelComGoals terrain terrainrender TerrainSearch trigger vclip viseffect \
    voice weapon WeaponFire weather sdlmain)
D3_C_SRCS := third_party/libacm/decode
ZLIB_C    := adler32 crc32 inffast inflate inftrees uncompr zutil
PS2_SRCS  := $(basename $(notdir $(wildcard $(HERE)/ps2/*.cpp)))
PS2_C     := $(basename $(notdir $(wildcard $(HERE)/ps2/*.c)))

# ---- the level scripts (OSIRIS modules), linked in --------------------------------
# Each script (and scripts/linux_lib.cpp) is linked into one object whose
# symbols are local but its exports, renamed <script>_<export>; the table
# $(OUT)/gen/ps2_modules.cpp lists them for dlopen and dlsym (ps2/ps2_dl.cpp).
SCRIPTS := aigame2 AIGame3 aigame4 AIGame barney BatteriesIncluded BossCamera CanyonsCTF \
  CellTestLevel ChrisTest clutter generic Geodomes HalfPipe InfernalBolt Inversion LEVEL0 \
  level10 level11 Level12 level13 level14 LEVEL15 Level16 level17 level1 level2 level3 level4 \
  level5 Level6 level7 level8 Level9 LevelS1 levelS2 Merc02 Merc1 Merc3 Merc4 merc5 Merc6 Merc7 \
  myPowerHouse Mysterious_Isle orbital Paranoia PiccuStation Polaris Quadsomniac RudeAwakening \
  SewerRat testscript TrainingMission Y2K
# the demo's levels and the game modules only (2.4 MiB less)
ifeq ($(DEMO),1)
SCRIPTS := PiccuStation Polaris generic AIGame aigame2 AIGame3 aigame4 clutter
endif
SCRIPT_EXPORTS := InitializeDLL ShutdownDLL GetGOScriptID GetTriggerScriptID GetCOScriptList \
  CreateInstance DestroyInstance CallInstanceEvent SaveRestoreState
SCRIPT_OBJS := $(patsubst %,$(OUT)/scripts/%.o,$(SCRIPTS))

GEN := $(OUT)/gen/d3_version.h
CXXDEPS := $(GEN) $(LIBCXX_A) $(HERE)/Makefile

$(OUT)/scripts/obj/%.o: $(HERE)/scripts/%.cpp $(CXXDEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(D3CXXFLAGS) -I$(HERE)/scripts -c $< -o $@

$(OUT)/scripts/%.o: $(OUT)/scripts/obj/%.o $(OUT)/scripts/obj/linux_lib.o
	$(LLVM)/ld.lld -m elf32ltsmipn32 -r -o $@.r $^
	$(LLVM)/llvm-objcopy $(patsubst %,--keep-global-symbol=%,$(SCRIPT_EXPORTS)) $@.r $@.l
	$(LLVM)/llvm-objcopy $(foreach e,$(SCRIPT_EXPORTS),--redefine-sym=$(e)=$*_$(e)) $@.l $@
	rm -f $@.r $@.l

$(OUT)/gen/ps2_modules.cpp: $(SCRIPT_OBJS) $(HERE)/Makefile
	{ echo '// generated by the Makefile from the scripts linked in'; \
	  echo '#include "ps2_modules.h"'; \
	  for m in $(SCRIPTS); do \
	    for e in $$($(LLVM)/llvm-nm -g --defined-only $(OUT)/scripts/$$m.o | awk '{print $$3}'); do \
	      echo "extern \"C\" char $$e;"; done; \
	    echo "static const ps2_export $${m}_exports[] = {"; \
	    for e in $$($(LLVM)/llvm-nm -g --defined-only $(OUT)/scripts/$$m.o | awk '{print $$3}'); do \
	      echo "  {\"$${e#$${m}_}\", &$$e},"; done; \
	    echo "  {0, 0}};"; done; \
	  echo "const ps2_module ps2_modules[] = {"; \
	  for m in $(SCRIPTS); do echo "  {\"$$m\", $${m}_exports},"; done; \
	  echo "  {0, 0}};"; } > $@

$(OUT)/gen/ps2_modules.o: $(OUT)/gen/ps2_modules.cpp
	$(CXX) $(D3CXXFLAGS) -I$(HERE)/ps2 -c $< -o $@

D3_OBJS := $(patsubst %,$(OUT)/d3/%.o,$(D3_SRCS)) $(patsubst %,$(OUT)/d3/%.o,$(D3_C_SRCS)) \
           $(patsubst %,$(OUT)/zlib/%.o,$(ZLIB_C)) \
           $(patsubst %,$(OUT)/ps2/%.o,$(PS2_SRCS)) $(patsubst %,$(OUT)/ps2/%.c.o,$(PS2_C)) \
           $(SCRIPT_OBJS) $(OUT)/gen/ps2_modules.o

$(GEN): $(HERE)/lib/d3_version.h.in
	@mkdir -p $(dir $@)
	sed -e 's/@PROJECT_VERSION_MAJOR@/1/' -e 's/@PROJECT_VERSION_MINOR@/5/' \
	  -e 's/@PROJECT_VERSION_PATCH@/0/' -e 's/@D3_GIT_HASH@/ps2/' $< > $@

# FRAMES: end after that many frames with a picture (SHOT, host:descent3.tga)
# (PLAY=1: frames of a level being played only, not of menus and loading);
# PROF=1: a profile (mx/prof.h, tools/prof.py) of the second half of them;
# ARGS: more of the game's command line; DIR: the game's directory on the
# file server. They go into $(OUT)/gen/runcfg.h, rewritten only when they
# change.
RUNCFG := $(OUT)/gen/runcfg.h
$(shell mkdir -p $(OUT)/gen; printf '#define PS2_FRAMES %s\n#define PS2_PLAY %s\n#define PS2_ARGS "%s"\n#define PS2_SHOT "%s"\n#define PS2_DIR "%s"\n#define PS2_SWAP "%s"\n#define PS2_PROF %s\n' \
  '$(or $(FRAMES),0)' '$(or $(PLAY),0)' '$(ARGS)' '$(or $(SHOT),host:descent3.tga)' '$(or $(DIR),/d3)' \
  '$(SWAP)' '$(or $(PROF),0)' > $(RUNCFG).new; \
  cmp -s $(RUNCFG).new $(RUNCFG) || mv $(RUNCFG).new $(RUNCFG); rm -f $(RUNCFG).new)

# the game's main is d3_main: ps2/ps2_sdl.cpp has main
$(OUT)/d3/Descent3/sdlmain.o: D3CXXFLAGS += -Dmain=d3_main

$(OUT)/d3/%.o: $(HERE)/%.cpp $(CXXDEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(D3CXXFLAGS) $(if $(filter $(HOT),$*),$(HOT_OPT)) -c $< -o $@
$(OUT)/d3/%.o: $(HERE)/%.c $(PICOLIBC_A)
	@mkdir -p $(dir $@)
	$(CC) $(D3CFLAGS) -c $< -o $@
$(OUT)/ps2/%.o: $(HERE)/ps2/%.cpp $(CXXDEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(D3CXXFLAGS) $(HOT_OPT) -I$(MAXSDK)/ee/include -c $< -o $@
$(OUT)/ps2/%.c.o: $(HERE)/ps2/%.c $(PICOLIBC_A)
	@mkdir -p $(dir $@)
	$(CC) $(D3CFLAGS) -I$(MAXSDK)/ee/include -c $< -o $@
$(OUT)/zlib/%.o: $(ZLIB_SRC)/%.c $(PICOLIBC_A)
	@mkdir -p $(dir $@)
	$(CC) $(D3CFLAGS) -c $< -o $@

# maxsdk's libc glue with its read, write and _exit renamed: ps2/ps2_posix.c
# has them (files), and passes the console's on
$(OUT)/libc/glue.o: $(MAXSDK)/ee/libc/glue.c $(PICOLIBC_A) $(HERE)/Makefile
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -ffreestanding,$(CFLAGS)) -nostdlibinc $(SYSINC) -Dread=mx_glue_read \
	  -Dwrite=mx_glue_write -D_exit=mx_glue_exit -c $< -o $@

$(OUT)/descent3.elf: $(D3_OBJS) $(MXGL_OBJS) $(HLSL_OBJS) $(MX_LIB) $(MX_CRT0) $(MX_LD) $(LIBC_LIBS)
	$(call link,$@,--error-limit=0 $(MEMWRAP) --defsym=__stack_size=0x80000 $(MX_CRT0) $(D3_OBJS) $(MXGL_OBJS) \
	  $(HLSL_OBJS) --start-group $(LIBC_LIBS) --end-group)

# ---- d3-linux.hog -----------------------------------------------------------------
# The 1.5 engine's own data (PRIMARY_HOG, opened last so searched first):
# its string tables (D3.STR in 1.5's order; the demo's d3.str has 399 in an
# older one), fonts and pictures, from scripts/data/fullhog by the host's
# HogMaker. The scripts (.so) are linked in, so left out.
HOGMAKER := $(LIBOUT)/hogmaker
$(HOGMAKER): $(wildcard $(HERE)/tools/HogMaker/*) $(GEN)
	c++ -std=c++17 -O2 -I$(HERE)/lib -I$(OUT)/gen $(HERE)/tools/HogMaker/HogFormat.cpp \
	  $(HERE)/tools/HogMaker/HogMaker.cpp -o $@
$(OUT)/d3-linux.hog: $(HOGMAKER) $(HERE)/scripts/data/fullhog/d3-linux-fullhog.txt
	grep -v -i '\.so$$' $(HERE)/scripts/data/fullhog/d3-linux-fullhog.txt > $(OUT)/gen/d3-linux-hog.txt
	$(HOGMAKER) $@ $(OUT)/gen/d3-linux-hog.txt $(HERE)/scripts/data/fullhog
all: $(OUT)/d3-linux.hog

# ---- picolibc and libc++ ------------------------------------------------------
cxxlib: $(LIBCXX_A)
# made by the libc++ build
$(LIBCXX)/lib/libc++abi.a: $(LIBCXX_A) ;
# libgl's objects (mk/mxgl.mk) include this port's picolibc headers
$(MXGL_OBJS): $(PICOLIBC_A)

$(PICOLIBC_A):
	@mkdir -p $(LIBOUT)/picolibc
	sed "s|@LLVM@|$(LLVM)|g" $(MAXSDK)/ee/libc/cross-r5900.txt.in > $(LIBOUT)/picolibc/cross.txt
	meson setup --wipe $(LIBOUT)/picolibc/build-meson $(PICOLIBC_SRC) \
	  --cross-file $(LIBOUT)/picolibc/cross.txt --prefix=$(PICOLIBC) -Dmultilib=false \
	  -Dpicocrt=false -Dsemihost=false -Dtests=false -Dthread-local-storage=false \
	  -Dposix-io=true -Dspecsdir=none -Dio-long-long=true -Dbuildtype=release
	ninja -C $(LIBOUT)/picolibc/build-meson install

$(LIBCXX_A): $(PICOLIBC_A)
	cmake -G Ninja -S $(LLVM_SRC)/runtimes -B $(LIBOUT)/libcxx/build \
	  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_INSTALL_PREFIX=$(LIBCXX) -DCMAKE_C_COMPILER=$(LLVM)/clang \
	  -DCMAKE_CXX_COMPILER=$(LLVM)/clang++ -DCMAKE_AR=$(LLVM)/llvm-ar \
	  -DCMAKE_RANLIB=$(LLVM)/llvm-ranlib -DCMAKE_C_FLAGS="$(LIB_FLAGS)" \
	  -DCMAKE_CXX_FLAGS="$(LIB_FLAGS)" -DCMAKE_SYSTEM_NAME=Generic \
	  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DRUNTIMES_USE_LIBC=picolibc \
	  -DLIBCXXABI_BAREMETAL=ON -DLIBCXXABI_ENABLE_ASSERTIONS=OFF \
	  -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
	  -DLIBCXXABI_ENABLE_THREADS=OFF -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
	  -DLIBCXXABI_USE_COMPILER_RT=ON -DLIBCXXABI_INCLUDE_TESTS=OFF \
	  -DLIBCXX_ENABLE_EXCEPTIONS=OFF -DLIBCXX_ENABLE_FILESYSTEM=ON \
	  -DLIBCXX_ENABLE_MONOTONIC_CLOCK=ON -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF \
	  -DLIBCXX_ENABLE_RTTI=ON -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXX_ENABLE_THREADS=OFF \
	  -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF -DLIBCXX_ENABLE_LOCALIZATION=ON \
	  -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
	  -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_USE_COMPILER_RT=ON -DLIBCXX_CXX_ABI=libcxxabi \
	  -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON -DLIBCXX_SHARED_OUTPUT_NAME=c++-shared \
	  -DLIBCXXABI_SHARED_OUTPUT_NAME=c++abi-shared
	ninja -C $(LIBOUT)/libcxx/build install-cxx install-cxxabi

# ---- running --------------------------------------------------------------------
D3DATA   ?=
RUNDIR   := $(PS2MAX)/build/pcsx2-test
# MX_MEMORY=vm: through mxlink's EMU build (host:mxload.txt names the game) on the
# EE interpreter, the only one of PCSX2's that raises TLB misses; as
# d3load.elf, since the runner stops the PCSX2 whose -elf it started (and
# other programs run from mxload.elf too)
run: $(OUT)/descent3.elf $(OUT)/d3-linux.hog
	@mkdir -p $(RUNDIR)/d3/missions
	@[ -z "$(D3DATA)" ] || cp -rnv "$(D3DATA)"/*.hog "$(D3DATA)"/missions $(RUNDIR)/d3/
	cp $(OUT)/d3-linux.hog $(RUNDIR)/d3/
ifeq ($(MX_MEMORY),vm)
	cp $(OUT)/descent3.elf $(RUNDIR)/descent3.elf
	printf 'host:descent3.elf' > $(RUNDIR)/mxload.txt
	cp $(MX_LOADER) $(OUT)/d3load.elf
	PCSX2_EE=interp $(PCSX2_RUN) $(OUT)/d3load.elf $(or $(TIMEOUT),3600)
else
	$(PCSX2_RUN) $(OUT)/descent3.elf $(or $(TIMEOUT),600)
endif

clean:
	rm -rf $(OUT)/d3 $(OUT)/ps2 $(OUT)/zlib $(OUT)/libc $(OUT)/gen $(OUT)/*.elf $(OUT)/*.map

.PHONY: all cxxlib run clean
-include $(shell find $(OUT)/d3 $(OUT)/ps2 -name '*.d' 2>/dev/null)
