#include <stdio.h>
#include "pcie_mempool.h"

void FpgaPcieMemPool::mempool_create(struct mempool_handle *mpool, unsigned int entry_size,
		unsigned int max_entries)
{
#ifdef USE_MEMPOOL
	if (posix_memalign((void **)&mpool->mempool, DMAPERF_PAGE_SIZE,
			   max_entries * (entry_size + sizeof(struct dma_meminfo)))) {
		printf("OOM Mempool\n");
		return -ENOMEM;
	}
	mpool->mempool_info = (struct dma_meminfo *)(((char *)mpool->mempool) + (max_entries * entry_size));
#endif
	mpool->mempool_blksz = entry_size;
	mpool->total_memblks = max_entries;
	mpool->mempool_blkidx = 0;

	return 0;
}

void FpgaPcieMemPool::mempool_free(struct mempool_handle *mpool)
{
#ifdef USE_MEMPOOL
	free(mpool->mempool);
	mpool->mempool = NULL;
#endif
}

void* FpgaPcieMemPool::dma_memalloc(struct mempool_handle *mpool, unsigned int num_blks)
{
	unsigned int _mempool_blkidx = mpool->mempool_blkidx;
	unsigned int tmp_blkidx = _mempool_blkidx;
	unsigned int max_blkcnt = tmp_blkidx + num_blks;
	unsigned int i, avail = 0;
	void *memptr = NULL;
	char *mempool = mpool->mempool;
	struct dma_meminfo *_mempool_info = mpool->mempool_info;
	unsigned int _total_memblks = mpool->total_memblks;

#ifdef USE_MEMPOOL
	if (max_blkcnt > _total_memblks) {
		tmp_blkidx = 0;
		max_blkcnt = num_blks;
	}
	for (i = tmp_blkidx; (i < _total_memblks) && (i < max_blkcnt); i++) {
		if (_mempool_info[i].memptr) { /* occupied blks ahead */
			i += _mempool_info[i].num_blks;
			max_blkcnt = i + num_blks;
			avail = 0;
			tmp_blkidx = i;
		} else
			avail++;
		if (max_blkcnt > _total_memblks) { /* reached the end of mempool. circle through*/
			if (num_blks > _mempool_blkidx) return NULL; /* Continuous num_blks not available */
			i = 0;
			avail = 0;
			max_blkcnt = num_blks;
			tmp_blkidx = 0;
		}
	}
	if (avail < num_blks) { /* no required available blocks */
		return NULL;
	}

	memptr = &(mempool[tmp_blkidx * mpool->mempool_blksz]);
	_mempool_info[tmp_blkidx].memptr = memptr;
	_mempool_info[tmp_blkidx].num_blks = num_blks;
	mpool->mempool_blkidx = tmp_blkidx + num_blks;
#else
	memptr = calloc(num_blks, mpool->mempool_blksz);
#endif

	return memptr;
}

void FpgaPcieMemPool::dma_free(struct mempool_handle *mpool, void *memptr)
{
#ifdef USE_MEMPOOL
	struct dma_meminfo *_meminfo = mpool->mempool_info;
	unsigned int idx;

	if (!memptr) return;

	idx = idx = (static_cast<char*>(memptr) - static_cast<char*>(mpool->mempool)) / mpool->mempool_blksz;
#ifdef DEBUG
	if (idx >= mpool->total_memblks) {
		printf("Asserting: %u:Invalid memory index %u acquired\n", mpool->id, idx);
		while(1);
	}
#endif

	_meminfo[idx].num_blks = 0;
	_meminfo[idx].memptr = NULL;
#else
	free(memptr);
#endif
}