CC       = x86_64-w64-mingw32-gcc
WINDRES  = x86_64-w64-mingw32-windres

VERSION  = 0.1.0

VER_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
VER_MINOR := $(word 2,$(subst ., ,$(VERSION)))
VER_PATCH := $(word 3,$(subst ., ,$(VERSION)))

CFLAGS   = -O2 -s -flto -mwindows \
           -DUNICODE -D_UNICODE \
           -DMRECORD_VERSION='"$(VERSION)"' \
           -Wall -Wextra -Wno-unused-parameter \
           $(CFLAGS_EXTRA)

CFLAGS_EXTRA ?=

RCFLAGS  = -DVER_MAJOR=$(VER_MAJOR) \
           -DVER_MINOR=$(VER_MINOR) \
           -DVER_PATCH=$(VER_PATCH)

LDLIBS   = -luser32 -lgdi32 -lshell32 -lole32 -luuid -ldwmapi -lwinmm \
           -ld3d11 -ldxgi -lmfplat -lmfreadwrite -lmfuuid

SRC_DIR  = src

PURE_SRCS = $(SRC_DIR)/args.c    \
            $(SRC_DIR)/geom.c    \
            $(SRC_DIR)/naming.c  \
            $(SRC_DIR)/pacing.c  \
            $(SRC_DIR)/convert.c

WIN_SRCS  = $(SRC_DIR)/main.c     \
            $(SRC_DIR)/console.c  \
            $(SRC_DIR)/control.c  \
            $(SRC_DIR)/monitors.c \
            $(SRC_DIR)/select.c   \
            $(SRC_DIR)/border.c   \
            $(SRC_DIR)/capture.c  \
            $(SRC_DIR)/cursor.c   \
            $(SRC_DIR)/encoder.c  \
            $(SRC_DIR)/audio.c    \
            $(SRC_DIR)/log.c

MRECORD_SRCS = $(PURE_SRCS) $(WIN_SRCS)
MRECORD_OBJS = $(MRECORD_SRCS:.c=.o)
RES_OBJ      = $(SRC_DIR)/mrecord.res.o

PURE_HDRS = $(SRC_DIR)/args.h $(SRC_DIR)/geom.h $(SRC_DIR)/naming.h \
            $(SRC_DIR)/pacing.h $(SRC_DIR)/convert.h

TARGET    = mrecord.exe

DISTNAME   = mrecord-$(VERSION)-win64
DISTDIR    = dist/$(DISTNAME)
DIST_FILES = README.md CHANGELOG.md MANUAL-TESTS.md LICENSE

HOST_CC   = cc
TEST_DIR  = test
TEST_BINS = $(TEST_DIR)/test_args    \
            $(TEST_DIR)/test_geom    \
            $(TEST_DIR)/test_naming  \
            $(TEST_DIR)/test_pacing  \
            $(TEST_DIR)/test_convert

.PHONY: all bump clean dist test print-version

all: $(TARGET)

bump:
	-git fetch --tags --quiet
	python3 tools/bump.py

print-version:
	@echo $(VERSION)

VERSION_STAMP = .version-$(VERSION)

$(VERSION_STAMP):
	@rm -f .version-*
	@touch $@

$(MRECORD_OBJS) $(RES_OBJ): $(VERSION_STAMP)

$(TARGET): $(MRECORD_OBJS) $(RES_OBJ)
	@echo "  LINK  $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(SRC_DIR)/log.o: $(SRC_DIR)/log.c $(SRC_DIR)/log.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/mrecord.h $(PURE_HDRS)
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(RES_OBJ): $(SRC_DIR)/mrecord.rc $(SRC_DIR)/mrecord.exe.manifest
	@echo "  RC    $<"
	$(WINDRES) $(RCFLAGS) -I$(SRC_DIR) -O coff -i $< -o $@

dist: $(TARGET)
	@echo "  DIST  $(DISTNAME)"
	rm -rf "$(DISTDIR)" "dist/$(DISTNAME).zip"
	mkdir -p "$(DISTDIR)"
	cp $(TARGET)     "$(DISTDIR)/"
	cp $(DIST_FILES) "$(DISTDIR)/"
	cd dist && python3 -m zipfile -c "$(DISTNAME).zip" "$(DISTNAME)"
	@echo "  ->    dist/$(DISTNAME).zip"

$(TEST_DIR)/test_%: $(TEST_DIR)/test_%.c $(SRC_DIR)/%.c $(SRC_DIR)/%.h $(TEST_DIR)/tests.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -o $@ $(TEST_DIR)/test_$*.c $(SRC_DIR)/$*.c -lm

test: $(TEST_BINS)
	@echo "  TEST"
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; \
	 if [ $$fail -ne 0 ]; then echo "  TESTS FAILED"; exit 1; fi; \
	 echo "  all tests passed"

clean:
	rm -f $(TARGET) $(MRECORD_OBJS) $(RES_OBJ) $(TEST_BINS)
	rm -f .version-*
