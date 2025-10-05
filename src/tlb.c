#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

typedef struct
{
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

tlb_entry_t tlb_l1[TLB_L1_SIZE];
tlb_entry_t tlb_l2[TLB_L2_SIZE];

uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

uint64_t tlb_l2_hits = 0;
uint64_t tlb_l2_misses = 0;
uint64_t tlb_l2_invalidations = 0;

uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

uint64_t get_total_tlb_l2_hits() { return tlb_l2_hits; }
uint64_t get_total_tlb_l2_misses() { return tlb_l2_misses; }
uint64_t get_total_tlb_l2_invalidations() { return tlb_l2_invalidations; }

void tlb_init()
{
  memset(tlb_l1, 0, sizeof(tlb_l1));
  memset(tlb_l2, 0, sizeof(tlb_l2));
  tlb_l1_hits = 0;
  tlb_l1_misses = 0;
  tlb_l1_invalidations = 0;
  tlb_l2_hits = 0;
  tlb_l2_misses = 0;
  tlb_l2_invalidations = 0;
}

// Finds empty entry, if not sends signal that L1 is full
int find_new_tlb_entry(tlb_entry_t *tlb_cache, int tlb_size) {
  uint64_t min_access = tlb_cache[0].last_access;
  int lru_index = 0;

  for (int i = 0; i < tlb_size; i++) {
    // If entry is empty, return index early
    if (tlb_cache[i].valid == 0) {
      return i;
    }
    // Save least recently used index
    else if (tlb_cache[i].last_access < min_access)
    {
      min_access = tlb_cache[i].last_access;
      lru_index = i;
    }
  }
  // If cache is full, return least recently used index
  return lru_index;
}

// Write Back Policy for TLB L1 Cache
void write_back_l1(int l1_index) {
  int evicted_index = 0;
  int found = 0;

  for (int i = 0; i < TLB_L2_SIZE; i++)
  {
    // If page is found:
    if (tlb_l2[i].valid && (tlb_l2[i].virtual_page_number == tlb_l1[l1_index].virtual_page_number))
    {
      evicted_index = i;
      found = 1;
      break;
    }
  }

  // If page is not found
  if (!found) evicted_index = find_new_tlb_entry(tlb_l2, TLB_L2_SIZE);
  
  tlb_l2[evicted_index].virtual_page_number = tlb_l1[l1_index].virtual_page_number;
  tlb_l2[evicted_index].physical_page_number = tlb_l1[l1_index].physical_page_number;
  tlb_l2[evicted_index].last_access = get_time();
  tlb_l2[evicted_index].valid = true;
  tlb_l2[evicted_index].dirty = true;
}

void tlb_invalidate(va_t virtual_page_number)
{
  // Checks for invalid entry in TLB L1 cache
  for (int i = 0; i < TLB_L1_SIZE; i++)
  {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number)
    {
      tlb_l1[i].valid = false;
      tlb_l1[i].dirty = false;
      tlb_l1_invalidations++;
      break;
    }
  }
  increment_time(TLB_L1_LATENCY_NS);

  // Checks for invalid entry in TLB L2 cache
  for (int i = 0; i < TLB_L2_SIZE; i++)
  {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number)
    {
      tlb_l2[i].valid = false;
      tlb_l2[i].dirty = false;
      tlb_l2_invalidations++;
      break;
    }
  }
  increment_time(TLB_L2_LATENCY_NS);
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op)
{
  va_t virtual_page_offset = virtual_address & PAGE_OFFSET_MASK;
  va_t virtual_page_number = (virtual_address >> PAGE_SIZE_BITS) & PAGE_INDEX_MASK;
  pa_dram_t translated_address;
  int new_l1_index = 0;
  int new_l2_index = 0;

  // Searches for entry in TLB L1 cache
  for (int i = 0; i < TLB_L1_SIZE; i++)
  {
    // If page is found:
    if (tlb_l1[i].valid && (tlb_l1[i].virtual_page_number == virtual_page_number))
    {
      tlb_l1_hits++;
      tlb_l1[i].last_access = get_time();
      if (op == OP_WRITE)
        tlb_l1[i].dirty = true;

      // Translate virtual address 
      translated_address = (tlb_l1[i].physical_page_number << PAGE_SIZE_BITS) | virtual_page_offset;
      increment_time(TLB_L1_LATENCY_NS);

      return translated_address;
    }
  }
  tlb_l1_misses++;
  increment_time(TLB_L1_LATENCY_NS);

  // Searches for entry in TLB L2 cache
  for (int i = 0; i < TLB_L2_SIZE; i++)
  {
    // If page is found:
    if (tlb_l2[i].valid && (tlb_l2[i].virtual_page_number == virtual_page_number))
    {
      tlb_l2_hits++;
      tlb_l2[i].last_access = get_time();
      if (op == OP_WRITE)
        tlb_l2[i].dirty = true;
      
      // Update TLB L1 if the entry was found in TLB L2
      
      // Finds new index in TLB L1
      new_l1_index = find_new_tlb_entry(tlb_l1, TLB_L1_SIZE);

      log_dbg("Evicting TLB L1 entry i=%d VPN=%" PRIx64 " PPN=%" PRIx64 " valid=%d dirty=%d...",
        new_l1_index, tlb_l1[new_l1_index].virtual_page_number, 
        tlb_l1[new_l1_index].physical_page_number, 
        tlb_l1[new_l1_index].valid, tlb_l1[new_l1_index].dirty
      );

      // Write Back Policy from L1 to L2
      if (tlb_l1[new_l1_index].valid && tlb_l1[new_l1_index].dirty) {
        log_dbg("***** TLB L1 write back to L2 *****");
        write_back_l1(new_l1_index);
      }

      tlb_l1[new_l1_index].virtual_page_number = tlb_l2[i].virtual_page_number;
      tlb_l1[new_l1_index].physical_page_number = tlb_l2[i].physical_page_number;
      tlb_l1[new_l1_index].last_access = get_time();
      tlb_l1[new_l1_index].valid = true;
      if (op == OP_WRITE) 
        tlb_l1[new_l1_index].dirty = true;
      
      // Translate virtual address 
      translated_address = (tlb_l2[i].physical_page_number << PAGE_SIZE_BITS) | virtual_page_offset;
      increment_time(TLB_L2_LATENCY_NS);

      return translated_address;
    }
  }
  tlb_l2_misses++;
  increment_time(TLB_L2_LATENCY_NS);
  
  // Translates virtual address to physical address
  translated_address = page_table_translate(virtual_address, op);
  va_t physical_page_number = (translated_address >> PAGE_SIZE_BITS) & PAGE_INDEX_MASK;

  // Update TLB L2 with the new entry

  // Finds new index in TLB L2
  new_l2_index = find_new_tlb_entry(tlb_l2, TLB_L2_SIZE);
   
  log_dbg("Evicting TLB L2 entry i=%d VPN=%" PRIx64 " PPN=%" PRIx64 " valid=%d dirty=%d...",
      new_l2_index, tlb_l2[new_l2_index].virtual_page_number, 
      tlb_l2[new_l2_index].physical_page_number, 
      tlb_l2[new_l2_index].valid, tlb_l2[new_l2_index].dirty
  );
  
  // Write Back Policy
  if (tlb_l2[new_l2_index].valid && tlb_l2[new_l2_index].dirty) {
    log_dbg("***** TLB L2 write back *****");
    pa_dram_t evicted_address = (tlb_l2[new_l2_index].physical_page_number << PAGE_SIZE_BITS);
    write_back_tlb_entry(evicted_address);
  }
  
  tlb_l2[new_l2_index].virtual_page_number = virtual_page_number;
  tlb_l2[new_l2_index].physical_page_number = physical_page_number;
  tlb_l2[new_l2_index].last_access = get_time();
  tlb_l2[new_l2_index].valid = true;
  tlb_l2[new_l2_index].dirty = (op == OP_WRITE);
  

  // Update TLB L1 with the new entry
  // Finds new index in TLB L1
  new_l1_index = find_new_tlb_entry(tlb_l1, TLB_L1_SIZE);
  log_dbg("Evicting TLB L1 entry i=%d VPN=%" PRIx64 " PPN=%" PRIx64 " valid=%d dirty=%d...",
        new_l1_index, tlb_l1[new_l1_index].virtual_page_number, 
        tlb_l1[new_l1_index].physical_page_number, 
        tlb_l1[new_l1_index].valid, tlb_l1[new_l1_index].dirty
    );

  // Write Back Policy from L1 to L2
  if (tlb_l1[new_l1_index].valid && tlb_l1[new_l1_index].dirty) {
    log_dbg("***** TLB L1 write back to L2 *****");
    write_back_l1(new_l1_index);
  }

  tlb_l1[new_l1_index].virtual_page_number = virtual_page_number;
  tlb_l1[new_l1_index].physical_page_number = physical_page_number;
  tlb_l1[new_l1_index].last_access = get_time();
  tlb_l1[new_l1_index].valid = true;
  tlb_l1[new_l1_index].dirty = (op == OP_WRITE);

  return translated_address;
}
