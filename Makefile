DMA_UTILS?=/home/fpga-v/dma_ip_drivers/QDMA/linux-kernel/apps/dma-utils
DMA_INCLUDE?=/home/fpga-v/dma_ip_drivers/QDMA/linux-kernel/apps/include

CXXFLAGS = -Wextra -pedantic -O3
CPPFLAGS = csrc/test_main.cpp csrc/pcie_driver.cpp
CPPFLAGS += -I$(DMA_UTILS) -I$(DMA_INCLUDE) -I/home/fpga-v/project/qdma-test/include


LDFLAGS = -L
LIBS = -lqdma -lrt

OUTFILE = qdma

all: clean qdma_

clean:
	rm -f $(OUTFILE)

qdma_:
	g++ $(CXXFLAGS) $(CPPFLAGS) -o $(OUTFILE) $(LDFLAGS) $(LIBS)
