#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "cache.h"
#include "print_helpers.h"

cache_t *make_cache(int capacity, int block_size, int assoc, enum protocol_t protocol, bool lru_on_invalidate_f){
  cache_t *cache = malloc(sizeof(cache_t));
  cache->stats = make_cache_stats();
  
  cache->capacity = capacity;      // in Bytes
  cache->block_size = block_size;  // in Bytes
  cache->assoc = assoc;            // 1, 2, 3... etc.

  cache->n_cache_line = capacity/block_size;
  cache->n_set = capacity/(assoc*block_size);
  cache->n_offset_bit = log2(block_size);
  cache->n_index_bit = log2(cache->n_set);
  cache->n_tag_bit = ADDRESS_SIZE - (cache->n_offset_bit) - (cache->n_index_bit);

  cache->lines = malloc(sizeof(cache_line_t)* cache->n_set);
  for(int i=0; i<cache->n_set;i++){
    cache->lines[i] = malloc(sizeof(cache_line_t) * assoc);
  }
  
  cache->lru_way = malloc((cache->n_set) * sizeof(int));

  for (int i = 0; i < cache->n_set; i++) {
    for (int j = 0; j < assoc; j++) {
      (cache->lines[i][j]).tag = 0;
      (cache->lines[i][j]).dirty_f = 0;
      (cache->lines[i][j]).state = INVALID;
    }
  }
  for(int i = 0; i < cache->n_set; i++){
    cache->lru_way[i]=0;
  }

  cache->protocol = protocol;
  cache->lru_on_invalidate_f = lru_on_invalidate_f;
  
  return cache;
}

/* Given a configured cache, returns the tag portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_tag(0b111101010001) returns 0b1111
 * in decimal -- get_cache_tag(3921) returns 15 
 */
unsigned long get_cache_tag(cache_t *cache, unsigned long addr) {
  int mask = ~0;
  mask = mask >> (ADDRESS_SIZE - cache->n_tag_bit);
  addr = addr >> (ADDRESS_SIZE - cache->n_tag_bit);
  return addr & mask;
}

/* Given a configured cache, returns the index portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_index(0b111101010001) returns 0b0101
 * in decimal -- get_cache_index(3921) returns 5
 */
unsigned long get_cache_index(cache_t *cache, unsigned long addr) {
  uint mask = 0xffffffff;
  addr = (addr >> cache->n_offset_bit);
  mask = mask >> (ADDRESS_SIZE - cache->n_index_bit);
  return mask & addr;
  
}

/* Given a configured cache, returns the given address with the offset bits zeroed out.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_block_addr(0b111101010001) returns 0b111101010000
 * in decimal -- get_cache_block_addr(3921) returns 3920
 */
unsigned long get_cache_block_addr(cache_t *cache, unsigned long addr) {
  addr = addr >> cache-> n_offset_bit;
  addr = addr << cache-> n_offset_bit;
  return addr;
}


/* this method takes a cache, an address, and an action
 * it proceses the cache access. functionality in no particular order: 
 *   - look up the address in the cache, determine if hit or miss
 *   - update the LRU_way, cacheTags, state, dirty flags if necessary
 *   - update the cache statistics (call update_stats)
 * return true if there was a hit, false if there was a miss
 * Use the "get" helper functions above. They make your life easier.
 */
bool access_cache(cache_t *cache, unsigned long addr, enum action_t action) {
  int index = get_cache_index(cache, addr);
  int tag = get_cache_tag(cache, addr);
  bool hit = false;
  bool wb = false;
  int * lru = cache->lru_way;
  cache_line_t *set = cache->lines[index];

  if(cache->protocol == MSI){
    bool upgrade_miss = false;
    switch(action){
      case LOAD:
        for(int i = 0; i < cache->assoc; i++){
          if(set[i].tag == tag && set[i].state != INVALID){
            update_stats(cache->stats, true, false, false, LOAD);
            log_way(i);
            log_set(index);
            lru[index] = (i+1) % cache->assoc;
            return true;
          }
        }

        if (set[lru[index]].dirty_f == true && set[lru[index]].state == MODIFIED) wb = true;
        set[lru[index]].tag = tag;
        set[lru[index]].dirty_f = false;
        set[lru[index]].state = SHARED;
        log_way(lru[index]);
        log_set(index);
        update_stats(cache->stats, false, wb, false, LOAD);
        lru[index] = (lru[index] +1) % cache->assoc;
        return false;

      case STORE:
        for(int i = 0; i < cache->assoc; i++){
          if(set[i].tag == tag && set[i].state != INVALID){
            set[i].dirty_f = true;
            lru[index] = (lru[index] +1) % cache->assoc;
            log_way(i);
            log_set(index);
            update_stats(cache->stats, true, false, false, STORE);
            set[lru[index]].state = MODIFIED;
            return true;
          }
        }

        if (set[lru[index]].dirty_f == true && set[lru[index]].state == MODIFIED) wb = true;
        update_stats(cache->stats, false, wb, false, STORE);
        log_way(lru[index]);
        log_set(index);
        set[lru[index]].tag = tag;
        set[lru[index]].dirty_f = true;
        set[lru[index]].state = MODIFIED;
        lru[index] = (lru[index] +1) % cache->assoc;
        return false;

      case LD_MISS:
        for(int i = 0; i < cache->assoc; i++){
          if(set[i].tag == tag && set[i].state != INVALID){
            hit = true;
            if(set[i].state == MODIFIED) wb = true;
            set[i].state = SHARED;
          }
        }
        update_stats(cache->stats,hit,wb,false,LD_MISS);
        return hit;
      
      case ST_MISS:
        for(int i = 0; i < cache->assoc; i++){
            if(set[i].tag == tag && set[i].state != INVALID){
              hit = true;
              if (set[i].state == MODIFIED) wb = true;
              if (set[i].state == SHARED) upgrade_miss = true;
              set[i].state = INVALID;
            }
        }
        update_stats(cache->stats, hit, wb, upgrade_miss, ST_MISS);
        return hit;
    }
    return true;
  }

  switch(action){
    case LOAD:
      for(int i = 0; i < cache->assoc; i++){
        if(set[i].tag == tag && set[i].state == VALID){
          log_way(i);
          log_set(index);
          update_stats(cache->stats, true, false, false, LOAD);
          lru[index] = (i +1) % cache->assoc;
          return true;
        }
      }
      log_way(lru[index]);
      log_set(index);
      if (set[lru[index]].dirty_f == true && set[lru[index]].state==VALID) wb = true;
      set[lru[index]].tag = tag;
      set[lru[index]].dirty_f = false;
      set[lru[index]].state = VALID;
      update_stats(cache->stats, false, wb, false, LOAD);
      lru[index] = (lru[index] +1) % cache->assoc;
      return false;

    case STORE:
      for(int i = 0; i < cache->assoc; i++){
        if(set[i].tag == tag && set[i].state == VALID){
          log_way(i);
          log_set(index);
          set[i].dirty_f = true;
          cache->lru_way[index] = (i +1) % cache->assoc;
          update_stats(cache->stats, true, false, false, STORE);
          return true;
        }
      }
      if (set[lru[index]].dirty_f == true && set[lru[index]].state==VALID) wb = true;
      log_way(lru[index]);
      log_set(index);
      update_stats(cache->stats, false, wb, false, STORE);
      set[lru[index]].tag = tag;
      set[lru[index]].dirty_f = true;
      set[lru[index]].state = VALID;
      lru[index] = (lru[index] +1) % cache->assoc;
      return false;

    case LD_MISS:
      for(int i = 0; i < cache->assoc; i++){
        if(set[i].tag == tag && set[i].state == VALID){
          hit = true;
          if(cache->protocol == VI){
            if (set[i].dirty_f == true) update_stats(cache->stats,hit,true,false,LD_MISS);
            set[i].state = INVALID;
            return hit;
          }
        }
      }
      update_stats(cache->stats,hit,false,false,LD_MISS);
      return hit;
    
    case ST_MISS:
      for(int i = 0; i < cache->assoc; i++){
          if(set[i].tag == tag && set[i].state == VALID){
            hit = true;
            if (cache->protocol == VI) {
              if (set[i].dirty_f == true) update_stats(cache->stats,hit,true,false,ST_MISS);
              set[i].state = INVALID;
              return true;
            }
          }
      }
      update_stats(cache->stats, hit, false, false, ST_MISS);
      return hit;

  }
  return true;
}
