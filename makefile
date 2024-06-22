SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin

ARCHIVIST := $(BIN_DIR)/archivist
DECODE := $(BIN_DIR)/archivist-decode
ENCODE := $(BIN_DIR)/archivist-encode
VERIFY := $(BIN_DIR)/archivist-verify

CPPFLAGS := -Iinclude -MMD -MP -D_FILE_OFFSET_BITS=64
CFLAGS := -Wall
LDFLAGS := -Llib
LDLIBS := -lfuse

OBJS := obj/blocks.o obj/sha1.o obj/blocks.o obj/logs.o

.phony: all clean testdata

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	@$(RM) -r $(BIN_DIR) $(OBJ_DIR)

all: $(BIN_DIR) $(ARCHIVIST) $(DECODE) $(ENCODE) $(VERIFY)

install: all
	sudo cp -f $(BIN_DIR)/archivist* /usr/local/bin/

$(ARCHIVIST): obj/archivist.o obj/sha1.o obj/blocks.o obj/seed.o obj/logs.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(DECODE): obj/decode.o obj/sha1.o obj/seed.o
	$(CC) $(LDFLAGS) $^ -o $@

$(ENCODE): obj/encode.o obj/sha1.o obj/seed.o
	$(CC) $(LDFLAGS) $^ -o $@

$(VERIFY): obj/verify.o obj/sha1.o obj/seed.o
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

-include $(OBJ:.o=.d)

reset: stop
	scripts/reset

start: $(ARCHIVE)
	scripts/start

stop:
	scripts/stop

test: start
	mkdir -p archive/testdata
	rsync --archive --verbose --compress --itemize-changes testdata/ archive/testdata/
	diff -r testdata/ archive/testdata/

test-decode: $(DECODE)
	@$(DECODE) archive1/testdata@/a.txt@ - > /tmp/a.txt
	@diff /tmp/a.txt testdata/a.txt
	@echo Test successful

test-encode: $(ENCODE) $(DECODE)
	@$(DECODE) archive1/testdata@/a.txt@ - | $(ENCODE) - - | $(DECODE) - - > /tmp/a.txt
	@diff /tmp/a.txt testdata/a.txt
	@echo Test successful

test-verify: $(VERIFY)
	@$(VERIFY) archive1/testdata@/a.txt@ 2>&1 | grep 'Verification of 2 blocks containing 525 data bytes'
	@dd if=archive1/testdata@/a.txt@ of=archive1/testdata@/c.txt@ bs=1 count=49 2>/dev/null
	@echo -n "C" >> archive1/testdata@/c.txt@
	@dd if=archive1/testdata@/a.txt@ of=archive1/testdata@/c.txt@ bs=1 conv=notrunc seek=50 skip=50 2>/dev/null
	@$(VERIFY) archive1/testdata@/c.txt@ 2>&1 | grep 'Invalid block hash when read block (0)'
	@echo Test successful
