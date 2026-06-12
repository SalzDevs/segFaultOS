#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <raylib.h>

#define PAGE_SIZE 4096
#define PGSIZE 4096
#define NUM_PAGES 8
#define PGSHIFT 12

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1ULL << (9 + 9 + 9 + 12 - 1))

// PX macro from xv6 - extracts 9-bit index for each page table level
#define PXMASK         0x1FF // 9 bits mask (0b111111111)
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64_t)(va)) >> PXSHIFT(level)) & PXMASK)

// Page table entry type (64-bit unsigned integer)
typedef uint64_t pte_t;

// Page table type (pointer to array of PTEs)
typedef uint64_t* pagetable_t;

__attribute__((aligned(PAGE_SIZE))) static uint8_t physical_ram[PAGE_SIZE * NUM_PAGES];
static pagetable_t kernel_pagetable;

// Simplified xv6-like memory layout constants
#define UART0       0x10000000ULL
#define VIRTIO0     0x10001000ULL
#define PLIC        0x0c000000ULL
#define KERNBASE    0x80000000ULL
#define PHYSTOP     (KERNBASE + 128ULL * 1024ULL * 1024ULL)
#define TRAMPOLINE  (MAXVA - PGSIZE)

// Simulated satp register / TLB fence helpers
static uint64_t simulated_satp;
#define MAKE_SATP(pgtbl) ((uint64_t)(pgtbl))

void sfence_vma(void) {
  // no-op in this simulation
}

void w_satp(uint64_t val) {
  simulated_satp = val;
}

// PTE flags
#define PTE_V (1ULL << 0)  // Valid
#define PTE_R (1ULL << 1)  // Read
#define PTE_W (1ULL << 2)  // Write
#define PTE_X (1ULL << 3)  // Execute
#define PTE_U (1ULL << 4)  // User

// Helper macros to convert between physical addresses and PTEs
#define PA2PTE(pa) ((((uint64_t)(pa)) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)

// Simple memory allocator for simulation
void* kalloc(void) {
  void* ptr = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
  return ptr;
}

void kfree(void* ptr) {
  free(ptr);
}

void panic(const char* msg) {
  fprintf(stderr, "PANIC: %s\n", msg);
  exit(1);
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va. If alloc!=0,
// create any required page-table pages.
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

int mappages(pagetable_t pagetable, uint64_t va, uint64_t size, uint64_t pa, int perm) {
  uint64_t a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | (uint64_t)perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64_t va, uint64_t pa, uint64_t sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Make a direct-map page table for the kernel (xv6-style, simplified).
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl = (pagetable_t)kalloc();
  if (kpgtbl == 0)
    panic("kvmmake: kalloc failed");

  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map a small kernel text segment executable and read-only (simulation)
  uint64_t etext = KERNBASE + 2 * PGSIZE;
  kvmmap(kpgtbl, KERNBASE, KERNBASE, etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data / RAM region as read-write (simulation)
  kvmmap(kpgtbl, etext, etext, PHYSTOP - etext, PTE_R | PTE_W);

  // map trampoline
  kvmmap(kpgtbl, TRAMPOLINE, TRAMPOLINE, PGSIZE, PTE_R | PTE_X);

  return kpgtbl;
}

// initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void) {
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart(void) {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

static void FormatFlags(uint64_t pte, char* out, int outSize) {
  snprintf(out, outSize, "%c%c%c%c%c", 
    (pte & PTE_V) ? 'V' : '-',
    (pte & PTE_R) ? 'R' : '-',
    (pte & PTE_W) ? 'W' : '-',
    (pte & PTE_X) ? 'X' : '-',
    (pte & PTE_U) ? 'U' : '-');
}

static void DrawNode(Rectangle r, const char* title, const char* line1, const char* line2, Color c) {
  DrawRectangleRec(r, Fade(c, 0.10f));
  DrawRectangleLinesEx(r, 2.0f, c);
  DrawText(title, (int)r.x + 10, (int)r.y + 8, 20, c);
  DrawText(line1, (int)r.x + 10, (int)r.y + 40, 18, RAYWHITE);
  DrawText(line2, (int)r.x + 10, (int)r.y + 64, 18, RAYWHITE);
}

int main(void) {
  kvminit();
  kvminithart();

  uint64_t trackedVA = KERNBASE;
  const uint64_t customVA = 0x400000ULL;
  bool customMapped = false;

  InitWindow(1280, 760, "segFaultOS - Page Table Visualizer (raylib)");
  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    if (IsKeyPressed(KEY_ONE)) trackedVA = UART0;
    if (IsKeyPressed(KEY_TWO)) trackedVA = VIRTIO0;
    if (IsKeyPressed(KEY_THREE)) trackedVA = KERNBASE;
    if (IsKeyPressed(KEY_FOUR)) trackedVA = TRAMPOLINE;

    if (IsKeyPressed(KEY_M)) {
      if (!customMapped) {
        if (mappages(kernel_pagetable, customVA, PGSIZE, (uint64_t)&physical_ram[0], PTE_R | PTE_W | PTE_U) == 0) {
          customMapped = true;
        }
      }
      trackedVA = customVA;
    }

    pte_t* pte = walk(kernel_pagetable, trackedVA, 0);
    bool mapped = (pte != 0) && ((*pte & PTE_V) != 0);
    uint64_t paBase = mapped ? PTE2PA(*pte) : 0;
    uint64_t paWithOffset = mapped ? (paBase | (trackedVA & (PGSIZE - 1))) : 0;

    char flags[16] = {0};
    if (mapped) FormatFlags(*pte, flags, sizeof(flags));

    char vaLine[128];
    char satpLine[128];
    snprintf(vaLine, sizeof(vaLine), "VA: 0x%llx", (unsigned long long)trackedVA);
    snprintf(satpLine, sizeof(satpLine), "satp(root): 0x%llx", (unsigned long long)simulated_satp);

    char l2[64], l1[64], l0[64], off[64];
    snprintf(l2, sizeof(l2), "VPN2: %llu", (unsigned long long)PX(2, trackedVA));
    snprintf(l1, sizeof(l1), "VPN1: %llu", (unsigned long long)PX(1, trackedVA));
    snprintf(l0, sizeof(l0), "VPN0: %llu", (unsigned long long)PX(0, trackedVA));
    snprintf(off, sizeof(off), "OFF : 0x%03llx", (unsigned long long)(trackedVA & 0xFFF));

    char pteLine[128];
    char paLine[128];
    if (mapped) {
      snprintf(pteLine, sizeof(pteLine), "PTE: 0x%llx  [%s]", (unsigned long long)*pte, flags);
      snprintf(paLine, sizeof(paLine), "PA : 0x%llx", (unsigned long long)paWithOffset);
    } else {
      snprintf(pteLine, sizeof(pteLine), "PTE: <unmapped>");
      snprintf(paLine, sizeof(paLine), "PA : <page fault>");
    }

    BeginDrawing();
    ClearBackground((Color){20, 22, 28, 255});

    DrawText("segFaultOS Visualizer - Renderer calls OS functions (walk/mappages/kvm*)", 24, 18, 22, SKYBLUE);
    DrawText("Keys: [1]UART [2]VIRTIO [3]KERNBASE [4]TRAMPOLINE [M] map+track custom VA", 24, 48, 18, LIGHTGRAY);

    DrawText(vaLine, 24, 84, 20, RAYWHITE);
    DrawText(satpLine, 24, 110, 20, RAYWHITE);

    Rectangle nVA = {40, 170, 300, 110};
    Rectangle nL2 = {380, 170, 260, 110};
    Rectangle nL1 = {680, 170, 260, 110};
    Rectangle nL0 = {980, 170, 260, 110};

    DrawNode(nVA, "Virtual Address", l2, l1, ORANGE);
    DrawText(l0, (int)nVA.x + 10, (int)nVA.y + 88, 18, RAYWHITE);
    DrawText(off, (int)nVA.x + 150, (int)nVA.y + 88, 18, RAYWHITE);

    DrawNode(nL2, "Level 2 table", "index = VPN2", "entry -> L1", GOLD);
    DrawNode(nL1, "Level 1 table", "index = VPN1", "entry -> L0", GOLD);
    DrawNode(nL0, "Level 0 table", "index = VPN0", mapped ? "entry = valid PTE" : "entry = invalid", mapped ? GREEN : RED);

    DrawLineEx((Vector2){nVA.x + nVA.width, nVA.y + 55}, (Vector2){nL2.x, nL2.y + 55}, 3.0f, LIGHTGRAY);
    DrawLineEx((Vector2){nL2.x + nL2.width, nL2.y + 55}, (Vector2){nL1.x, nL1.y + 55}, 3.0f, LIGHTGRAY);
    DrawLineEx((Vector2){nL1.x + nL1.width, nL1.y + 55}, (Vector2){nL0.x, nL0.y + 55}, 3.0f, LIGHTGRAY);

    DrawText(pteLine, 40, 330, 22, mapped ? GREEN : RED);
    DrawText(paLine, 40, 362, 22, mapped ? GREEN : RED);

    DrawText("Theory: walk() finds PTE slot, mappages() writes mappings, kvminithart() installs satp + TLB fence.", 24, 708, 18, LIGHTGRAY);

    EndDrawing();
  }

  CloseWindow();
  return 0;
}
