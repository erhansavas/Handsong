# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Every Apple silicon Mac: compiled for the M1's instructions, tuned for the newest. The SDK and the deployment target are
# pinned, so that the Command Line Tools SDK can never be picked up by accident.

SDK := $(shell xcrun --sdk macosx --show-sdk-path)
SIGN_ID := $(shell security find-identity -v -p codesigning 2>/dev/null | grep -q "Handsong Local" && echo "Handsong Local" || echo "-")
OUT := build/h
APP := build/Handsong.app
BUNDLE_ID := io.github.erhansavas.handsong

MIN_MACOS := 15.0
MODE ?= release
TARGET := -arch arm64 -isysroot $(SDK) -mmacosx-version-min=$(MIN_MACOS)
ifeq ($(MODE),release)
  OPT := -O3 -mcpu=apple-m1 -mtune=apple-m4 -flto=full -DNDEBUG -fno-math-errno
  LINK_OPT := -O3 -mcpu=apple-m1 -mtune=apple-m4 -flto=full -Wl,-dead_strip
else ifeq ($(MODE),asan)
  OPT := -O1 -g -mcpu=apple-m1 -mtune=apple-m4 -fsanitize=address,undefined -fno-omit-frame-pointer
  LINK_OPT := -fsanitize=address,undefined
  OUT := build/h-asan
  APP := build/asan/Handsong.app
else ifeq ($(MODE),tsan)
  OPT := -O1 -g -mcpu=apple-m1 -mtune=apple-m4 -fsanitize=thread -fno-omit-frame-pointer
  LINK_OPT := -fsanitize=thread
  OUT := build/h-tsan
  APP := build/tsan/Handsong.app
endif

DEPS := -MMD -MP
WARN := -Wall -Wextra -Werror
INCLUDES := -Isrc -Ithird_party/opus/opus-1.5.2/include

# ---------------------------------------------------------------------------- First party
C_SRC := $(filter-out src/app/cpu_gate.c src/guardian/guardian.c,$(shell find src -name '*.c'))
M_SRC := $(shell find src -name '*.m')
CPP_SRC := $(shell find src -name '*.cpp')
C_OBJ := $(C_SRC:%.c=$(OUT)/%.o)
M_OBJ := $(M_SRC:%.m=$(OUT)/%.o)
CPP_OBJ := $(CPP_SRC:%.cpp=$(OUT)/%.o)
SWIFT_SRC := $(shell find src -name '*.swift')
SWIFT_OBJ := $(if $(SWIFT_SRC),$(OUT)/home.o,)

$(OUT)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	clang -std=gnu11 $(TARGET) $(OPT) $(WARN) $(DEPS) $(INCLUDES) -c $< -o $@

$(OUT)/src/%.o: src/%.m
	@mkdir -p $(dir $@)
	clang -std=gnu11 -fobjc-arc $(TARGET) $(OPT) $(WARN) $(DEPS) $(INCLUDES) -c $< -o $@

$(OUT)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -fno-exceptions -fno-rtti $(TARGET) $(OPT) $(WARN) $(DEPS) $(INCLUDES) -isystem third_party/jolt -isystem third_party -c $< -o $@

$(OUT)/home.o: $(SWIFT_SRC) src/app/home/bridge.h
	@mkdir -p $(dir $@)
	xcrun swiftc -O -wmo -parse-as-library -sdk $(SDK) -target arm64-apple-macos$(MIN_MACOS) -module-name Handsong -swift-version 5 \
	  -import-objc-header src/app/home/bridge.h -Isrc -emit-object -o $@ $(SWIFT_SRC)

# ---------------------------------------------------------------------------- The icon
# Absolute paths: actool hands them to a long-lived helper, which resolves relative ones from wherever it happened to start.
ICON_DIR := tools/icon/Handsong.icon
ICON_OUT := $(OUT)/icon
$(ICON_OUT)/Assets.car: $(wildcard $(ICON_DIR)/*) $(wildcard $(ICON_DIR)/Assets/*)
	@mkdir -p $(ICON_OUT)
	xcrun actool $(abspath $(ICON_DIR)) --compile $(abspath $(ICON_OUT)) --app-icon Handsong --enable-on-demand-resources NO --development-region en \
	  --target-device mac --platform macosx --minimum-deployment-target $(MIN_MACOS) --output-partial-info-plist $(abspath $(ICON_OUT))/partial.plist --output-format human-readable-text >/dev/null

# ---------------------------------------------------------------------------- What an Intel Mac runs instead: one message
$(OUT)/other_mac_x86_64: src/app/cpu_gate.c
	@mkdir -p $(dir $@)
	clang -std=gnu11 -arch x86_64 -isysroot $(SDK) -mmacosx-version-min=$(MIN_MACOS) -O2 $(WARN) $< \
	  -framework CoreFoundation -o $@

# ---------------------------------------------------------------------------- Third party
JOLT_SRC := $(shell find third_party/jolt/Jolt -name '*.cpp')
JOLT_OBJ := $(JOLT_SRC:%.cpp=$(OUT)/%.o)
$(OUT)/third_party/jolt/%.o: third_party/jolt/%.cpp
	@mkdir -p $(dir $@)
	clang++ -std=c++17 -fno-exceptions -fno-rtti $(TARGET) $(OPT) -w -Ithird_party/jolt -c $< -o $@

OPUS_DIR := third_party/opus/opus-1.5.2
include $(OPUS_DIR)/celt_sources.mk
include $(OPUS_DIR)/silk_sources.mk
include $(OPUS_DIR)/opus_sources.mk
OPUS_SRC := $(addprefix $(OPUS_DIR)/,$(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FLOAT) $(OPUS_SOURCES) $(OPUS_SOURCES_FLOAT))
OPUS_OBJ := $(OPUS_SRC:%.c=$(OUT)/%.o)
$(OUT)/$(OPUS_DIR)/%.o: $(OPUS_DIR)/%.c
	@mkdir -p $(dir $@)
	clang -std=gnu11 $(TARGET) $(OPT) -w -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF -DPACKAGE_VERSION=\"1.5.2\" \
	  -I$(OPUS_DIR)/include -I$(OPUS_DIR)/celt -I$(OPUS_DIR)/silk -I$(OPUS_DIR)/silk/float -I$(OPUS_DIR) -c $< -o $@

# ---------------------------------------------------------------------------- Shaders
METAL_SRC := $(wildcard shaders/*.metal)
$(OUT)/shaders.metallib: $(METAL_SRC) $(wildcard shaders/*.h)
	@mkdir -p $(dir $@)
	$(if $(METAL_SRC),xcrun metal -std=metal3.2 -O3 -arch air64 -mmacos-version-min=$(MIN_MACOS) -Wall -Werror -Ishaders -o $@ $(METAL_SRC),touch $@)

# ---------------------------------------------------------------------------- The app
FRAMEWORKS := -framework Foundation -framework AppKit -framework IOKit -framework AudioToolbox -framework CoreAudio \
  -framework GameController -framework AVFoundation -framework CoreMedia -framework Accelerate -framework Metal -framework QuartzCore \
  -framework ImageIO -framework CoreGraphics -framework CoreText -framework ServiceManagement -framework SwiftUI
SWIFT_LIBS := -L$(SDK)/usr/lib/swift -Wl,-rpath,/usr/lib/swift

$(OUT)/Handsong_arm64: $(C_OBJ) $(M_OBJ) $(CPP_OBJ) $(SWIFT_OBJ) $(JOLT_OBJ) $(OPUS_OBJ)
	clang++ $(TARGET) $(LINK_OPT) -o $@ $^ $(FRAMEWORKS) $(SWIFT_LIBS)

$(OUT)/guardian: src/guardian/guardian.c src/link/protocol.c src/link/protocol.h
	@mkdir -p $(dir $@)
	clang -std=gnu11 $(TARGET) -O2 $(WARN) -Isrc src/guardian/guardian.c src/link/protocol.c -framework CoreFoundation -framework IOKit -framework CoreAudio -o $@

ASSETS := $(wildcard assets/instrument/*.hs*)
SCORE := $(wildcard assets/instrument/score/*)

all: $(APP)/Contents/MacOS/Handsong
.DEFAULT_GOAL := all

$(APP)/Contents/MacOS/Handsong: $(OUT)/Handsong_arm64 $(OUT)/other_mac_x86_64 $(OUT)/guardian $(OUT)/shaders.metallib tools/Info.plist tools/entitlements.plist $(ASSETS) $(SCORE) $(ICON_OUT)/Assets.car tools/icon/Handsong.icns tools/icon/menubar.png
	@mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Helpers $(APP)/Contents/Resources/score
	cp tools/Info.plist $(APP)/Contents/Info.plist
	lipo -create $(OUT)/Handsong_arm64 $(OUT)/other_mac_x86_64 -output $@
	cp $(OUT)/guardian $(APP)/Contents/Helpers/handsong-guardian
	cp $(OUT)/shaders.metallib $(APP)/Contents/Resources/shaders.metallib
	cp $(ICON_OUT)/Assets.car tools/icon/Handsong.icns tools/icon/menubar.png $(APP)/Contents/Resources/
	$(if $(ASSETS),cp -R $(ASSETS) $(APP)/Contents/Resources/,)
	$(if $(SCORE),cp -R $(SCORE) $(APP)/Contents/Resources/score/,)
	codesign --force --options runtime --sign "$(SIGN_ID)" --identifier $(BUNDLE_ID).guardian $(APP)/Contents/Helpers/handsong-guardian
	codesign --force --options runtime --sign "$(SIGN_ID)" --identifier $(BUNDLE_ID) --entitlements tools/entitlements.plist $(APP)

-include $(C_OBJ:.o=.d) $(M_OBJ:.o=.d) $(CPP_OBJ:.o=.d)

# ---------------------------------------------------------------------------- Tools the gates use
$(OUT)/test/feel_tool: test/feel_tool.c src/listen/feel.c src/listen/feel.h src/listen/resampler.h
	@mkdir -p $(dir $@)
	clang -std=gnu11 $(TARGET) -O2 $(WARN) $(INCLUDES) $< -o $@

$(OUT)/test/speaker_tool: test/speaker_tool.c src/listen/speaker_chain.c $(OPUS_OBJ)
	@mkdir -p $(dir $@)
	clang -std=gnu11 $(TARGET) $(OPT) $(WARN) $(INCLUDES) test/speaker_tool.c src/listen/speaker_chain.c $(OPUS_OBJ) -o $@

test-tools: $(OUT)/test/feel_tool $(OUT)/test/speaker_tool

.PHONY: all analyze test-tools
analyze:
	@for f in $(C_SRC) $(M_SRC); do clang --analyze -Xclang -analyzer-output=text -std=gnu11 -fobjc-arc $(TARGET) $(INCLUDES) $$f -o /dev/null || exit 1; done
