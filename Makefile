
dtop := ../..
include $(dtop)/platform.conf
ddist := ../dist/$(CHIP_TYPE)

prfx   ?= $(TARGET)-
cc     := $(prfx)gcc
cxx    := $(prfx)g++
ar     := $(prfx)ar
ranlib := $(prfx)ranlib
strip  := $(prfx)strip

srcs    := $(wildcard *.c) $(wildcard *.cpp)
objs    := $(patsubst %.c,%.o,$(filter %.c, $(srcs))) \
           $(patsubst %.cpp,%.o,$(filter %.cpp, $(srcs)))
deps    := $(patsubst %.o,%.d,$(objs))
libs    := -lpthread
cflags   = -I. -D_DEFAULT_SOURCE -g
cflags  += # -Wno-maybe-uninitialized -Wno-sign-compare -Wno-strict-aliasing -Wno-type-limits
# for reproducible build
objs    := $(sort $(objs))
# cflags  += -Wno-builtin-macro-redefined -U__FILE__ -D__FILE__=\"$(notdir $<)\"

targets := $(ddist)/librtsp_client.a
all : $(targets)

clean : 
	rm -f $(targets)
	rm -f $(objs) $(deps)

$(ddist)/librtsp_client.a : $(objs)
	@$(ar) -crD $@ $^
	@$(ranlib) -D $@
	@$(strip) --strip-unneeded $@
	$(info $(ar) -crD $(notdir $@) $(notdir $^))

%.o : %.c
	@$(cc) -Os -Wall -Wextra -std=c11 -fPIC -D_POSIX_C_SOURCE=200809L $(cflags) -c $< -o $@ -MMD -MF $*.d -MP
	$(info $(cc) -c $(notdir $<) -o $(notdir $@))

%.o : %.cpp
	@$(cxx) -Os -Wall -Wextra -std=c++11 -fPIC $(cflags) -c $< -o $@ -MMD -MF $*.d -MP
	$(info $(cc) -c $(notdir $<) -o $(notdir $@))

-include $(deps)
