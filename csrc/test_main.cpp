#include "pcie_driver.h"

#define DEVICE_NAME_DEFAULT "/dev/qdma01000-MM-0"
#define SIZE_DEFAULT (32)
#define COUNT_DEFAULT (1)
FpgaPcieDevice *fpga_driver = NULL;
char *buffer = NULL;
char *allocated = NULL;

void exit_my();

int main(int argc, char *argv[]) {
    ssize_t rc = 0;
    // init
    fpga_driver = new FpgaPcieDevice("/dev/qdma01000-MM-0");
    posix_memalign((void **)&allocated, 4096 /*alignment */ , 4096);
	if (!allocated) {
		fprintf(stderr, "OOM %lu.\n", 4096);
		rc = -ENOMEM;
		exit_my();
	}

    //test
    struct timespec ts_start, ts_end;
    double total_time = 0;
    int count = 0;
    for (count = 0; count < 10; count++) {
		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		rc = fpga_driver->read_pcie_buffer(buffer, 4096, 0);
		if (rc < 0)
			assert(0);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
        ts_end.tv_sec = ts_end.tv_sec - ts_start.tv_sec;
        ts_end.tv_nsec = ts_end.tv_nsec - ts_start.tv_nsec;
		total_time += (ts_end.tv_sec + ((double)ts_end.tv_nsec/10000));
    }
    double avg_time = (double)total_time/(double)count;
    double result = ((double)4096)/avg_time;

    printf("** Avg time device %s, total time %f nsec, avg_time = %f, BW = %f bytes/sec\n",
		DEVICE_NAME_DEFAULT, total_time, avg_time, result);
    return 0;
}

void exit_my() {
    free(allocated);
    delete fpga_driver;
}