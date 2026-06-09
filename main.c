#include <stdio.h>
#include <stdint.h>

__attribute__((aligned(16))) uint8_t stack[4096];

int main(){
  for (int i = 0; i < 4096; i++){
    printf("%p\n",&stack[i]);
  }
  printf("first:%p\n",&stack[0]);
  printf("last:%p\n",&stack[4096-1]);
  return 0;
}
