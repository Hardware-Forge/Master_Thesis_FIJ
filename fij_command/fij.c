#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>

#define IOCTL_START_FAULT _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT  _IO('f', 2)
#define IOCTL_STATUS_FAULT _IOR('f', 3, int)
#define IOCTL_EXEC_AND_FAULT _IOW('f', 4, struct fij_params)

enum fij_reg_id {
    FIJ_REG_NONE = 0,
    FIJ_REG_RAX, FIJ_REG_RBX, FIJ_REG_RCX, FIJ_REG_RDX,
    FIJ_REG_RSI, FIJ_REG_RDI, FIJ_REG_RBP, FIJ_REG_RSP,
    FIJ_REG_RIP,        /* PC */
    FIJ_REG_MAX
};

struct fij_params {
    char process_name[256];
    char process_path[256];
    char process_args[256];
    int  cycles;
    unsigned long target_pc;     /* offset from start_code in INT */
    int  target_reg;             /* enum fij_reg_id */
    int  reg_bit;
};

/************** Helper to find target register ************** */
static int reg_name_to_id(const char *name) {
    if (!name) return FIJ_REG_NONE;
    if (!strcasecmp(name,"rax")) return FIJ_REG_RAX;
    if (!strcasecmp(name,"rbx")) return FIJ_REG_RBX;
    if (!strcasecmp(name,"rcx")) return FIJ_REG_RCX;
    if (!strcasecmp(name,"rdx")) return FIJ_REG_RDX;
    if (!strcasecmp(name,"rsi")) return FIJ_REG_RSI;
    if (!strcasecmp(name,"rdi")) return FIJ_REG_RDI;
    if (!strcasecmp(name,"rbp")) return FIJ_REG_RBP;
    if (!strcasecmp(name,"rsp")) return FIJ_REG_RSP;
    if (!strcasecmp(name,"pc") || !strcasecmp(name,"rip")) return FIJ_REG_RIP;
    return FIJ_REG_NONE;
}

/*****Helper to parse target_pc argument******/
unsigned long parse_pc_arg(const char *arg) {
    if (strncmp(arg, "pc=", 3) != 0)
        return 0;
    const char *val = arg + 3;
    char *end;
    unsigned long pc = strtoul(val, &end, 0); // auto-detect 0x prefix
    return pc;
}


int main(int argc, char *argv[]) {
    int fd = open("/dev/fij", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
	if (argc < 3 || argc > 4) {
    		fprintf(stderr, "Usage: %s start process=NAME [cycles=N]\n", argv[0]);
    		return 1;
	}

	struct fij_params params = {0};
	params.cycles = 0;  // default: infinite

	for (int i = 2; i < argc; ++i) {
	    if (strncmp(argv[i], "process=", 8) == 0) {
		    strncpy(params.process_name, argv[i] + 8, sizeof(params.process_name) - 1);
	    } else if (strncmp(argv[i], "cycles=", 7) == 0) {
		    params.cycles = atoi(argv[i] + 7);
	    } 
        else {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            return 1;
	    }
	}
        if (ioctl(fd, IOCTL_START_FAULT, &params) < 0) {
            perror("ioctl start");
            return 1;
        }

        printf("Started fault injection for '%s' (%d cycles)\n", params.process_name, params.cycles);
    } else if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        if (ioctl(fd, IOCTL_STOP_FAULT) < 0) {
            perror("ioctl stop");
            return 1;
        }
        printf("Fault injection stopped\n");
    } else if (argc == 2 && strcmp(argv[1], "status") == 0) {
        int status;
        if (ioctl(fd, IOCTL_STATUS_FAULT, &status) < 0) {
            perror("ioctl status");
            return 1;
        }
        printf("Status: %s\n", status ? "Running" : "Idle");
    } else if (argc >= 2 && strcmp(argv[1], "exec") == 0) {
	struct fij_params params = {0};
	params.cycles = 0;  // default to infinite
	params.target_pc = 0;

	for (int i = 2; i < argc; ++i) {
	    if (strncmp(argv[i], "path=", 5) == 0) {
		strncpy(params.process_path, argv[i] + 5, sizeof(params.process_path) - 1);
		// Auto-fill process_name
		char *last_slash = strrchr(params.process_path, '/');
		if (last_slash)
		    strncpy(params.process_name, last_slash + 1, sizeof(params.process_name) - 1);
		else
		    strncpy(params.process_name, params.process_path, sizeof(params.process_name) - 1);
	    } else if (strncmp(argv[i], "args=", 5) == 0) {
		strncpy(params.process_args, argv[i] + 5, sizeof(params.process_args) - 1);
	    } else if (strncmp(argv[i], "cycles=", 7) == 0) {
		params.cycles = atoi(argv[i] + 7);
	    } else if (strncmp(argv[i], "pc=", 3) == 0) {
		params.target_pc = parse_pc_arg(argv[i]);
	    } else if (strncmp(argv[i], "reg=", 4) == 0) {
            const char *nm = argv[i] + 4;
            params.target_reg = reg_name_to_id(nm);
            if (params.target_reg == FIJ_REG_NONE) {
                fprintf(stderr, "Invalid reg name: %s\n", nm);
                return 1;
            }
        } else if (strncmp(argv[i], "bit=", 4) == 0) {
            char *end = NULL;
            long b = strtol(argv[i] + 4, &end, 0);
            if (end == argv[i] + 4 || b < 0 || b > 63) {
                fprintf(stderr, "Invalid bit index (0..63): %s\n", argv[i] + 4);
                return 1;
            }
            params.reg_bit = (int)b;
        } else {
		fprintf(stderr, "Invalid argument: %s\n", argv[i]);
		return 1;
	    }
	}
        if (params.process_path[0] == '\0') {
            fprintf(stderr, "Missing path= argument\n");
            return 1;
        }

        if (ioctl(fd, IOCTL_EXEC_AND_FAULT, &params) < 0) {
            perror("ioctl exec");
            return 1;
        }

        printf("Executed '%s' with args '%s' and injected faults (%s cycles)\n",
               params.process_path,
               params.process_args[0] ? params.process_args : "(none)",
               params.cycles == 0 ? "infinite" : argv[3]);
    }
    else {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "%s start process=NAME [cycles=N]\n", argv[0]);
        fprintf(stderr, "  %s stop\n", argv[0]);
        fprintf(stderr, "  %s status\n", argv[0]);
        fprintf(stderr, "  %s exec path=PATH [args=\"ARG1 ARG2\"] [cycles=N]\n", argv[0]);	
        return 1;
    }

    close(fd);
    return 0;
}
