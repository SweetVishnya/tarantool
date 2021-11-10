#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "tx_stat.h"
struct memtx_story;

enum MEMTX_TX_ALLOC_TYPE {
	MEMTX_TX_ALLOC_MIN = 2,
	MEMTX_TX_ALLOC_TRACKER = 3,
	MEMTX_TX_ALLOC_LOG = 4,
	MEMTX_TX_ALLOC_TRIGGER = 5,
	MEMTX_TX_ALLOC_MAX = 6
};

static_assert(MEMTX_TX_ALLOC_MIN == TXN_ALLOC_MAX - 1,
	      "enum MEMTX_TX_ALLOC_TYPE is not consistent with TXN_ALLOC_TYPE");

enum MEMTX_TX_PIN_TYPE {
	MEMTX_TX_PIN_USED = 0,
	MEMTX_TX_PIN_RV = 1,
	MEMTX_TX_PIN_TRACK_GAP = 2,
	MEMTX_TX_PIN_NOT_USED = 3,
	MEMTX_TX_PIN_MAX = 4
};

static const char *MEMTX_TX_PIN_TYPE_STRS[] = {
	"USED BY ACTIVE TXNS",
	"POTENTIALLY IN READ VIEW",
	"USED TO TRACK GAP",
	"GARBAGE"
};

static const char *MEMTX_TX_ALLOC_TYPE_STRS[] = {
	"",
	"",
	"",
	"TRACKERS",
	"LOGS",
	"TRIGGERS"
};

struct memtx_tx_stats {
	uint64_t total[MEMTX_TX_ALLOC_MAX];
	uint64_t avg[MEMTX_TX_ALLOC_MAX];
	uint64_t max[MEMTX_TX_ALLOC_MAX];
	uint64_t min[MEMTX_TX_ALLOC_MAX];

	uint64_t stories_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_count[MEMTX_TX_PIN_MAX];
};

struct memtx_tx_stat_manager {
	struct tx_stat_manager txn_stats;
	uint64_t stories_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_count[MEMTX_TX_PIN_MAX];
	struct txn_stat_storage stats_storage[MEMTX_TX_ALLOC_MAX];
};

static inline void *
memtx_tx_stat_story_alloc(struct memtx_tx_stat_manager *stat, struct mempool *pool)
{
	assert(pool != NULL);
	struct mempool_stats pool_stats;
	mempool_stats(pool, &pool_stats);
	stat->stories_total[MEMTX_TX_PIN_USED] += pool_stats.objsize;
	return mempool_alloc(pool);
}

static inline void
memtx_tx_stat_story_free(struct memtx_tx_stat_manager *stat,
			 struct mempool *pool, struct memtx_story *story,
			 enum MEMTX_TX_PIN_TYPE story_status)
{
	assert(pool != NULL);
	say_info("free story");
	struct mempool_stats pool_stats;
	mempool_stats(pool, &pool_stats);
	stat->stories_total[story_status] -= pool_stats.objsize;
	mempool_free(pool, story);
}

static inline void
memtx_tx_stat_pin_tuple(struct memtx_tx_stat_manager *stat,
			enum MEMTX_TX_PIN_TYPE status, size_t tuple_size)
{
	assert(status >= 0 && status < MEMTX_TX_PIN_MAX);

	stat->pinned_tuples_total[status] += tuple_size;
	stat->pinned_tuples_count[status]++;
}

static inline void
memtx_tx_stat_release_tuple(struct memtx_tx_stat_manager *stat,
			    enum MEMTX_TX_PIN_TYPE status, size_t tuple_size)
{
	assert(status >= 0 && status < MEMTX_TX_PIN_MAX);
	assert(stat->pinned_tuples_count[status] > 0);

	stat->pinned_tuples_total[status] -= tuple_size;
	stat->pinned_tuples_count[status]--;
}

static inline void
memtx_tx_stat_refresh_story_status(struct memtx_tx_stat_manager *stat,
				   enum MEMTX_TX_PIN_TYPE old_status,
				   enum MEMTX_TX_PIN_TYPE new_status,
				   size_t size)
{
	stat->stories_total[old_status] -= size;
	stat->stories_total[new_status] += size;
}

static inline void
memtx_tx_stat_refresh_pinned_tuple_status(struct memtx_tx_stat_manager *stat,
					  enum MEMTX_TX_PIN_TYPE old_status,
					  enum MEMTX_TX_PIN_TYPE new_status,
					  size_t size)
{
	stat->pinned_tuples_total[old_status] -= size;
	stat->pinned_tuples_count[old_status]--;
	stat->pinned_tuples_total[new_status] += size;
	stat->pinned_tuples_count[new_status]++;
}

static inline void
memtx_tx_stat_get_stats(struct memtx_tx_stat_manager *stat_manager,
			struct memtx_tx_stats *stats_out)
{
	assert(stat_manager != NULL);
	assert(stats_out != NULL);

	for (size_t i = 0; i < MEMTX_TX_ALLOC_MAX; ++i) {
		stats_out->total[i] = stat_manager->stats_storage[i].total;
		if (stat_manager->txn_stats.txn_num > 0) {
			stats_out->avg[i] = stat_manager->stats_storage[i].total /
					stat_manager->txn_stats.txn_num;
		} else {
			stats_out->avg[i] = 0;
		}
		stats_out->max[i] = histogram_max(
			stat_manager->stats_storage[i].hist);
	}
	for (size_t i = 0; i < MEMTX_TX_PIN_MAX; ++i) {
		stats_out->pinned_tuples_total[i] =
			stat_manager->pinned_tuples_total[i];
		stats_out->pinned_tuples_count[i] =
			stat_manager->pinned_tuples_count[i];
		stats_out->stories_total[i] = stat_manager->stories_total[i];
	}
}

static inline void
memtx_tx_stat_init(struct memtx_tx_stat_manager *stat)
{
	tx_stat_init(&stat->txn_stats, MEMTX_TX_ALLOC_MAX,
		     stat->stats_storage);
	memset(&stat->pinned_tuples_total, 0, MEMTX_TX_PIN_MAX);
	memset(&stat->pinned_tuples_count, 0, MEMTX_TX_PIN_MAX);
	memset(&stat->stories_total, 0, MEMTX_TX_PIN_MAX);
}

static inline void
memtx_tx_stat_free(struct memtx_tx_stat_manager *stat)
{
	tx_stat_free(&stat->txn_stats);
}

#ifdef __cplusplus
}
#endif
