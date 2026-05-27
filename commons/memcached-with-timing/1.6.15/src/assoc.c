/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Hash table
 *
 * The hash function used here is by Bob Jenkins, 1996:
 *    <http://burtleburtle.net/bob/hash/doobs.html>
 *       "By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.
 *       You may use this code any way you wish, private, educational,
 *       or commercial.  It's free."
 *
 * The rest of the file is licensed under the BSD license.  See LICENSE.
 */

#include "memcached.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/resource.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

static pthread_cond_t maintenance_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t maintenance_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t assoc_find_timing_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t *assoc_find_times = NULL;
static size_t assoc_find_times_count = 0;
static size_t assoc_find_times_capacity = 0;
static bool assoc_find_timing_registered = false;

/* how many powers of 2's worth of buckets we use */
unsigned int hashpower = HASHPOWER_DEFAULT;

#define hashsize(n) ((uint64_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)

/* Main hash table. This is where we look except during expansion. */
static item** primary_hashtable = 0;

/*
 * Previous hash table. During expansion, we look here for keys that haven't
 * been moved over to the primary yet.
 */
static item** old_hashtable = 0;

/* Flag: Are we in the middle of expanding now? */
static bool expanding = false;

/*
 * During expansion we migrate values with bucket granularity; this is how
 * far we've gotten so far. Ranges from 0 .. hashsize(hashpower - 1) - 1.
 */
static uint64_t expand_bucket = 0;

static int assoc_find_time_cmp(const void *a, const void *b) {
    const uint64_t lhs = *(const uint64_t *)a;
    const uint64_t rhs = *(const uint64_t *)b;
    return (lhs > rhs) - (lhs < rhs);
}

static uint64_t assoc_find_percentile(const uint64_t *times, const size_t count,
                                      const size_t percentile) {
    size_t index = ((percentile * count) + 99) / 100;
    index = index == 0 ? 0 : index - 1;
    return times[index];
}

static void assoc_find_print_timing(void) {
    pthread_mutex_lock(&assoc_find_timing_lock);
    if (assoc_find_times_count == 0) {
        pthread_mutex_unlock(&assoc_find_timing_lock);
        return;
    }

    qsort(assoc_find_times, assoc_find_times_count, sizeof(uint64_t),
          assoc_find_time_cmp);
    fprintf(stderr,
            "assoc_find Time: count=%zu P25 %llu ns  P50 %llu ns  P99 %llu ns\n",
            assoc_find_times_count,
            (unsigned long long)assoc_find_percentile(assoc_find_times,
                                                      assoc_find_times_count, 25),
            (unsigned long long)assoc_find_percentile(assoc_find_times,
                                                      assoc_find_times_count, 50),
            (unsigned long long)assoc_find_percentile(assoc_find_times,
                                                      assoc_find_times_count, 99));
    free(assoc_find_times);
    assoc_find_times = NULL;
    assoc_find_times_count = 0;
    assoc_find_times_capacity = 0;
    pthread_mutex_unlock(&assoc_find_timing_lock);
}

static void assoc_find_record_time(const uint64_t elapsed_ns) {
    pthread_mutex_lock(&assoc_find_timing_lock);
    if (assoc_find_times_count == assoc_find_times_capacity) {
        size_t new_capacity = assoc_find_times_capacity == 0
                                  ? 1024
                                  : assoc_find_times_capacity * 2;
        uint64_t *new_times = realloc(assoc_find_times,
                                      new_capacity * sizeof(uint64_t));
        if (new_times == NULL) {
            pthread_mutex_unlock(&assoc_find_timing_lock);
            return;
        }
        assoc_find_times = new_times;
        assoc_find_times_capacity = new_capacity;
    }
    assoc_find_times[assoc_find_times_count++] = elapsed_ns;
    pthread_mutex_unlock(&assoc_find_timing_lock);
}

void assoc_init(const int hashtable_init) {
    if (!assoc_find_timing_registered) {
        atexit(assoc_find_print_timing);
        assoc_find_timing_registered = true;
    }
    if (hashtable_init) {
        hashpower = hashtable_init;
    }
    primary_hashtable = calloc(hashsize(hashpower), sizeof(void *));
    if (! primary_hashtable) {
        fprintf(stderr, "Failed to init hashtable.\n");
        exit(EXIT_FAILURE);
    }
    STATS_LOCK();
    stats_state.hash_power_level = hashpower;
    stats_state.hash_bytes = hashsize(hashpower) * sizeof(void *);
    STATS_UNLOCK();
}

item *assoc_find(const char *key, const size_t nkey, const uint32_t hv) {
    struct timespec assoc_find_start, assoc_find_end;
    item *it;
    uint64_t oldbucket;

    clock_gettime(CLOCK_MONOTONIC, &assoc_find_start);

    if (expanding &&
        (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
    {
        it = old_hashtable[oldbucket];
    } else {
        it = primary_hashtable[hv & hashmask(hashpower)];
    }

    item *ret = NULL;
    int depth = 0;
    while (it) {
        if ((nkey == it->nkey) && (memcmp(key, ITEM_key(it), nkey) == 0)) {
            ret = it;
            break;
        }
        it = it->h_next;
        ++depth;
    }
    clock_gettime(CLOCK_MONOTONIC, &assoc_find_end);
    MEMCACHED_ASSOC_FIND(key, nkey, depth);
    if (settings.verbose > 1) {
        uint64_t assoc_find_ns = (uint64_t)(assoc_find_end.tv_sec - assoc_find_start.tv_sec) * 1000000000ULL
            + (uint64_t)(assoc_find_end.tv_nsec - assoc_find_start.tv_nsec);
        assoc_find_record_time(assoc_find_ns);
    }
    return ret;
}

/* returns the address of the item pointer before the key.  if *item == 0,
   the item wasn't found */

static item** _hashitem_before (const char *key, const size_t nkey, const uint32_t hv) {
    item **pos;
    uint64_t oldbucket;

    if (expanding &&
        (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
    {
        pos = &old_hashtable[oldbucket];
    } else {
        pos = &primary_hashtable[hv & hashmask(hashpower)];
    }

    while (*pos && ((nkey != (*pos)->nkey) || memcmp(key, ITEM_key(*pos), nkey))) {
        pos = &(*pos)->h_next;
    }
    return pos;
}

/* grows the hashtable to the next power of 2. */
static void assoc_expand(void) {
    old_hashtable = primary_hashtable;

    primary_hashtable = calloc(hashsize(hashpower + 1), sizeof(void *));
    if (primary_hashtable) {
        if (settings.verbose > 1)
            fprintf(stderr, "Hash table expansion starting\n");
        hashpower++;
        expanding = true;
        expand_bucket = 0;
        STATS_LOCK();
        stats_state.hash_power_level = hashpower;
        stats_state.hash_bytes += hashsize(hashpower) * sizeof(void *);
        stats_state.hash_is_expanding = true;
        STATS_UNLOCK();
    } else {
        primary_hashtable = old_hashtable;
        /* Bad news, but we can keep running. */
    }
}

void assoc_start_expand(uint64_t curr_items) {
    if (pthread_mutex_trylock(&maintenance_lock) == 0) {
        if (curr_items > (hashsize(hashpower) * 3) / 2 && hashpower < HASHPOWER_MAX) {
            pthread_cond_signal(&maintenance_cond);
        }
        pthread_mutex_unlock(&maintenance_lock);
    }
}

/* Note: this isn't an assoc_update.  The key must not already exist to call this */
int assoc_insert(item *it, const uint32_t hv) {
    uint64_t oldbucket;

//    assert(assoc_find(ITEM_key(it), it->nkey) == 0);  /* shouldn't have duplicately named things defined */

    if (expanding &&
        (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
    {
        it->h_next = old_hashtable[oldbucket];
        old_hashtable[oldbucket] = it;
    } else {
        it->h_next = primary_hashtable[hv & hashmask(hashpower)];
        primary_hashtable[hv & hashmask(hashpower)] = it;
    }

    MEMCACHED_ASSOC_INSERT(ITEM_key(it), it->nkey);
    return 1;
}

void assoc_delete(const char *key, const size_t nkey, const uint32_t hv) {
    item **before = _hashitem_before(key, nkey, hv);

    if (*before) {
        item *nxt;
        /* The DTrace probe cannot be triggered as the last instruction
         * due to possible tail-optimization by the compiler
         */
        MEMCACHED_ASSOC_DELETE(key, nkey);
        nxt = (*before)->h_next;
        (*before)->h_next = 0;   /* probably pointless, but whatever. */
        *before = nxt;
        return;
    }
    /* Note:  we never actually get here.  the callers don't delete things
       they can't find. */
    assert(*before != 0);
}


static volatile int do_run_maintenance_thread = 1;

#define DEFAULT_HASH_BULK_MOVE 1
int hash_bulk_move = DEFAULT_HASH_BULK_MOVE;

static void *assoc_maintenance_thread(void *arg) {

    mutex_lock(&maintenance_lock);
    while (do_run_maintenance_thread) {
        int ii = 0;

        /* There is only one expansion thread, so no need to global lock. */
        for (ii = 0; ii < hash_bulk_move && expanding; ++ii) {
            item *it, *next;
            uint64_t bucket;
            void *item_lock = NULL;

            /* bucket = hv & hashmask(hashpower) =>the bucket of hash table
             * is the lowest N bits of the hv, and the bucket of item_locks is
             *  also the lowest M bits of hv, and N is greater than M.
             *  So we can process expanding with only one item_lock. cool! */
            if ((item_lock = item_trylock(expand_bucket))) {
                    for (it = old_hashtable[expand_bucket]; NULL != it; it = next) {
                        next = it->h_next;
                        bucket = hash(ITEM_key(it), it->nkey) & hashmask(hashpower);
                        it->h_next = primary_hashtable[bucket];
                        primary_hashtable[bucket] = it;
                    }

                    old_hashtable[expand_bucket] = NULL;

                    expand_bucket++;
                    if (expand_bucket == hashsize(hashpower - 1)) {
                        expanding = false;
                        free(old_hashtable);
                        STATS_LOCK();
                        stats_state.hash_bytes -= hashsize(hashpower - 1) * sizeof(void *);
                        stats_state.hash_is_expanding = false;
                        STATS_UNLOCK();
                        if (settings.verbose > 1)
                            fprintf(stderr, "Hash table expansion done\n");
                    }

            } else {
                usleep(10*1000);
            }

            if (item_lock) {
                item_trylock_unlock(item_lock);
                item_lock = NULL;
            }
        }

        if (!expanding) {
            /* We are done expanding.. just wait for next invocation */
            pthread_cond_wait(&maintenance_cond, &maintenance_lock);
            /* assoc_expand() swaps out the hash table entirely, so we need
             * all threads to not hold any references related to the hash
             * table while this happens.
             * This is instead of a more complex, possibly slower algorithm to
             * allow dynamic hash table expansion without causing significant
             * wait times.
             */
            if (do_run_maintenance_thread) {
                pause_threads(PAUSE_ALL_THREADS);
                assoc_expand();
                pause_threads(RESUME_ALL_THREADS);
            }
        }
    }
    mutex_unlock(&maintenance_lock);
    return NULL;
}

static pthread_t maintenance_tid;

int start_assoc_maintenance_thread() {
    int ret;
    char *env = getenv("MEMCACHED_HASH_BULK_MOVE");
    if (env != NULL) {
        hash_bulk_move = atoi(env);
        if (hash_bulk_move == 0) {
            hash_bulk_move = DEFAULT_HASH_BULK_MOVE;
        }
    }

    if ((ret = pthread_create(&maintenance_tid, NULL,
                              assoc_maintenance_thread, NULL)) != 0) {
        fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
        return -1;
    }
    return 0;
}

void stop_assoc_maintenance_thread() {
    mutex_lock(&maintenance_lock);
    do_run_maintenance_thread = 0;
    pthread_cond_signal(&maintenance_cond);
    mutex_unlock(&maintenance_lock);

    /* Wait for the maintenance thread to stop */
    pthread_join(maintenance_tid, NULL);
}

struct assoc_iterator {
    uint64_t bucket;
    item *it;
    item *next;
    bool bucket_locked;
};

void *assoc_get_iterator(void) {
    struct assoc_iterator *iter = calloc(1, sizeof(struct assoc_iterator));
    if (iter == NULL) {
        return NULL;
    }
    // this will hang the caller while a hash table expansion is running.
    mutex_lock(&maintenance_lock);
    return iter;
}

bool assoc_iterate(void *iterp, item **it) {
    struct assoc_iterator *iter = (struct assoc_iterator *) iterp;
    *it = NULL;
    // - if locked bucket and next, update next and return
    if (iter->bucket_locked) {
        if (iter->next != NULL) {
            iter->it = iter->next;
            iter->next = iter->it->h_next;
            *it = iter->it;
        } else {
            // unlock previous bucket, if any
            item_unlock(iter->bucket);
            // iterate the bucket post since it starts at 0.
            iter->bucket++;
            iter->bucket_locked = false;
            *it = NULL;
        }
        return true;
    }

    // - loop until we hit the end or find something.
    if (iter->bucket != hashsize(hashpower)) {
        // - lock next bucket
        item_lock(iter->bucket);
        iter->bucket_locked = true;
        // - only check the primary hash table since expand is blocked.
        iter->it = primary_hashtable[iter->bucket];
        if (iter->it != NULL) {
            // - set it, next and return
            iter->next = iter->it->h_next;
            *it = iter->it;
        } else {
            // - nothing found in this bucket, try next.
            item_unlock(iter->bucket);
            iter->bucket_locked = false;
            iter->bucket++;
        }
    } else {
        return false;
    }

    return true;
}

void assoc_iterate_final(void *iterp) {
    struct assoc_iterator *iter = (struct assoc_iterator *) iterp;
    if (iter->bucket_locked) {
        item_unlock(iter->bucket);
    }
    mutex_unlock(&maintenance_lock);
    free(iter);
}
