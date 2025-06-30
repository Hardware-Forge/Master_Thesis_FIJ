obj-m += fij_mod.o
MYAPP := test_prog  #### ----------> INSERT PROCESS EXECUTABLE NAME
MODNAME := fij_mod.ko

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install-module: all
	sudo insmod $(MODNAME)
	sleep 1
	sudo chmod 666 /dev/fij || echo "Note: Set /dev/fij permissions manually if needed"

remove-module:
	sudo rmmod fij_mod || true

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

run: all
	@pid=$$(pidof $(MYAPP)); \
	if [ -z "$$pid" ]; then \
		echo "Error: Failed to find PID for $(MYAPP)"; \
		exit 1; \
	else \
		echo "Inserting module $(MODNAME) with pid=$$pid"; \
		sudo insmod $(MODNAME) pid=$$pid; \
	fi
logs:	
	sudo dmesg
