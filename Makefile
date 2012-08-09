.PHONY: all, debug, profile, test, terminfo_local, lint, clean, distclean

SHELL = /bin/sh
CC    = gcc

INCS = -I.
LIBS = -lX11 -lutil
 
CFLAGS      += -std=gnu99 -pedantic -Wall -Wextra -march=native
LDFLAGS     += ${LIBS}
DEBUGFLAGS   = -O0 -D_DEBUG -ggdb3
RELEASEFLAGS = -O2 -DNDEBUG
 
TARGET  = terma
MAINSRC = src/$(TARGET).c
SOURCES = $(filter-out $(MAINSRC),$(shell echo src/*.c))
COMMON  = src/config.h src/keymap.h src/util.h src/types.h
HEADERS = $(shell echo src/*.h)
OBJECTS = $(SOURCES:.c=.o)
TESTSRC = $(shell echo test/unit/test_*.c)
TESTS   = $(notdir $(basename $(TESTSRC)))
INFO	=${TARGET}.info

 

all: $(TARGET) terminfo_local
 
$(TARGET): $(OBJECTS) $(COMMON) $(MAINSRC)
	$(CC) $(FLAGS) $(CFLAGS) $(RELEASEFLAGS) -o $(TARGET) $(OBJECTS) $(MAINSRC) ${LDFLAGS}

debug: $(SOURCES) $(HEADERS) $(COMMON)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) -o $(TARGET) $(SOURCES) $(MAINSRC) ${LDFLAGS}

profile: CFLAGS += -ggdb3
profile: $(TARGET)

test: ${TESTS}

$(TESTS): ${TESTSRC} ${SOURCES} ${HEADERS} ${COMMON}
	@echo "=== Testing $@ ==="
	$(CC) $(FLAGS) $(CFLAGS) -I src $(DEBUGFLAGS) -o $@ $(SOURCES) test/unit/$@.c ${LDFLAGS}
	@./$@
	@rm -f $@


$(INFO): res/$(INFO).in src/config.h src/keymap.h util/terminfogen.c
	@echo "Generating $(INFO)"
	@cp res/$(INFO).in $(INFO)
	@$(CC) $(FLAG) $(CFLAGS) -I src util/terminfogen.c -o terminfogen
	@./terminfogen >> $(INFO)
	@rm -f terminfogen

terminfo_local: $(INFO)
	tic -s $(INFO)
 
 
#install: release
#  install -D $(TARGET) $(BINDIR)/$(TARGET)
# 
#install-strip: release
#  install -D -s $(TARGET) $(BINDIR)/$(TARGET)
# 
#uninstall:
#	-rm $(BINDIR)/$(TARGET)
 
 
clean:
	-rm -f $(OBJECTS)
	-rm -f src/*.d
	-rm -f gmon.out
	-rm -f $(TARGET)
	-rm -f $(TESTS)
	-rm -f $(INFO)

info:
	echo $(OBJECTS)
 
%.o: %.c
	$(CC) $(FLAGS) $(CFLAGS) $(RELEASEFLAGS) -c -o $@ $<
	@$(CC) -MM $(CFLAGS) $*.c > $*.d


lint:
	splint $(SOURCES) -preproc +posixlib
