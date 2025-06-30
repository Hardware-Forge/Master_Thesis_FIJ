#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>

#define IOCTL_START_FAULT _IOW('f', 1, struct fij_params)
#define IOCTL_STOP_FAULT  _IO('f', 2)
#define IOCTL_STATUS_FAULT _IOR('f', 3, int)

struct fij_params {
    char process_name[256];
    int cycles;
};

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
    } else {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "%s start process=NAME [cycles=N]\n", argv[0]);
        fprintf(stderr, "  %s stop\n", argv[0]);
        fprintf(stderr, "  %s status\n", argv[0]);
        return 1;
    }

    close(fd);
    return 0;
}
