#ifndef __FPGA_PCIE_DRIVER_H__
#define __FPGA_PCIE_DRIVER_H__

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 500
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>



class FpgaPcieDevice {
public:
    FpgaPcieDevice(const char *fpga_name) {
        open_pcie_device(fpga_name);
    };
    ~FpgaPcieDevice() {
        if (fpga_fd > 0) {
            close(fpga_fd);
        }
    };

    int open_pcie_device(const char *devname);
    int read_pcie_buffer(char *buffer, uint64_t size, uint64_t addr);

private:
    size_t rc = 0;
    int fpga_fd = -1;
    char fpga_name[128]={};
};

#endif
