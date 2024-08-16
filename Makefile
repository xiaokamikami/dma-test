DMA_UTILS?=/home/fpga-v/dma_ip_drivers/QDMA/linux-kernel/apps/dma-utils
DMA_INCLUDE?=/home/fpga-v/dma_ip_drivers/QDMA/linux-kernel/apps/include

CXXFLAGS = -Wextra -pedantic -O3 -fpermissive
CPPFLAGS = csrc/test_main.cpp csrc/pcie_driver.cpp csrc/pcie_mempool.cpp
CPPFLAGS += -I$(DMA_UTILS) -I$(DMA_INCLUDE) -I/home/fpga-v/project/qdma-test/include


LDFLAGS = -L
LIBS = -lqdma -lrt -laio -lpthread

PROGNAME = qdma_test

all: clean qdma_build

clean:
	rm -f $(PROGNAME)

qdma_build:
	g++ $(CXXFLAGS) $(CPPFLAGS) -o $(PROGNAME) $(LDFLAGS) $(LIBS)
