#include <stdio.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define NUM_PAGES 8

__attribute__((aligned(16))) uint8_t physical_ram[PAGE_SIZE * NUM_PAGES];

uint64_t translate(uint64_t virtual_addr,int *page_table){
  int vpn = virtual_addr/PAGE_SIZE;
  int offset = virtual_addr%PAGE_SIZE;

  if (page_table[vpn] == -1){
    printf("Hardware Page Fault! Virtual Page %d not mapped",vpn);
    return -1;
  }

  int ppn = page_table[vpn];
  return ppn * PAGE_SIZE + offset;
}

int main(){
  int page_table[NUM_PAGES];

  for (int i = 0; i < NUM_PAGES; i++){
    page_table[i] = -1;
  }

  page_table[3] = 5;
  printf("OS mapped virtual page 3-> physical page 5\n");
  
  uint64_t vaddr = 0x3070;
  uint64_t paddr = translate(vaddr,page_table);

  if (paddr != (uint64_t)-1){
    printf("[Hardware] Virtual 0x%lx → Physical 0x%lx\n", vaddr, paddr);
    physical_ram[paddr] = 42;  
  }
  
  return 0;
}
