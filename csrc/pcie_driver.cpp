#include "pcie_driver.h"
#include "dma_xfer_utils.c"
int FpgaPcieDevice::open_pcie_device(const char *devname) {
    fpga_fd = open(devname, O_RDWR);
    memcpy(fpga_name, devname, sizeof(devname));
    if (fpga_fd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n", devname, fpga_fd);
		perror("open device");
		return -EINVAL;
	}
}

int FpgaPcieDevice::read_pcie_buffer(char *buffer, uint64_t size, uint64_t addr) {
    if (read_to_buffer(fpga_name, fpga_fd, buffer, size, addr) < 0) {
        assert(0);
    }
}
