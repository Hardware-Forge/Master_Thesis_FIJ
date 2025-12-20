KDIR        ?=
INCDIRS     ?=
SUDO        ?= sudo
CONFIGJSON  ?= ./fij_runner/config.json #relative path from the directory of this file

KMOD_DIR        := fij
RUNNERCPP_DIR   := fij_runner

# --- Detect Package Manager ---
ifneq ($(shell which apt-get 2>/dev/null),)
    PKG_MANAGER := apt
    INSTALL_CMD := sudo apt-get update && sudo apt-get install -y
    # Package names for Debian/Ubuntu
    PKGS := build-essential linux-headers-$(shell uname -r) libopencv-dev nlohmann-json3-dev
else ifneq ($(shell which dnf 2>/dev/null),)
    PKG_MANAGER := dnf
    INSTALL_CMD := sudo dnf install -y
    # Package names for Fedora/RHEL
    PKGS := kernel-devel kernel-headers opencv-devel nlohmann-json-devel gcc-c++
else ifneq ($(shell which pacman 2>/dev/null),)
    PKG_MANAGER := pacman
    INSTALL_CMD := sudo pacman -S --noconfirm
    # Package names for Arch Linux
    PKGS := linux-headers opencv nlohmann-json base-devel
else
    PKG_MANAGER := unknown
endif

.PHONY: all build install uninstall \
        build-module install-module remove-module clean-module \
        build-user install-user uninstall-user clean-user \
        build-runnercpp clean-runnercpp \
        clean distclean run logs help	\
		deps	\

# Default: build everything (module first, then userspace)
all: deps build

############################
# Dependencies installation #
############################
deps:
	@echo "Detected Package Manager: $(PKG_MANAGER)"
ifneq ($(PKG_MANAGER),unknown)
	$(INSTALL_CMD) $(PKGS)
	@echo "Dependencies installed successfully."
else
	@echo "Error: Could not detect a supported package manager (apt, dnf, or pacman)."
	@echo "Please manually install: OpenCV, nlohmann-json, and Kernel Headers."
	@exit 1
endif

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
	@$(MAKE) deps
	@$(MAKE) install-module
	@$(MAKE) build-runnercpp

uninstall:
	@$(MAKE) remove-module
	@$(MAKE) clean

############################
# Cleaning                 #
############################
clean: clean-module clean-user clean-runnercpp
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
