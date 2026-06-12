#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define PAGE_SIZE 4096
#define PGSIZE 4096
#define NUM_PAGES 8
#define PGSHIFT 12
#define MAXVA (1L << 39)  // One beyond the highest possible virtual address

// PX macro from xv6 - extracts 9-bit index for each page table level
#define PXMASK         0x1FF // 9 bits mask (0b111111111)
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64_t)(va)) >> PXSHIFT(level)) & PXMASK)

/*
 * PX(level, va) extracts the 9-bit page table index for a given level
 * 
 * Virtual address (39 bits):
 * ┌──────────┬──────────┬──────────┬────────────┐
 * │  VPN[2]  │  VPN[1]  │  VPN[0]  │   offset   │
 * │ 9 bits   │ 9 bits   │ 9 bits   │  12 bits   │
 * └──────────┴──────────┴──────────┴────────────┘
 *  38      30 29      21 20      12 11         0
 * 
 * Level 2: bits 30-38
 * Level 1: bits 21-29
 * Level 0: bits 12-20
 */

__attribute__((aligned(PAGE_SIZE))) uint8_t physical_ram[PAGE_SIZE * NUM_PAGES];

// Page table entry type (64-bit unsigned integer)
typedef uint64_t pte_t;

// Page table type (pointer to array of PTEs)
typedef uint64_t* pagetable_t;

// PTE flags
#define PTE_V (1L << 0)  // Valid
#define PTE_R (1L << 1)  // Read
#define PTE_W (1L << 2)  // Write
#define PTE_X (1L << 3)  // Execute
#define PTE_U (1L << 4)  // User

// Helper macros to convert between physical addresses and PTEs
#define PA2PTE(pa) ((((uint64_t)(pa)) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)

// Simple memory allocator for simulation
void* kalloc() {
    void* ptr = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    if (ptr) {
        printf("[kalloc] Allocated page at %p\n", ptr);
    }
    return ptr;
}

void kfree(void* ptr) {
    free(ptr);
}

// Panic function (simplified)
void panic(const char* msg) {
    fprintf(stderr, "PANIC: %s\n", msg);
    exit(1);
}

/*
 * walk() - Real xv6's page table walk function
 * 
 * Return the address of the PTE in page table pagetable
 * that corresponds to virtual address va. If alloc!=0,
 * create any required page-table pages.
 * 
 * The risc-v Sv39 scheme has three levels of page-table
 * pages. A page-table page contains 512 64-bit PTEs.
 */
pte_t* walk(pagetable_t pagetable, uint64_t va, int alloc) {
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

int mappages(pagetable_t pagetable, uint64_t va, uint64_t size,uint64_t pa, int perm){
  uint64_t last,a;
  pte_t* pte;

  if((va%PAGE_SIZE)!=0) panic("mappages: va not aligned!");
  if((size%PAGE_SIZE)!=0) panic("mappages: size not aligned!");
  if(size==0) panic("mappages: size!");

  a = va;
  last = va + size -PAGE_SIZE;
  for(;;){
    if((pte = walk(pagetable,a,1))==0) return -1;
    
    if(*pte & PTE_V) panic("mappages: remap!");
    
    *pte = PA2PTE(pa)| perm | PTE_V;
    
    if(a==last) break;
    
    a += PGSIZE;
    pa += PGSIZE;
  }

  return 0;
}

int main() {
    printf("=== Testing xv6 walk() implementation ===\n\n");
    
    // Allocate root page table (level 2)
    pagetable_t pagetable = (pagetable_t)kalloc();
    if (!pagetable) {
        printf("Failed to allocate root page table\n");
        return 1;
    }
    memset(pagetable, 0, PGSIZE);
    
    // Test virtual address
    uint64_t vaddr = 0x1000;  // Simple virtual address
    
    printf("\n=== Test 1: Lookup with alloc=0 (should fail) ===\n");
    pte_t *pte = walk(pagetable, vaddr, 0);
    if (pte == 0) {
        printf("Expected: walk returned 0 (page table not allocated)\n");
    }
    
    printf("\n=== Test 2: Create mapping with alloc=1 ===\n");
    pte = walk(pagetable, vaddr, 1);
    if (pte) {
        printf("walk() returned PTE at %p\n", (void*)pte);
        printf("VPN2=%llu, VPN1=%llu, VPN0=%llu\n", PX(2, vaddr), PX(1, vaddr), PX(0, vaddr));
        
        // Create a mapping: map to physical page at offset 0 in physical_ram
        uint64_t pa = (uint64_t)&physical_ram[0];
        *pte = PA2PTE(pa) | PTE_V | PTE_R | PTE_W | PTE_U;
        printf("\nCreated mapping: VA 0x%llx -> PA 0x%llx\n", vaddr, pa);
        printf("PTE value: 0x%llx\n", *pte);
    }
    
    printf("\n=== Test 3: Lookup with alloc=0 (should succeed now) ===\n");
    pte = walk(pagetable, vaddr, 0);
    if (pte) {
        printf("walk() found PTE at %p\n", (void*)pte);
        printf("PTE value: 0x%llx\n", *pte);
        
        // Extract physical address from PTE
        uint64_t pa = PTE2PA(*pte);
        printf("Mapped to physical address: 0x%llx\n", pa);
        
        // Test read/write through the mapping
        *(uint64_t*)pa = 42;
        printf("Wrote 42 to physical memory\n");
        printf("Read back: %llu\n", *(uint64_t*)pa);
    }
    
    printf("\n=== Success! ===\n");
    return 0;
}
