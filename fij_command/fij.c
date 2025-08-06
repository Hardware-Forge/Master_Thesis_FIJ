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

struct fij_params {
    char process_name[256];
    char process_path[256];     // program path
    char process_args[256];     // argument string
    int cycles;                 // fault cycles
    unsigned long target_pc;    // new: target instruction pointer (0 = immediate)
};

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
	    } else {
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
