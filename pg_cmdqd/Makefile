CMAKE_BUILD_TYPES := Release Debug
CMAKE_BUILD_TYPE := Release
EXECUTABLE_NAME := pg_cmdqd  # `pg_cmd_queue_daemon` exceeds the available 16 chars for a process name.

executable_targets := $(CMAKE_BUILD_TYPES:%=%/$(EXECUTABLE_NAME))
makefile_targets := $(CMAKE_BUILD_TYPES:%=%/Makefile)

.DEFAULT_GOAL := $(CMAKE_BUILD_TYPE)/$(EXECUTABLE_NAME)

all: $(executable_targets)

.SECONDEXPANSION:
$(executable_targets): $$(@D)/Makefile *.cpp *.h
	$(MAKE) -C "$(@D)" -j 2

$(makefile_targets): CMakeLists.txt *.cpp *.h
	cmake -DCMAKE_BUILD_TYPE="$(@D)" -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -S "$(CURDIR)" -B $(@D)
	ln -sf $(@D)/compile_commands.json .

.PHONY: clean
clean:
	rm -rf $(CMAKE_BUILD_TYPES)

.PHONY: target_executable_path
.SILENT: target_executable_path
target_executable_path:
	echo $(abspath $(CURDIR)/$(CMAKE_BUILD_TYPE)/$(EXECUTABLE_NAME))

# vim: set noexpandtab tabstop=4 shiftwidth=4:
