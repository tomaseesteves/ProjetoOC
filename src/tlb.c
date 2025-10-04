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

void tlb_invalidate(va_t virtual_page_number)
{
  // TODO: implement TLB entry invalidation.
  for (int i = 0; i < TLB_L1_SIZE; i++)
  {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number)
    {
      tlb_l1[i].valid = false;
      tlb_l1_invalidations++;
      increment_time(TLB_L1_LATENCY_NS);
      break;
    }
  }
  increment_time(TLB_L1_LATENCY_NS);
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op)
{
  // TODO: implement the TLB logic.
  va_t virtual_page_offset = virtual_address & PAGE_OFFSET_MASK;

  for (int i = 0; i < TLB_L1_SIZE; i++)
  {
    // If page is found:
    if ((tlb_l1[i].virtual_page_number == ((virtual_address >> PAGE_SIZE_BITS) & PAGE_INDEX_MASK)) && tlb_l1[i].valid)
    {
      tlb_l1_hits++;
      tlb_l1[i].last_access = get_time();
      if (op == OP_WRITE)
        tlb_l1[i].dirty = true;

      // Translate virtual address 
      pa_dram_t translated_address = (tlb_l1[i].physical_page_number << PAGE_SIZE_BITS) | virtual_page_offset;
      increment_time(TLB_L1_LATENCY_NS);

      return translated_address;
    }
  }

  // In case entry doesn't exist in TLB, finds empty entry or uses LRU replacement
  int new_index = 0;
  tlb_l1_misses++;

  // Full TLB -> LRU replacement
  if (sizeof(tlb_l1) / sizeof(tlb_l1[0]) == TLB_L1_SIZE) {
    uint64_t min_access = tlb_l1[0].last_access;
    for (int i = 1; i < TLB_L1_SIZE; i++)
    {
      if (tlb_l1[i].last_access < min_access)
      {
        min_access = tlb_l1[i].last_access;
        new_index = i;
      }
    }  
  }
  // TLB has empty entries, looks sequentially for one
  else {
    for (int i = 1; i < TLB_L1_SIZE; i++)
    {
      if (!tlb_l1[i].valid)
      {
        new_index = i;
        break;
      }
    }
  }
  
  // Translates virtual address to physical address
  increment_time(TLB_L1_LATENCY_NS);
  pa_dram_t translated_access = page_table_translate(virtual_address, op);

  log_dbg("Evicting TLB L1 entry i=%d VPN=%" PRIx64 " PPN=%" PRIx64 " valid=%d dirty=%d...",
      new_index, tlb_l1[new_index].virtual_page_number, 
      tlb_l1[new_index].physical_page_number, 
      tlb_l1[new_index].valid, tlb_l1[new_index].dirty
  );

  // Write Back Policy
  if (tlb_l1[new_index].valid && tlb_l1[new_index].dirty) {
    log_dbg("***** TLB L1 write back *****");
    pa_dram_t evicted_address = (tlb_l1[new_index].physical_page_number << PAGE_SIZE_BITS);
    write_back_tlb_entry(evicted_address);
  }

  tlb_l1[new_index].virtual_page_number = (virtual_address >> PAGE_SIZE_BITS) & PAGE_INDEX_MASK;
  tlb_l1[new_index].physical_page_number = (translated_access >> PAGE_SIZE_BITS) & PAGE_INDEX_MASK;
  tlb_l1[new_index].last_access = get_time();
  tlb_l1[new_index].valid = true;
  tlb_l1[new_index].dirty = (op == OP_WRITE);

  return translated_access;
}
