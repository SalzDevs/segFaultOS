#include <stdio.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define NUM_PAGES 8

__attribute__((aligned(16))) uint8_t physical_ram[PAGE_SIZE * NUM_PAGES];

int main(){
  int page_table[NUM_PAGES];
  printf("each page_table elem occupies: %lu bits\n",sizeof(int)*8);

  for (int i = 0; i < NUM_PAGES; i++){
    page_table[i] = -1;
    printf("current memory:%p current val:%d\n",&page_table[i],page_table[i]);
  }
  return 0;
}
