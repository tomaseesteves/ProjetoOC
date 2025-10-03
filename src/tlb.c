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
    if (tlb_l1[i].virtual_page_number == virtual_page_number)
    {
      tlb_l1[i].valid = false;
      tlb_l1_invalidations++;
    }
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op)
{
  // TODO: implement the TLB logic.
  for (int i = 0; i < TLB_L1_SIZE; i++)
  {
    if (tlb_l1[i].virtual_page_number == (virtual_address >> PAGE_OFFSET_BITS))
    {
      tlb_l1_hits++;
      tlb_l1[i].last_access = get_time();
      if (op == OP_WRITE)
        tlb_l1[i].dirty = true;
      return tlb_l1[i].physical_page_number;
    }
  }

  tlb_l1_misses++;
  int lru_index = 0;
  uint64_t min_access = tlb_l1[0].last_access;

  for (int i = 1; i < TLB_L1_SIZE; i++)
  {
    if (tlb_l1[i].last_access < min_access)
    {
      min_access = tlb_l1[i].last_access;
      lru_index = i;
    }
  }

  tlb_l1[lru_index].virtual_page_number = virtual_address >> PAGE_OFFSET_BITS;
  tlb_l1[lru_index].physical_page_number = page_table_translate(virtual_address, op);
  tlb_l1[lru_index].last_access = get_time();
  tlb_l1[lru_index].valid = true;
  tlb_l1[lru_index].dirty = (op == OP_WRITE);

  return page_table_translate(virtual_address, op);
}
