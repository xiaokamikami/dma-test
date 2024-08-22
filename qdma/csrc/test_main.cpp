#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <sstream>

#include "pcie_driver.h"

#define DMA_NAME_DEFAULT "qdma03000"
#define DEVICE_NAME_DEFAULT "/dev/qdma03000-MM-0"
#define SIZE_DEFAULT (32)
#define COUNT_DEFAULT (1)

#define DEFAULT_PAGE_SIZE 4096
#define MAX_AIO_EVENTS 65536

char *buffer = NULL;
char *allocated = NULL;

bool queue_exit(const std::string& pci_addr, const std::string& direction);
void create_qdma_queue(const std::string& pci_addr, int queue_idx, const std::string& mode, const std::string& direction);
void start_qdma_queue(const std::string& pci_addr, int queue_idx, const std::string& direction);

int main(int argc, char *argv[]) {
    // init
    ssize_t rc = 0;

    //test mm
    FpgaPcieDevice *fpga_driver = new FpgaPcieDevice("/dev/qdma03000-MM-0");
    posix_memalign((void **)&allocated, DEFAULT_PAGE_SIZE /*alignment */ , DEFAULT_PAGE_SIZE);
	if (!allocated) {
		fprintf(stderr, "OOM %lu.\n", DEFAULT_PAGE_SIZE);
		rc = -ENOMEM;
	}

    struct timespec ts_start, ts_end;
    double total_time = 0;
    int count = 0;
    for (count = 0; count < 10; count++) {
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        rc = fpga_driver->read_pcie_buffer(buffer, DEFAULT_PAGE_SIZE, 0);
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

    free(allocated);
    delete fpga_driver;

    // test aio
    // FpgaPcieDeviceAsyncIO *fpga_driver_aio = new FpgaPcieDeviceAsyncIO("/dev/qdma01000-MM-0");

    return 0;
}


std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// dma-ctl cmd
bool queue_exists(const std::string& pci_addr, int queue_idx, const std::string& direction) {
    std::string cmd = "dma-ctl qdma" + pci_addr + " q list";
    std::string result = exec(cmd.c_str());
    std::stringstream ss(result);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("qdma" + pci_addr + "-MM-" + std::to_string(queue_idx) + " " + direction) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void delete_qdma_queue(const std::string& pci_addr, int queue_idx, const std::string& direction) {
    std::string cmd = "dma-ctl qdma" + pci_addr + " q del idx " + std::to_string(queue_idx) + 
                      " dir " + direction;
    
    std::cout << "Executing command: " << cmd << std::endl;
    
    try {
        std::string result = exec(cmd.c_str());
        std::cout << "Command output: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error executing command: " << e.what() << std::endl;
    }
}

void create_qdma_queue(const std::string& pci_addr, int queue_idx, const std::string& mode, const std::string& direction) {
    if (queue_exists(pci_addr, queue_idx, direction)) {
        std::cout << "Queue already exists. Deleting old queue..." << std::endl;
        delete_qdma_queue(pci_addr, queue_idx, direction);
    }

    std::string cmd = "dma-ctl qdma" + pci_addr + " q add idx " + std::to_string(queue_idx) + 
                      " mode " + mode + " dir " + direction;
    
    std::cout << "Executing command: " << cmd << std::endl;
    
    try {
        std::string result = exec(cmd.c_str());
        std::cout << "Command output: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error executing command: " << e.what() << std::endl;
    }
}

void start_qdma_queue(const std::string& pci_addr, int queue_idx, const std::string& direction) {
    std::string cmd = "dma-ctl qdma" + pci_addr + " q start idx " + std::to_string(queue_idx) + 
                      " dir " + direction;
    
    std::cout << "Executing command: " << cmd << std::endl;
    
    try {
        std::string result = exec(cmd.c_str());
        std::cout << "Command output: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error executing command: " << e.what() << std::endl;
    }
}
