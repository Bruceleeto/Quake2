# Makefile for Quake 2 - Dreamcast

ifneq ($(MAKECMDGOALS),assets)
include $(KOS_BASE)/Makefile.rules
endif

BUILDDIR = build_dreamcast

CC = kos-cc
CFLAGS = -std=gnu23 -O3 -fipa-pta -fno-omit-frame-pointer -fbuiltin -ffast-math -ffp-contract=fast -mfsrra -mfsca -ffunction-sections -fdata-sections -flto -funroll-loops -DNDEBUG -Dstricmp=strcasecmp -Wno-missing-braces -Wno-unused-variable -I$(KOS_BASE)/utils
CFLAGS_O3 = -std=gnu23 -O3 -fipa-pta -fno-omit-frame-pointer -fbuiltin -ffast-math -ffp-contract=fast -mfsrra -mfsca -ffunction-sections -fdata-sections -flto -funroll-loops -DNDEBUG -Dstricmp=strcasecmp -Wno-missing-braces -Wno-unused-variable -I$(KOS_BASE)/utils
CFLAGS_GL = -std=gnu23 -O3 -fipa-pta -fno-omit-frame-pointer -fbuiltin -ffast-math -ffp-contract=fast -mfsrra -mfsca -ffunction-sections -fdata-sections -flto -funroll-loops -DNDEBUG -Dstricmp=strcasecmp -Wno-missing-braces -Wno-unused-variable -I$(KOS_BASE)/utils
CFLAGS_GL_RMAIN = -std=gnu23 -O3 -fipa-pta -fno-omit-frame-pointer -fbuiltin -ffast-math -ffp-contract=fast -mfsrra -mfsca -ffunction-sections -fdata-sections -flto -funroll-loops -DNDEBUG -Dstricmp=strcasecmp -Wno-missing-braces -Wno-unused-variable -I$(KOS_BASE)/utils
LDFLAGS = 
# LDFLAGS = -Wl,--gc-sections -s -Wl,--strip-all
KOS_LIBS += -lsh4zam -lfastmem -lm -Wl,-Map=quake2.map

TARGET = quake2.elf

DO_CC = $(CC) $(CFLAGS) -o $@ -c $<
DO_CC_O3 = $(CC) $(CFLAGS_O3) -o $@ -c $<
DO_CC_GL = $(CC) $(CFLAGS_GL) -o $@ -c $<
DO_CC_GL_RMAIN = $(CC) $(CFLAGS_GL_RMAIN) -o $@ -c $<

#############################################################################
# ASSET TOOLS
#############################################################################

TOOLS_DIR = tools
PAKEXTRACT_DIR = $(TOOLS_DIR)/pakextract-master
PAKEXTRACT = $(TOOLS_DIR)/pakextract
WAL2PNG_DIR = $(TOOLS_DIR)/wal2png-master
WAL2PNG = $(WAL2PNG_DIR)/wal2png
DCACONV_DIR = $(TOOLS_DIR)/dcaconv-main
DCACONV = $(TOOLS_DIR)/dcaconv

#############################################################################
# BUILD TARGETS
#############################################################################

.PHONY: all clean dirs assets

all: dirs $(BUILDDIR)/$(TARGET)

dirs:
	@mkdir -p $(BUILDDIR)/client $(BUILDDIR)/ref_gl $(BUILDDIR)/net $(BUILDDIR)/sound $(BUILDDIR)/game

#############################################################################
# ASSET PIPELINE
#############################################################################

$(PAKEXTRACT):
	$(MAKE) -C $(PAKEXTRACT_DIR)
	cp $(PAKEXTRACT_DIR)/pakextract $@

$(WAL2PNG):
	$(MAKE) -C $(WAL2PNG_DIR)

$(DCACONV):
	$(MAKE) -C $(DCACONV_DIR)
	cp $(DCACONV_DIR)/dcaconv $@

assets: $(PAKEXTRACT) $(WAL2PNG) $(DCACONV)
	@echo "=== Extracting PAK files ==="
	@./tools/extract.sh
	@echo "=== Converting PCX to PNG ==="
	@./tools/convert_pcx.sh
	@echo "=== Converting WAL to PNG ==="
	@./tools/wal2png.sh
	@echo "=== Converting audio assets ==="
	@./tools/convert_audio.sh
	@echo "=== Converting PNG to DT ==="
	@./tools/convert_dt.sh
	@echo "=== Converting CIN to MPG ==="
	@./tools/convert_cin.sh

#############################################################################
# MAIN EXECUTABLE
#############################################################################

QUAKE2_OBJS = \
	$(BUILDDIR)/client/cl_cin.o \
	$(BUILDDIR)/client/mpeg.o \
	$(BUILDDIR)/client/cl_ents.o \
	$(BUILDDIR)/client/cl_fx.o \
	$(BUILDDIR)/client/cl_newfx.o \
	$(BUILDDIR)/client/cl_input.o \
	$(BUILDDIR)/client/cl_inv.o \
	$(BUILDDIR)/client/cl_main.o \
	$(BUILDDIR)/client/cl_parse.o \
	$(BUILDDIR)/client/cl_pred.o \
	$(BUILDDIR)/client/cl_tent.o \
	$(BUILDDIR)/client/cl_scrn.o \
	$(BUILDDIR)/client/cl_view.o \
	$(BUILDDIR)/client/console.o \
	$(BUILDDIR)/client/keys.o \
	$(BUILDDIR)/client/menu.o \
	$(BUILDDIR)/client/snd_dma.o \
	$(BUILDDIR)/client/snd_mem.o \
	$(BUILDDIR)/client/snd_mix.o \
	$(BUILDDIR)/client/qmenu.o \
	$(BUILDDIR)/client/cmd.o \
	$(BUILDDIR)/client/cmodel.o \
	$(BUILDDIR)/client/common.o \
	$(BUILDDIR)/client/crc.o \
	$(BUILDDIR)/client/cvar.o \
	$(BUILDDIR)/client/files.o \
	$(BUILDDIR)/client/md4.o \
	$(BUILDDIR)/client/net_chan.o \
	$(BUILDDIR)/client/sv_ccmds.o \
	$(BUILDDIR)/client/sv_ents.o \
	$(BUILDDIR)/client/sv_game.o \
	$(BUILDDIR)/client/sv_init.o \
	$(BUILDDIR)/client/sv_main.o \
	$(BUILDDIR)/client/sv_send.o \
	$(BUILDDIR)/client/sv_user.o \
	$(BUILDDIR)/client/sv_world.o \
	$(BUILDDIR)/client/cd_null.o \
	$(BUILDDIR)/client/q_hunk.o \
	$(BUILDDIR)/client/vid_menu.o \
	$(BUILDDIR)/client/vid_lib.o \
	$(BUILDDIR)/client/q_system.o \
	$(BUILDDIR)/client/glob.o \
	$(BUILDDIR)/client/pmove.o \
	$(BUILDDIR)/net/net_loopback.o \
	$(BUILDDIR)/sound/snddma_null.o \
	$(BUILDDIR)/port_platform_unix.o

GAME_OBJS = \
	$(BUILDDIR)/game/g_ai.o \
	$(BUILDDIR)/game/p_client.o \
	$(BUILDDIR)/game/g_cmds.o \
	$(BUILDDIR)/game/g_svcmds.o \
	$(BUILDDIR)/game/g_combat.o \
	$(BUILDDIR)/game/g_func.o \
	$(BUILDDIR)/game/g_items.o \
	$(BUILDDIR)/game/g_main.o \
	$(BUILDDIR)/game/g_misc.o \
	$(BUILDDIR)/game/g_monster.o \
	$(BUILDDIR)/game/g_phys.o \
	$(BUILDDIR)/game/g_save.o \
	$(BUILDDIR)/game/g_spawn.o \
	$(BUILDDIR)/game/g_target.o \
	$(BUILDDIR)/game/g_trigger.o \
	$(BUILDDIR)/game/g_turret.o \
	$(BUILDDIR)/game/g_utils.o \
	$(BUILDDIR)/game/g_weapon.o \
	$(BUILDDIR)/game/m_actor.o \
	$(BUILDDIR)/game/m_berserk.o \
	$(BUILDDIR)/game/m_boss2.o \
	$(BUILDDIR)/game/m_boss3.o \
	$(BUILDDIR)/game/m_boss31.o \
	$(BUILDDIR)/game/m_boss32.o \
	$(BUILDDIR)/game/m_brain.o \
	$(BUILDDIR)/game/m_chick.o \
	$(BUILDDIR)/game/m_flipper.o \
	$(BUILDDIR)/game/m_float.o \
	$(BUILDDIR)/game/m_flyer.o \
	$(BUILDDIR)/game/m_gladiator.o \
	$(BUILDDIR)/game/m_gunner.o \
	$(BUILDDIR)/game/m_hover.o \
	$(BUILDDIR)/game/m_infantry.o \
	$(BUILDDIR)/game/m_insane.o \
	$(BUILDDIR)/game/m_medic.o \
	$(BUILDDIR)/game/m_move.o \
	$(BUILDDIR)/game/m_mutant.o \
	$(BUILDDIR)/game/m_parasite.o \
	$(BUILDDIR)/game/m_soldier.o \
	$(BUILDDIR)/game/m_supertank.o \
	$(BUILDDIR)/game/m_tank.o \
	$(BUILDDIR)/game/p_hud.o \
	$(BUILDDIR)/game/p_trail.o \
	$(BUILDDIR)/game/p_view.o \
	$(BUILDDIR)/game/p_weapon.o \
	$(BUILDDIR)/game/q_shared.o \
	$(BUILDDIR)/game/g_chase.o \
	$(BUILDDIR)/game/m_flash.o

GL_OBJS = \
	$(BUILDDIR)/ref_gl/gl_draw.o \
	$(BUILDDIR)/ref_gl/gl_image.o \
	$(BUILDDIR)/ref_gl/gl_light.o \
	$(BUILDDIR)/ref_gl/gl_mesh.o \
	$(BUILDDIR)/ref_gl/gl_model.o \
	$(BUILDDIR)/ref_gl/gl_rmain.o \
	$(BUILDDIR)/ref_gl/gl_rsurf.o \
	$(BUILDDIR)/ref_gl/gl_warp.o \
	$(BUILDDIR)/ref_gl/port_gl_sdl.o

$(BUILDDIR)/$(TARGET): $(QUAKE2_OBJS) $(GAME_OBJS) $(GL_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(KOS_LIBS)

#############################################################################
# COMPILATION RULES
#############################################################################

$(BUILDDIR)/client/%.o: client/%.c
	$(DO_CC)

$(BUILDDIR)/client/%.o: qcommon/%.c
	$(DO_CC)

$(BUILDDIR)/client/%.o: server/%.c
	$(DO_CC)

$(BUILDDIR)/client/%.o: null/%.c
	$(DO_CC)

$(BUILDDIR)/client/%.o: other/%.c
	$(DO_CC)

$(BUILDDIR)/net/%.o: net/%.c
	$(DO_CC)

$(BUILDDIR)/sound/%.o: sound/%.c
	$(DO_CC)

$(BUILDDIR)/port_platform_unix.o: port_platform_unix.c
	$(DO_CC)

# q_shared.c uses -O3
$(BUILDDIR)/game/q_shared.o: game/q_shared.c
	$(DO_CC_O3)

$(BUILDDIR)/game/%.o: game/%.c
	$(DO_CC)

# GL objects use -Os except gl_rmain.c
$(BUILDDIR)/ref_gl/gl_rmain.o: ref_gl/gl_rmain.c
	$(DO_CC_GL_RMAIN)

$(BUILDDIR)/ref_gl/%.o: ref_gl/%.c
	$(DO_CC_GL)

$(BUILDDIR)/ref_gl/port_gl_sdl.o: port_gl_sdl.c
	$(DO_CC_GL)

#############################################################################
# CLEAN
#############################################################################

clean:
	rm -rf $(BUILDDIR)