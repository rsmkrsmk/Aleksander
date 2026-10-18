LV=lvgl
CFLAGS=-O2 -DLV_CONF_INCLUDE_SIMPLE -Iinc -I$(LV) -I$(LV)/src -w
SRCS=$(shell find $(LV)/src -name '*.c')
OBJS=$(patsubst %.c,build/%.o,$(SRCS))
build/%.o: %.c
	@mkdir -p $(dir $@)
	@gcc $(CFLAGS) -c $< -o $@
liblv.a: $(OBJS)
	@ar rcs $@ $(OBJS)
sim_%: scen_%.h sim.cpp liblv.a
	g++ -O2 -DLV_CONF_INCLUDE_SIMPLE -Iinc -I$(LV) -I. -I/home/claude/aleksander -DSIM_SCENARIOS='"scen_$*.h"' -w sim.cpp liblv.a -lm -o $@
