#include "mem.h"
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

#define DEBUG 1
#define LOCAL_COALESCE 2
#define GLOBAL_COALESCE 1
#define NO_COALESCE 0
/* utils */
#define BLOCK_SIZE 32
#define ALIGN(size, word) (((size) + (word - 1)) & ~(word - 1))
#define MATH_CEIL(x) ((x - (int)(x)) > 0 ? (int)(x + 1) : (int)(x))
#define MATH_MAX(x, y) (((x) > (y)) ? (x) : (y))

typedef struct block_t
{
    uint64_t footer;           /* size of previous chunck in bytes*/
    uint32_t chunck_size;      /* size of current chunck in bytes*/
    uint16_t merge;            /* previous is free */
    uint16_t free;             /* current is free */
    struct block_t *free_prev; /* previous free chunck */
    struct block_t *free_next; /* next free chunck */
} block;

/* global variables */
block *memory_head;
block *free_mem_head;
block *current_worst_fit;
int m_error = 0;

/* prototypes */
static block *find_worst_fit(void);

int Mem_Init(long sizeOfRegion)
{
    //duplicate call
    if (memory_head != NULL)
        return -1;
    if (sizeOfRegion <= 0)
    {
        m_error = E_BAD_ARGS;
        return -1;
    }
    if (DEBUG)
        printf("size requested is %lu\n", sizeOfRegion);
    if (DEBUG)
        printf("size requested aligned is %lu\n", sizeOfRegion);
    long overhead = (sizeOfRegion / 8 + 1) * BLOCK_SIZE;
    if (DEBUG)
        printf("overhead needed is %lu\n", overhead);
    long total_size = sizeOfRegion + overhead;
    total_size = ALIGN(total_size, 8);
    if (DEBUG)
        printf("size requested aligned is %d\n", (int)total_size);
    //page-align heap size
    int PAGE_SIZE = getpagesize();
    int page_alignment = MATH_CEIL(total_size / (double)PAGE_SIZE) * PAGE_SIZE;
    void *heap = mmap(NULL, page_alignment, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (heap == MAP_FAILED)
    {
        m_error = E_BAD_ARGS;
        return -1;
    }
    block header = {(uint64_t)0, (uint32_t)(page_alignment - BLOCK_SIZE), (uint16_t)0, (uint16_t)1, NULL, NULL};
    memory_head = (block *)heap;
    *memory_head = header;
    free_mem_head = memory_head;
    current_worst_fit = free_mem_head;
    if (DEBUG)
        printf("Memory head p is %p\n", memory_head);
    if (DEBUG)
        printf("Free head p is %p\n", free_mem_head);
    if (DEBUG)
        printf("footer %lu size %d merge %d free %d prev %p next %p\n", memory_head->footer, memory_head->chunck_size, memory_head->merge, memory_head->free, memory_head->free_prev, memory_head->free_next);
    return 0;
}

void *Mem_Alloc(long size)
{
    if (size <= 0)
    {
        m_error = E_BAD_ARGS;
        return NULL;
    }
    long required_size = ALIGN(size, 8);
    if (current_worst_fit == NULL || current_worst_fit->chunck_size < required_size + BLOCK_SIZE)
    {
        m_error = E_NO_SPACE;
        return NULL;
    }
    void *usr_address = (void*)current_worst_fit + BLOCK_SIZE;
    if (DEBUG) printf("user address is %p has %d memory\n",usr_address,required_size);
    //create new block header
    void *newest = usr_address + required_size;
    int newest_size = current_worst_fit->chunck_size - required_size-BLOCK_SIZE;
     if (DEBUG) printf("newest free address is %p has %d memory\n",newest,newest_size);
    bool create_newest = newest_size > 8;
    //update current block
    current_worst_fit->free = 0;
    //create new block
    if (create_newest)
    {
        current_worst_fit->chunck_size = required_size;
        block newest_block = {(uint64_t)required_size, (uint32_t)newest_size, (uint16_t)0, (uint16_t)1, current_worst_fit->free_prev, current_worst_fit->free_next};
        *((block *)newest) = newest_block;
        if (current_worst_fit->free_prev != NULL)
        {
            current_worst_fit->free_prev->free_next = newest;
        }
        if (current_worst_fit->free_next != NULL)
        {
            current_worst_fit->free_next->free_prev = newest;
        }
        if (free_mem_head == current_worst_fit)
            free_mem_head = newest;
    }
    current_worst_fit->free_next = NULL;
    current_worst_fit->free_prev = NULL;
    //update current worst fit
    current_worst_fit = find_worst_fit();
    if (DEBUG) printf("New worst fit is %p\n",current_worst_fit);
    if (DEBUG && current_worst_fit==NULL) printf("New worst fit is NULL\n");
    return usr_address;
}

int Mem_Free(void *ptr, int coalesce)
{
    return 0;
}

void Mem_Dump()
{
    block *current = free_mem_head;
    while (current != NULL)
    {
        assert(current->free == 1);
        printf("pointer %p footer %lu size %d merge %d free %d prev %p next %p\n",current, current->footer, current->chunck_size, current->merge, current->free, current->free_prev, current->free_next);
        current = free_mem_head->free_next;
    }
}

block *find_worst_fit()
{
    uint32_t max_size = 0;
    block *current = free_mem_head;
    while (current != NULL)
    {
        assert(current->free == 1);
        if (MATH_MAX(current->chunck_size, max_size) == current->chunck_size)
            return current;
        current = free_mem_head->free_next;
    }
    return NULL;
}