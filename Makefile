
prfx   ?= 
cc     := $(prfx)gcc
cxx    := $(prfx)g++
ar     := $(prfx)ar
ranlib := $(prfx)ranlib
strip  := $(prfx)strip

# c version will be rtcthrd_c
name    := rtspserver
srcs    := utils.c stream_queue.c \
           rtsp_msg.c rtp_enc.c \
           rtsp_demo.c # $(wildcard *.c)
objs    := $(patsubst %.c,%.o,$(filter %.c, $(srcs)))
deps    := $(patsubst %.o,%.d,$(objs))
libs    := -lpthread
cflags   = -I. -DNDEBUG
cflags  += -std=gnu11 -D_DEFAULT_SOURCE -D__LINUX__ -Wno-unused-parameter
ldflags := 
# for reproducible build
objs    := $(sort $(objs))
# cflags  += -Wno-builtin-macro-redefined -U__FILE__ -D__FILE__=\"$(notdir $<)\"

targets := lib$(name).so lib$(name).a
all : $(targets)

clean : 
	rm -f $(targets)
	rm -f $(objs) $(deps)

lib$(name).so : $(objs)
	@$(cc) -shared -Wl,--gc-sections -Wl,--as-needed -Wl,--export-dynamic $(ldflags) $^ -o $@ $(libs)
	@$(strip) --strip-all $@
	$(info $(cc) -shared $(notdir $^) -o $(notdir $@))

lib$(name).a : $(objs)
	@$(ar) -crD $@ $^
	@$(ranlib) -D $@
	@$(strip) --strip-unneeded $@
	$(info $(ar) -crD $(notdir $@) $(notdir $^))

%.o : %.c
	@$(cc) -Os -Wall -Wextra -std=c11 -fPIC $(cflags) -c $< -o $@ -MMD -MF $*.d -MP
	$(info $(cc) -c $(notdir $<) -o $(notdir $@))

-include $(deps)
