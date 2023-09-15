EXTENSION = pg_cmd_queue

PG_CMDQD_DIR = $(CURDIR)/pg_cmdqd
PG_CMDQD_BUILD_TYPE ?= Release
PG_CMDQD_BIN = $(PG_CMDQD_BUILD_TYPE)/pg_cmd_queue_daemon

DISTVERSION = $(shell sed -n -E "/default_version/ s/^.*'(.*)'.*$$/\1/p" $(EXTENSION).control)

DATA = $(wildcard sql/$(EXTENSION)--*.sql)

REGRESS = test_extension_update_paths

# `pg_cmd_queue_daemon` is not a script, but when we call it a `PROGRAM_built`, PGXS wants to play with its
# object files—a task which we want to leave up to the CMake (wrapped by a GNU Makefile) in the daemon's
# subdirectory.
SCRIPTS_built = $(PG_CMDQD_DIR)/$(PG_CMDQD_BIN)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Set some environment variables for the regression tests that will be fed to `pg_regress`:
installcheck: export EXTENSION_NAME=$(EXTENSION)
installcheck: export EXTENSION_ENTRY_VERSIONS=$(patsubst sql/$(EXTENSION)--%.sql,%,$(wildcard sql/$(EXTENSION)--[0-99].[0-99].[0-99].sql))

README.md: sql/README.sql install
	psql --quiet postgres < $< > $@

META.json: sql/META.sql install
	psql --quiet postgres < $< > $@

dist: META.json README.md
	git archive --format zip --prefix=$(EXTENSION)-$(DISTVERSION)/ -o $(EXTENSION)-$(DISTVERSION).zip HEAD
	git archive --format zip --prefix=$(EXTENSION)-$(DISTVERSION)/ -o $(EXTENSION)-$(DISTVERSION).zip HEAD

test_dump_restore: TEST_DUMP_RESTORE_OPTIONS?=
test_dump_restore: $(CURDIR)/bin/test_dump_restore.sh sql/test_dump_restore.sql
	PGDATABASE=test_dump_restore \
		$< --extension $(EXTENSION) \
		$(TEST_DUMP_RESTORE_OPTIONS) \
		--psql-script-file sql/test_dump_restore.sql \
		--out-file results/test_dump_restore.out \
		--expected-out-file expected/test_dump_restore.out

.PHONY: $(PG_CMDQD_DIR)/$(PG_CMDQD_BIN)
$(PG_CMDQD_DIR)/$(PG_CMDQD_BIN): export CMAKE_BUILD_TYPE=$(PG_CMDQD_BUILD_TYPE)
$(PG_CMDQD_DIR)/$(PG_CMDQD_BIN):
	$(MAKE) -C $(PG_CMDQD_DIR) $(PG_CMDQD_BIN)

.PHONY: cmdqd_target_executable_path
.SILENT: cmdqd_target_executable_path
cmdqd_target_executable_path:
	echo $(abspath $(PG_CMDQD_DIR)/$(PG_CMDQD_BIN))

# Latch the daemon's cleaning chores onto PGXS its factory-default `clean`ing target.
clean: cmdqd-clean
.PHONY: cmdqd-clean
cmdqd-clean:
	$(MAKE) -C $(PG_CMDQD_DIR) clean
