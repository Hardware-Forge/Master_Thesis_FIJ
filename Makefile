KDIR        ?=
INCDIRS     ?=
SUDO        ?= sudo
CONFIGJSON  ?= ./fij_runner/config.json #relative path from the directory of this file

KMOD_DIR        := fij
USER_DIR        := fij_command
RUNNERCPP_DIR   := fij_runner

.PHONY: all build install uninstall \
        build-module install-module remove-module clean-module \
        build-user install-user uninstall-user clean-user \
        build-runnercpp clean-runnercpp \
        clean distclean run logs help	\

# Default: build everything (module first, then userspace)
all: build

############################
# Kernel module delegation #
############################
build-module:
	@$(MAKE) -C $(KMOD_DIR) $(if $(KDIR),KDIR=$(KDIR))

install-module:
	@$(MAKE) -C $(KMOD_DIR) $(if $(KDIR),KDIR=$(KDIR)) install-module

remove-module:
	@$(MAKE) -C $(KMOD_DIR) $(if $(KDIR),KDIR=$(KDIR)) remove-module

clean-module:
	@$(MAKE) -C $(KMOD_DIR) $(if $(KDIR),KDIR=$(KDIR)) clean

run:
	@$(MAKE) -C $(KMOD_DIR) run

logs:
	@$(MAKE) -C $(KMOD_DIR) logs

############################
# Start campaign           #
############################
start:
	@if [ -z $(CONFIGJSON) ]; then \
		echo "ERROR: CONFIGJSON is not set."; \
		echo "Please edit the Makefile and set CONFIGJSON to your config file path."; \
		exit 1; \
	fi
	@if [ ! -f $(CONFIGJSON) ]; then \
		echo "ERROR: CONFIGJSON file not found: $(CONFIGJSON)"; \
		exit 1; \
	fi
	./fij_runner/fij_app $(CONFIGJSON)

############################
# Userspace delegation     #
############################
build-user:
	@$(MAKE) -C $(USER_DIR) $(if $(INCDIRS),INCDIRS="$(INCDIRS)")

install-user:
	@$(MAKE) -C $(USER_DIR) $(if $(INCDIRS),INCDIRS="$(INCDIRS)") install

uninstall-user:
	@$(MAKE) -C $(USER_DIR) uninstall

clean-user:
	@$(MAKE) -C $(USER_DIR) clean

############################
# fij_runnercpp build/clean#
############################
build-runnercpp:
	@$(MAKE) -C $(RUNNERCPP_DIR)

clean-runnercpp:
	@$(MAKE) -C $(RUNNERCPP_DIR) clean

#####################################
# Ordered system-wide install steps #
#####################################
# IMPORTANT: install module first, then userspace.
install:
	@$(MAKE) install-module
	@$(MAKE) build-runnercpp

# Reverse order on uninstall: remove userspace, then module
uninstall:
	@$(MAKE) uninstall-user
	@$(MAKE) remove-module
	@$(MAKE) clean

############################
# Cleaning                 #
############################
clean: clean-module clean-user clean-runnercpp   # <-- added clean-runnercpp
distclean: clean

############################
# Help                     #
############################
help:
	@echo "Targets:"
	@echo "  build (default)      - build kernel module, userspace CLI, and runnercpp app"
	@echo "  install              - install kernel module, then userspace"
	@echo "  uninstall            - uninstall userspace, then remove kernel module"
	@echo "  build-module         - build only the kernel module"
	@echo "  install-module       - insmod the kernel module"
	@echo "  remove-module        - rmmod the kernel module"
	@echo "  build-user           - build only the userspace program"
	@echo "  install-user         - install the userspace program"
	@echo "  uninstall-user       - uninstall the userspace program"
	@echo "  build-runnercpp      - build only the C++ runner app"
	@echo "  clean-runnercpp      - clean only the C++ runner app"
	@echo "  run                  - load module and print usage hint"
	@echo "  logs                 - follow kernel logs"
	@echo "  clean                - clean all subprojects"
	@echo ""
	@echo "Variables:"
	@echo "  KDIR=/path/to/kernel/build   (override kernel build dir if needed)"
	@echo "  INCDIRS='-I... -I...'        (override userspace include dirs)"
