#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/fij.h>

#define IOCTL_START_FAULT _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT  _IO('f', 2)
#define IOCTL_STATUS_FAULT _IOR('f', 3, int)
#define IOCTL_EXEC_AND_FAULT _IOW('f', 4, struct fij_params)

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

static void set_process_name_from_path(struct fij_params *p) {
    if (!p || p->process_path[0] == '\0') return;
    const char *last_slash = strrchr(p->process_path, '/');
    const char *base = last_slash ? (last_slash + 1) : p->process_path;
    // Ensure NUL termination
    strncpy(p->process_name, base, sizeof(p->process_name) - 1);
    p->process_name[sizeof(p->process_name) - 1] = '\0';
}

static int parse_common_params(int argc, char **argv, int start_idx, struct fij_params *p) {
    if (!p) return -1;

    for (int i = start_idx; i < argc; ++i) {
        if (strncmp(argv[i], "path=", 5) == 0) {
            strncpy(p->process_path, argv[i] + 5, sizeof(p->process_path) - 1);
            p->process_path[sizeof(p->process_path) - 1] = '\0';
            set_process_name_from_path(p);
        } else if (strncmp(argv[i], "args=", 5) == 0) {
            strncpy(p->process_args, argv[i] + 5, sizeof(p->process_args) - 1);
            p->process_args[sizeof(p->process_args) - 1] = '\0';
        } else {
            // Not a common parameter; caller will handle (e.g., cycles=, pc=, reg=, bit=)
        }
    }

    // Validate required common fields if the caller expects them
    // (Exec requires path; Start also expects path in your current design)
    if (p->process_path[0] == '\0') {
        fprintf(stderr, "Missing path= argument\n");
        return -1;
    }
    return 0;
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

    // parse common fields (path, args)
    if (parse_common_params(argc, argv, 2, &params) < 0) {
        return 1;
    }

	// parse start-specific fields
    for (int i = 2; i < argc; ++i) {
        if (strncmp(argv[i], "cycles=", 7) == 0) {
            params.cycles = atoi(argv[i] + 7);
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

    // parse common fields (path, args)
    if (parse_common_params(argc, argv, 2, &params) < 0) return 1;

	for (int i = 2; i < argc; ++i) {
        if (strncmp(argv[i], "cycles=", 7) == 0) {
            params.cycles = atoi(argv[i] + 7);
        } else if (strncmp(argv[i], "pc=", 3) == 0) {
            params.target_pc = (unsigned long)strtoull(argv[i] + 3, NULL, 0);
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
        }
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
