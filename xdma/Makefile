# C++ 编译器
CXX = g++

FPGA?=0
DMA_CHANNS?=1

# C++ 编译标志
CXXFLAGS = -std=c++11 -Wall -Wextra -pedantic -march=native -O3 -DDMA_CHANNS=$(DMA_CHANNS)
# 包含目录
INCLUDES = -I../include

# C++ 源文件
SOURCES = xdma_test.cpp

# 需要链接的库
LIBS = -lrt -lpthread

ifeq ($(FPGA),1)
	CXXFLAGS += -DHAVE_FPGA
#LIBS += -laio
endif

# 程序名称
PROGNAME = xdma_test

# 默认目标
all: clean $(PROGNAME)

# 清理规则
clean:
	rm -f $(PROGNAME)

# 编译和链接规则
$(PROGNAME): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -g -o $@ $(LDFLAGS) $(LIBS)

.PHONY: all clean