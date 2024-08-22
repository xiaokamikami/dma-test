# 定义 DMA 相关路径
DMA_UTILS ?= /home/fpga-v/dma_ip_drivers/QDMA/linux-kernel/apps/dma-utils
DMA_INCLUDE ?= /home/fpga-v/dma_ip_drivers/QDMA/linux-kernel/apps/include

# 获取 DMA 工具的 .o 文件
DMA_UTILS_OBJS := $(wildcard $(DMA_UTILS)/*.o)

# C++ 编译器
CXX = g++

# C++ 编译标志
CXXFLAGS = -Wextra -pedantic -O3 -fpermissive -fcommon

# C++ 源文件
SOURCES = csrc/test_main.cpp csrc/pcie_driver.cpp csrc/pcie_mempool.cpp

# 包含目录
INCLUDES = -I$(DMA_UTILS) -I$(DMA_INCLUDE) -I../include -I../../include

# 链接器标志
LDFLAGS = -L$(DMA_UTILS)

# 需要链接的库
LIBS =  -lqdma -lrt -laio -lpthread

# 程序名称
PROGNAME = qdma_test

# 默认目标
all: $(PROGNAME)

# 清理规则
clean:
	rm -f $(PROGNAME)

# 编译和链接规则
$(PROGNAME): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS) $(LIBS) $(DMA_UTILS_OBJS)

.PHONY: all clean