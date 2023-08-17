CMAKE_BUILD_TYPES := Release Debug
DEFAULT_CMAKE_BUILD_TYPE := Release
EXECUTABLE_NAME := pg_cmd_queue_daemon

executable_targets := $(CMAKE_BUILD_TYPES:%=%/$(EXECUTABLE_NAME))
makefile_targets := $(CMAKE_BUILD_TYPES:%=%/Makefile)

.DEFAULT_GOAL := $(DEFAULT_CMAKE_BUILD_TYPE)/$(EXECUTABLE_NAME)

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

# vim: set noexpandtab tabstop=4 shiftwidth=4:
