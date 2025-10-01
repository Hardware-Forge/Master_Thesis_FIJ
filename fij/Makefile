# Kernel module name and objects
obj-m += fij.o

# Objects that make up fij.o (order not important)
fij-objs := \
    fij_main.o \
    iface/chardev.o \
    iface/ioctl.o \
    core/bitflip.o \
    core/uprobe.o \
    core/monitor.o \
    core/exec_helper.o \
	core/fij_regs.o \
    core/util.o

# Include paths for in-tree and userspace headers
ccflags-y += -I$(src)/include -I$(src)/include/uapi/linux

# ---- Configurable knobs ----
KDIR    ?= /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)
MODNAME := fij.ko
MYAPP   := test_prog   # (kept for convenience, not used by the module itself)

.PHONY: all modules install-module remove-module clean run logs

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install-module: modules
	- sudo insmod $(MODNAME)
	@sleep 1
	@if [ -e /dev/fij ]; then \
	  sudo chmod 666 /dev/fij; \
	else \
	  echo "Note: /dev/fij not found yet. If udev/devtmpfs didn't create it automatically:"; \
	  echo "  minor=$$(grep -w fij /proc/misc | awk '{print $$1}');"; \
	  echo "  sudo mknod /dev/fij c 10 $$minor && sudo chmod 666 /dev/fij"; \
	fi

remove-module:
	- sudo rmmod fij || true

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Kept for workflow parity; now just loads the module.
# Interact with /dev/fij via your userspace tool (ioctl).
run: install-module
	@echo "Module loaded. Use your userspace command to talk to /dev/fij (no module params)."
	@echo "Example: ./fijctl exec <proc_name> <path> \"<args>\" <pc_offset_hex|0> <cycles>"

logs:
	sudo dmesg -w
