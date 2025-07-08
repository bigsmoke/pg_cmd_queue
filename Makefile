EXTENSION = pg_cmd_queue

PG_CMDQD_DIR_NAME := pg_cmdqd
PG_CMDQD_DIR_PATH := $(CURDIR)/$(PG_CMDQD_DIR_NAME)
PG_CMDQD_BUILD_TYPE ?= Release
PG_CMDQD_REGRESS_LAUNCHER := $(PG_CMDQD_DIR_NAME)/Debug/with_cmdqd
PG_CMDQD_DEBUG_TARGET := $(PG_CMDQD_DIR_NAME)/Debug/pg_cmdqd
PG_CMDQD_RELEASE_TARGET := $(PG_CMDQD_DIR_NAME)/Release/pg_cmdqd
PG_CMDQD_DEFAULT_TARGET := $(PG_CMDQD_DIR_NAME)/$(PG_CMDQD_BUILD_TYPE)/pg_cmdqd
PG_CMDQD_TARGETS := $(if $(or $(filter Debug,$(PG_CMDQD_BUILD_TYPE)),$\
                              $(filter installcheck,$(MAKECMDGOALS))),$\
                          $(PG_CMDQD_DEBUG_TARGET) $(PG_CMDQD_REGRESS_LAUNCHER))
PG_CMDQD_TARGETS += $(if $(filter Release,$(PG_CMDQD_BUILD_TYPE)),$\
                          $(PG_CMDQD_RELEASE_TARGET))
PG_CMDQD_DONT_BUILD ?=

DISTVERSION = $(shell sed -n -E "/default_version/ s/^.*'(.*)'.*$$/\1/p" $(EXTENSION).control)

DATA = $(wildcard sql/$(EXTENSION)--*.sql)

REGRESS = test_extension_update_paths

# We kinda need to use a temp. instance; or at least, we don't want `psql` to try to drop and recreate
# the database after with already launched `pg_cmdqd`.
REGRESS_OPTS += --launcher=$(PG_CMDQD_REGRESS_LAUNCHER) --temp-instance=$(CURDIR)/temp-instance

# `pg_cmd_queue_daemon` is not a script, but when we call it a `PROGRAM_built`, PGXS wants to play with its
# object files—a task which we want to leave up to the CMake (wrapped by a GNU Makefile) in the daemon's
# subdirectory.
ifeq ($(strip $(PG_CMDQD_DONT_BUILD)),)
SCRIPTS_built := $(PG_CMDQD_DEFAULT_TARGET)
endif

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# If we're running tests, we will want a debug build
installcheck: $(PG_CMDQD_DEBUG_TARGET)

# Set some environment variables for the regression tests that will be fed to `pg_regress`:
installcheck: export PATH:=$(dir $(CURDIR)/$(PG_CMDQD_DEBUG_TARGET)):$(PATH)
installcheck: export PG_CMDQD_BIN=$(CURDIR)/$(PG_CMDQD_DEBUG_TARGET)
installcheck: export PG_CMDQD_LOG_FILE=$(CURDIR)/results/pg_cmdqd.log
installcheck: export PG_CMDQD_ENV_TEST__EMPTY_VAR=
installcheck: export PG_CMDQD_ENV_TEST__DIFFICULT_VAR=spaces and even an = sign
installcheck: export PG_CMDQD_ENV_TEST__IGNORED=should not be passed
installcheck: export EXTENSION_NAME=$(EXTENSION)
installcheck: export EXTENSION_ENTRY_VERSIONS=$(patsubst sql/$(EXTENSION)--%.sql,%,$(wildcard sql/$(EXTENSION)--[0-99].[0-99].[0-99].sql))

README.md: sql/README.sql install
	psql --quiet postgres < $< > $@

META.json: sql/META.sql install
	psql --quiet postgres < $< > $@

dist: META.json README.md
	git archive --format zip --prefix=$(EXTENSION)-$(DISTVERSION)/ -o $(EXTENSION)-$(DISTVERSION).zip HEAD

test_dump_restore: TEST_DUMP_RESTORE_OPTIONS?=
test_dump_restore: $(CURDIR)/bin/test_dump_restore.sh sql/test_dump_restore.sql
	PGDATABASE=test_dump_restore \
		$< --extension $(EXTENSION) \
		$(TEST_DUMP_RESTORE_OPTIONS) \
		--psql-script-file sql/test_dump_restore.sql \
		--out-file results/test_dump_restore.out \
		--expected-out-file expected/test_dump_restore.out

.PHONY: $(PG_CMDQD_TARGETS)
$(PG_CMDQD_DEBUG_TARGET): export CMAKE_BUILD_TYPE=Debug
$(PG_CMDQD_RELEASE_TARGET): export CMAKE_BUILD_TYPE=Release
$(PG_CMDQD_TARGETS):
	$(MAKE) -C $(PG_CMDQD_DIR_PATH) $(patsubst $(PG_CMDQD_DIR_NAME)/%,%,$@)

.PHONY: cmdqd_default_target_executable_path
.SILENT: cmdqd_default_target_executable_path
cmdqd_default_target_executable_path:
	echo $(abspath $(PG_CMDQD_DEFAULT_TARGET))

# Latch the daemon's cleaning chores onto PGXS its factory-default `clean`ing target.
clean: cmdqd-clean
.PHONY: cmdqd-clean
cmdqd-clean:
	$(MAKE) -C $(PG_CMDQD_DIR_PATH) clean
