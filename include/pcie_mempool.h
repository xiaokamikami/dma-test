#ifndef __FPGA_PCIE_MEMPOOL_H__
#define __FPGA_PCIE_MEMPOOL_H__

#include <stdlib.h>
#include <sys/mman.h>

#include "dmautils.h"
#include "qdma_nl.h"
#include "dmaxfer.h"

#define USE_MEMPOOL

// DMA memory pool
class FpgaPcieMemPool {
public:
    struct dma_meminfo {
        void *memptr;
        unsigned int num_blks;
    };

    struct mempool_handle {
        void *mempool;
        unsigned int mempool_blkidx;
        unsigned int mempool_blksz;
        unsigned int total_memblks;
        struct dma_meminfo *mempool_info;
    };

    void mempool_create(struct mempool_handle *mpool, unsigned int entry_size, unsigned int max_entries);
    void mempool_free(struct mempool_handle *mpool);
    void *dma_memalloc(struct mempool_handle *mpool, unsigned int num_blks);
    void dma_free(struct mempool_handle *mpool, void *memptr);

private:
    struct mempool_handle ctxhandle;
    struct mempool_handle iocbhandle;
    struct mempool_handle datahandle;

};

#endif
