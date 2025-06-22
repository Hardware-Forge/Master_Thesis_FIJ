obj-m += fij_mod.o
MYAPP := test_prog  #### ----------> INSERT PROCESS EXECUTABLE NAME
MODNAME := fij_mod.ko

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

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
