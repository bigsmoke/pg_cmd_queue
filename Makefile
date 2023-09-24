EXTENSION = pg_cmd_queue

PG_CMDQD_DIR = $(CURDIR)/pg_cmdqd
PG_CMDQD_BUILD_TYPE ?= Release
PG_CMDQD_BIN = $(PG_CMDQD_BUILD_TYPE)/pg_cmdqd
PG_CMDQD_PID_FILE = "$(CURDIR)/pg_cmdqd.pid"

DISTVERSION = $(shell sed -n -E "/default_version/ s/^.*'(.*)'.*$$/\1/p" $(EXTENSION).control)

DATA = $(wildcard sql/$(EXTENSION)--*.sql)

REGRESS = test_extension_update_paths

# `REGRESS_PREP` is not documented on https://www.postgresql.org/docs/current/extend-pgxs.html
#  Maybe one should send in a documentation patch… one day…
#REGRESS_PREP = launch_background_daemon

# `REGRESS_KILL` is a concoction of this here `Makefile`; it is _not_ recognized by PGXS.
# Therefore, we will need some hackery to have these targets execute _after
#REGRESS_KILL = murder_background_daemon

REGRESS_OPTS += --load-extension=hstore --load-extension=pg_cmd_queue --launcher=$(CURDIR)/bin/with_cmdqd.sh

# Overriding `pg_regress_installcheck` too ventures a bit outside of the
#override pg_regress_installcheck = $(pg_regress_installcheck) ; echo "Extraaaaa"

#.PHONY: launch_background_daemon
#launch_background_daemon:
#	bg-child -- -- pg_cmdqd --pid-file $(PG_CMDQD_PID_FILE) --background

#.PHONY:
#kill_background_deamon:
#	kill $(shell cat "$(CURDIR)/pg_cmdqd.pid")

# `pg_cmd_queue_daemon` is not a script, but when we call it a `PROGRAM_built`, PGXS wants to play with its
# object files—a task which we want to leave up to the CMake (wrapped by a GNU Makefile) in the daemon's
# subdirectory.
SCRIPTS_built = $(PG_CMDQD_DIR)/$(PG_CMDQD_BIN)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Set some environment variables for the regression tests that will be fed to `pg_regress`:
installcheck: export CMDQD_BIN=$(PG_CMDQD_DIR)/$(PG_CMDQD_BIN)
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
