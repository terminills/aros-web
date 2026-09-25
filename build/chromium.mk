# Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
# Where the Chromium tree the web libraries build against lives, and which
# revision it has to be.
#
# CHROMIUM_SRC is the `src` directory of a Chromium checkout carrying the AROS
# changes. It defaults to a sibling of the AROS source tree; override it on the
# make command line or in the environment. Every library derives its V8_SOURCE
# and CEF_SOURCE from it.
#
# build/chromium.pin names the commit each repository must be at (the main tree,
# V8 and CEF). A checkout at another commit still builds, but says so: a
# library built against an unpinned tree is not the one this revision of
# aros-web describes.

CHROMIUM_SRC ?= $(SRCDIR)/../chromium-aros/src

CHROMIUM_PIN_FILE := $(dir $(lastword $(MAKEFILE_LIST)))chromium.pin

ifneq ($(wildcard $(CHROMIUM_SRC)/.git),)
ifneq ($(wildcard $(CHROMIUM_PIN_FILE)),)
chromium_pin = $(shell awk '$$1 == "$(1)" { print $$2 }' $(CHROMIUM_PIN_FILE))
chromium_head = $(shell git -C $(CHROMIUM_SRC)/$(1) rev-parse HEAD 2>/dev/null)
$(foreach repo,. v8 cef,\
  $(if $(filter-out $(call chromium_pin,$(repo)),$(call chromium_head,$(repo))),\
    $(warning Chromium $(repo) is at $(call chromium_head,$(repo)), not the pinned $(call chromium_pin,$(repo)) (build/chromium.pin))))
endif
endif
