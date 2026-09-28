# Astrolog (Version 8.00) File: Makefile (Unix version)
#
# IMPORTANT NOTICE: Astrolog and all chart display routines and anything
# not enumerated elsewhere in this program are Copyright (C) 1991-2026 by
# Walter D. Pullen (Astara@msn.com, http://www.astrolog.org/astrolog.htm).
# Permission is granted to freely use, modify, and distribute these
# routines provided these credits and notices remain unmodified with any
# altered or distributed versions of the program.
#
# First created 11/21/1991.
#
# MODIFIED VERSION NOTICE (GPL v2 section 2a): This file was modified on
# 2026-09-28 by Ryan Arthur, adding the Astrolog Studio targets at the end
# (studio, install-studio, uninstall-studio, clean-studio). This is not an
# official Astrolog release; see README.md.
#
# This Makefile is included only for convenience. One could easily compile
# Astrolog on most Unix systems by hand with the command:
# % cc -c -O *.cpp; cc -o astrolog *.o -lm -lX11
# Generally, all that needs to be done to compile once astrolog.h has been
# edited, is compile each source file, and link them together with the math
# library, and if applicable, the main X library.
#
NAME = astrolog
OBJS = astrolog.o atlas.o calc.o charts0.o charts1.o charts2.o charts3.o\
 data.o express.o general.o intrpret.o io.o matrix.o placalc.o placalc2.o\
 xdata.o xgeneral.o xdevice.o xcharts0.o xcharts1.o xcharts2.o xscreen.o\
 swecl.o swedate.o swehouse.o swejpl.o swemmoon.o swemplan.o sweph.o\
 swephlib.o

# If you don't have X windows, delete the "-lX11" part from the line below:
# If not compiling with GNUC, delete the "-ldl" part from the line below:
LIBS = -lm -lX11 -ldl -s
CPPFLAGS = -O -Wno-write-strings -Wno-narrowing -Wno-comment
RM = rm -f

$(NAME): $(OBJS)
	cc -o $(NAME) $(OBJS) $(LIBS)

clean:
	$(RM) $(OBJS) $(NAME)
#

# ---------------------------------------------------------------------------
# Astrolog Studio: a modern GTK 4 front end (see studio/README.md).
#   make studio          build ./astrolog-studio
#   make install-studio  install a desktop launcher for the current user
# The engine is compiled a second time without X11 and with the classic
# main() renamed, so the studio binary can also act as the CLI (--cli).

STUDIO = astrolog-studio
STUDIO_BUILD = build/studio
STUDIO_CORE = $(patsubst %.o,$(STUDIO_BUILD)/core/%.o,$(OBJS))
STUDIO_UI = $(STUDIO_BUILD)/engine.o $(STUDIO_BUILD)/theme.o \
 $(STUDIO_BUILD)/render.o $(STUDIO_BUILD)/store.o $(STUDIO_BUILD)/app.o
STUDIO_CXX = g++
STUDIO_CXXFLAGS = -O2 -std=c++17 -Wall -Wno-unused-function \
 $(shell pkg-config --cflags gtk4)
STUDIO_CORE_FLAGS = -O2 -w -DASTROLOG_NO_X11 -Dmain=astrolog_cli_main
STUDIO_LIBS = $(shell pkg-config --libs gtk4) -lm -ldl

studio: $(STUDIO)

$(STUDIO): $(STUDIO_CORE) $(STUDIO_UI)
	$(STUDIO_CXX) -o $@ $^ $(STUDIO_LIBS)

$(STUDIO_BUILD)/core/%.o: %.cpp astrolog.h extern.h
	@mkdir -p $(dir $@)
	$(STUDIO_CXX) $(STUDIO_CORE_FLAGS) -c $< -o $@

$(STUDIO_BUILD)/engine.o: studio/engine.cpp studio/engine.h astrolog.h extern.h
	@mkdir -p $(dir $@)
	$(STUDIO_CXX) $(STUDIO_CXXFLAGS) -w -DASTROLOG_NO_X11 -c $< -o $@

$(STUDIO_BUILD)/%.o: studio/%.cpp studio/*.h
	@mkdir -p $(dir $@)
	$(STUDIO_CXX) $(STUDIO_CXXFLAGS) -c $< -o $@

install-studio: $(STUDIO)
	studio/install.sh "$(CURDIR)"

uninstall-studio:
	studio/install.sh --uninstall

clean-studio:
	$(RM) -r $(STUDIO_BUILD) $(STUDIO)

.PHONY: studio install-studio uninstall-studio clean-studio
