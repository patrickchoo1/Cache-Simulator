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

  // FIX THIS CODE!
  // first, correctly set these 5 variables. THEY ARE ALL WRONG
  // note: you may find math.h's log2 function useful
  cache->n_cache_line = capacity/block_size;
  cache->n_set = capacity/(assoc*block_size);
  cache->n_offset_bit = log2(block_size);
  cache->n_index_bit = log2(cache->n_set);
  cache->n_tag_bit = ADDRESS_SIZE - (cache->n_offset_bit) - (cache->n_index_bit);

  // next create the cache lines and the array of LRU bits
  // - malloc an array with n_rows
  // - for each element in the array, malloc another array with n_col
  // FIX THIS CODE!
  cache->lines = malloc(sizeof(cache_line_t)* cache->n_set);
  for(int i=0; i<cache->n_set;i++){
    cache->lines[i] = malloc(sizeof(cache_line_t) * assoc);
  }
  
  cache->lru_way = malloc((cache->n_set) * sizeof(int));
  // initializes cache tags to 0, dirty bits to false,
  // state to INVALID, and LRU bits to 0
  // FIX THIS CODE!

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
  // FIX THIS CODE!
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
  // FIX THIS CODE!
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
  // fix
  addr = addr >> cache-> n_offset_bit;
  addr = addr << cache-> n_offset_bit;
  // long mask = ~0;
  // mask = mask >> (ADDRESS_SIZE - cache->n_offset_bit);
  // long baddr = addr & mask;
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
  // FIX THIS CODE!
  int index = get_cache_index(cache, addr);
  int tag = get_cache_tag(cache, addr);
  bool hit = false;
  bool wb = false;
  if(cache->protocol == MSI){
    return msi(cache, addr, action);
  }

  switch(action){
    case LOAD:
      for(int i = 0; i < cache->assoc; i++){
        if(cache->lines[index][i].tag == tag && cache->lines[index][i].state == VALID){
          update_stats(cache->stats, true, false, false, LOAD);
          cache->lru_way[index] = (cache->lru_way[index]+1) % cache->assoc;
          return true;
        }
      }

      if (cache->lines[index][cache->lru_way[index]].dirty_f == true) wb = true;
      cache->lines[index][cache->lru_way[index]].tag = tag;
      cache->lines[index][cache->lru_way[index]].dirty_f = false;
      cache->lines[index][cache->lru_way[index]].state = VALID;
      update_stats(cache->stats, false, wb, false, LOAD);
      cache->lru_way[index] = (cache->lru_way[index] +1) % cache->assoc;
      return false;

    case STORE:
      for(int i = 0; i < cache->assoc; i++){
        if(cache->lines[index][i].tag == tag && cache->lines[index][i].state == VALID){
          cache->lines[index][i].dirty_f = true;
          cache->lru_way[index] = (cache->lru_way[index] +1) % cache->assoc;
          update_stats(cache->stats, true, false, false, STORE);
          return true;
        }
      }
      if (cache->lines[index][cache->lru_way[index]].dirty_f == true) wb = true;
      update_stats(cache->stats, false, wb, false, STORE);
      cache->lines[index][cache->lru_way[index]].tag = tag;
      cache->lines[index][cache->lru_way[index]].dirty_f = true;
      cache->lines[index][cache->lru_way[index]].state = VALID;
      cache->lru_way[index] = (cache->lru_way[index] +1) % cache->assoc;
      return false;

    case LD_MISS:
      for(int i = 0; i < cache->assoc; i++){
        if(cache->lines[index][i].tag == tag && cache->lines[index][i].state == VALID){
          hit = true;
          if(cache->protocol == VI){
            if (cache->lines[index][i].dirty_f == true) update_stats(cache->stats,hit,wb,false,LD_MISS);
            cache->lines[index][i].state = INVALID;
            return false;
          }
        }
      }
      update_stats(cache->stats,hit,false,false,LD_MISS);
      return false;
    
    case ST_MISS:
      for(int i = 0; i < cache->assoc; i++){
          if(cache->lines[index][i].tag == tag && cache->lines[index][i].state == VALID){
            hit = true;
            if (cache->protocol == VI) {
              if (cache->lines[index][i].dirty_f == true) update_stats(cache->stats,hit,wb,false,ST_MISS);
              cache->lines[index][i].state = INVALID;
              return false;
            }
          }
      }
      update_stats(cache->stats, hit, false, false, ST_MISS);
      return false;

  }
  return true;
}

bool msi(cache_t *cache, unsigned long addr,enum action_t action){
  int index = get_cache_index(cache, addr);
  int tag = get_cache_tag(cache, addr);
  bool hit = false;
  bool wb = false;
  switch(action){
    case LOAD:
    // IF invalid, turns to shared
    // IF modified, stays at modified
    // IF shared, stays at shared
      for(int i = 0; i < cache->assoc; i++){
        if(cache->lines[index][i].tag == tag && cache->lines[index][i].state != INVALID){
          update_stats(cache->stats, true, false, false, LOAD);
          cache->lru_way[index] = (cache->lru_way[index]+1) % cache->assoc;
          return true;
        }else if(cache->lines[index][i].tag == tag && cache->lines[index][i].state == INVALID){
          update_stats(cache->stats, false, false, false, LOAD);
          cache->lines[index][i].tag = tag;
          cache->lines[index][i].dirty_f = false;
          cache->lines[index][i].state = SHARED;
          return false;
        }
      }

      if (cache->lines[index][cache->lru_way[index]].dirty_f == true) wb = true;
      cache->lines[index][cache->lru_way[index]].tag = tag;
      cache->lines[index][cache->lru_way[index]].dirty_f = false;
      cache->lines[index][cache->lru_way[index]].state = VALID;
      update_stats(cache->stats, false, wb, false, LOAD);
      cache->lru_way[index] = (cache->lru_way[index] +1) % cache->assoc;
      return false;

    case STORE:
      // IF invalid, then turns to modified
      // IF modified, stays at modified
      // IF shared, then turns to modified
      for(int i = 0; i < cache->assoc; i++){
        if(cache->lines[index][i].tag == tag){
          cache->lines[index][i].dirty_f = true;
          cache->lru_way[index] = (cache->lru_way[index] +1) % cache->assoc;
          update_stats(cache->stats, true, false, false, STORE);
          cache->lines[index][cache->lru_way[index]].state = MODIFIED;
          return true;
        }
      }
      if (cache->lines[index][cache->lru_way[index]].dirty_f == true) wb = true;
      update_stats(cache->stats, false, wb, false, STORE);
      cache->lines[index][cache->lru_way[index]].tag = tag;
      cache->lines[index][cache->lru_way[index]].dirty_f = true;
      cache->lines[index][cache->lru_way[index]].state = MODIFIED;
      cache->lru_way[index] = (cache->lru_way[index] +1) % cache->assoc;
      return false;

    case LD_MISS:
      // IF invalid, then stays at invalid
      // IF modified, then turns to shared (wb)
      // IF shared, then stays at shared
      for(int i = 0; i < cache->assoc; i++){
        if(cache->lines[index][i].tag == tag && cache->lines[index][i].state == VALID){
          hit = true;
          if(cache->protocol == VI){
            if (cache->lines[index][i].dirty_f == true) update_stats(cache->stats,hit,wb,false,LD_MISS);
            cache->lines[index][i].state = INVALID;
            return false;
          }
        }
      }
      update_stats(cache->stats,hit,false,false,LD_MISS);
      return false;
    
    case ST_MISS:
      // IF invalid, stays at invlaid
      // IF modified, then turns to invalid (wb)
      // IF shared, then turns to invalid
      for(int i = 0; i < cache->assoc; i++){
          if(cache->lines[index][i].tag == tag){
            hit = true;
            if (cache->lines[index][i].state == MODIFIED) {
              update_stats(cache->stats,hit,wb,false,ST_MISS);
            }
            // if (cache->protocol == VI) {
            //   if (cache->lines[index][i].dirty_f == true) update_stats(cache->stats,hit,wb,false,ST_MISS);
            //   cache->lines[index][i].state = INVALID;
            //   return false;
            }
          }
      }
      update_stats(cache->stats, hit, false, false, ST_MISS);
      return false;
  return true;
}