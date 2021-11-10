#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "small/mempool.h"
#include "histogram.h"
struct txn;
struct tuple;

enum TXN_ALLOC_TYPE {
	TXN_ALLOC_STMT = 0,
	TXN_ALLOC_SVP = 1,
	TXN_ALLOC_USER_DATA = 2,
	TXN_ALLOC_MAX = 3
};

static const char *TXN_ALLOC_TYPE_STRS[] = {
	"STATEMENTS",
	"SAVEPOINTS",
	"USER DATA"
};

struct txn_stat_info {
	uint64_t max[TXN_ALLOC_MAX];
	uint64_t avg[TXN_ALLOC_MAX];
	uint64_t total[TXN_ALLOC_MAX];
};

struct txn_stat_storage {
	struct histogram *hist;
	uint64_t total;
};

struct tx_stat_manager {
	int alloc_max;
	uint64_t txn_num;
	struct txn_stat_storage *stats_storage;
};

struct txn_mem_used {
	uint64_t mempool_total;
	uint64_t total[];
};

/**
 * @brief Return stats collected by stat manager of memtx.
 * @param out stats min, max, avg and total for every statistic.
 */
void
tx_stat_get_stats(struct tx_stat_manager *stat, struct txn_stat_info *stats);

/**
 * @brief A wrapper over region_alloc.
 * @param txn Owner of a region
 * @param size Bytes to allocate
 * @param alloc_type See TX_ALLOC_TYPE
 * @note The only way to truncate a region of @a txn is to clear it.
 */
void *
tx_region_alloc(struct tx_stat_manager *stat, struct txn *txn, size_t size,
		int alloc_type);

/**
 * @brief Register @a txn in @a tx_stat. It is very important
 * to register txn before using allocators from @a tx_stat.
 */
void
tx_stat_register_txn(struct tx_stat_manager *stat, struct txn *txn);

/**
 * @brief Unregister @a txn and truncate its region up to sizeof(txn).
 */
void
tx_stat_clear_txn(struct tx_stat_manager *stat, struct txn *txn);

/**
 * @brief A wrapper over region_aligned_alloc.
 * @param txn Owner of a region
 * @param size Bytes to allocate
 * @param alignment Alignment of allocation
 * @param alloc_type See TX_ALLOC_TYPE
 */
void *
tx_region_aligned_alloc(struct tx_stat_manager *stat, struct txn *txn,
			size_t size, size_t alignment, int alloc_type);

#define tx_region_alloc_object(stat, txn, T, size, alloc_type) ({		            \
	*(size) = sizeof(T);							            \
	(T *)tx_region_aligned_alloc((stat), (txn), sizeof(T), alignof(T), (alloc_type));   \
})

/**
 * @brief A wrapper over mempool_alloc.
 * @param txn Txn that requires an allocation.
 * @param pool Mempool to allocate from.
 * @param alloc_type See TX_ALLOC_TYPE
 */
void *
tx_mempool_alloc(struct tx_stat_manager *stat, struct txn *txn,
		 struct mempool *pool, int alloc_type);

/**
 * @brief A wrapper over mempool_free.
 * @param txn Txn that requires a deallocation.
 * @param pool Mempool to deallocate from.
 * @param ptr Pointer to free.
 * @param alloc_type See TX_ALLOC_TYPE.
 */
void
tx_mempool_free(struct tx_stat_manager *stat, struct txn *txn,
		struct mempool *pool, void *ptr, int alloc_type);

void
tx_stat_init(struct tx_stat_manager *stat, int alloc_max,
	     struct txn_stat_storage *stat_storage);

void
tx_stat_free(struct tx_stat_manager *stat);

#ifdef __cplusplus
}
#endif
