#include "mem.h"
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

#define DEBUG 0
#define LOCAL_COALESCE 2
#define GLOBAL_COALESCE 1
#define NO_COALESCE 0
#define MAGIC_NUMBER 0xABCDEF00
/* utils */
#define BLOCK_SIZE 32
#define ALIGN(size, word) (((size) + (word - 1)) & ~(word - 1))
#define MATH_CEIL(x) ((x - (int)(x)) > 0 ? (int)(x + 1) : (int)(x))
#define MATH_MAX(x, y) (((x) > (y)) ? (x) : (y))

typedef struct block_t
{
    uint32_t footer;           /* size of previous chunck in bytes*/
    uint32_t canary;           /* magic number */
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
block *current_worst_fit_ = NULL;
int m_error = 0;

/* prototypes */
static block *find_worst_fit(void);
static void insert_free_list(block *ptr);
static void local_coalesce(block *ptr);
static void global_coalesce(void);

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
    block header = {(uint32_t)0, (uint32_t)MAGIC_NUMBER, (uint32_t)(page_alignment - BLOCK_SIZE), (uint16_t)0, (uint16_t)1, NULL, NULL};
    memory_head = (block *)heap;
    *memory_head = header;
    free_mem_head = memory_head;
    current_worst_fit = free_mem_head;
    if (DEBUG)
        printf("Memory head p is %p\n", memory_head);
    if (DEBUG)
        printf("Free head p is %p\n", free_mem_head);
    if (DEBUG)
        printf("footer %d size %d merge %d free %d prev %p next %p\n", memory_head->footer, memory_head->chunck_size, memory_head->merge, memory_head->free, memory_head->free_prev, memory_head->free_next);
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
    if (DEBUG)
        printf("WE need %ld but we have %d\n", required_size + BLOCK_SIZE, current_worst_fit->chunck_size);

    if (current_worst_fit == NULL || current_worst_fit->chunck_size < required_size + BLOCK_SIZE)
    {
        m_error = E_NO_SPACE;
        return NULL;
    }
    void *usr_address = (void *)current_worst_fit + BLOCK_SIZE;
    if (DEBUG)
        printf("user address is %p has %ld memory\n", usr_address, required_size);
    //create new block header
    void *newest = usr_address + required_size;
    int newest_size = current_worst_fit->chunck_size - required_size - BLOCK_SIZE;
    if (DEBUG)
        printf("newest free address is %p has %d memory\n", newest, newest_size);
    bool create_newest = newest_size > 8;
    //update current block
    current_worst_fit->free = 0;
    //update next's merge
    bool valid_next = false;
    block *next_block;
    void *next_block_canary = (void *)current_worst_fit + current_worst_fit->chunck_size + BLOCK_SIZE + 4;
    if (*((uint32_t *)next_block_canary) == MAGIC_NUMBER)
    {
        if (DEBUG)
            printf("alloc: valid canary\n");
        valid_next = true;
        //next block was malloced
        next_block = (void *)current_worst_fit + current_worst_fit->chunck_size + BLOCK_SIZE;
        next_block->merge = create_newest ? (uint16_t)1 : (uint16_t)0;
    }
    //create new block
    if (create_newest)
    {
        current_worst_fit->chunck_size = required_size;
        block newest_block = {(uint64_t)required_size, MAGIC_NUMBER, (uint32_t)newest_size, (uint16_t)0, (uint16_t)1, current_worst_fit->free_prev, current_worst_fit->free_next};
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
        //update next's footer
        if (valid_next)
        {
            next_block->footer = (uint32_t)newest_block.chunck_size;
            next_block->merge = (uint16_t)1;
        }
        if (DEBUG && valid_next)
            printf("Next block updated to footer %d size %d merge %d free %d prev %p next %p\n", next_block->footer, next_block->chunck_size, next_block->merge, next_block->free, next_block->free_prev, next_block->free_next);
    }
    else
    {
        //update head if needed
        if (current_worst_fit == free_mem_head)
        {
            free_mem_head = current_worst_fit->free_next;
        }
    }
    current_worst_fit->free_next = NULL;
    current_worst_fit->free_prev = NULL;
    //update current worst fit
    if (create_newest && current_worst_fit_)
    {
        block *newest = (void *)current_worst_fit + current_worst_fit->chunck_size + BLOCK_SIZE;
        if (newest->chunck_size > current_worst_fit_->chunck_size)
            current_worst_fit = newest;
        else
        {
            current_worst_fit = current_worst_fit_;
            current_worst_fit_ = newest;
        }
    }
    else if (!current_worst_fit_)
    {
        current_worst_fit = find_worst_fit();
    }
    else
    {
        current_worst_fit = current_worst_fit_;
        current_worst_fit_ = find_worst_fit();
    }
    if (DEBUG)
        printf("New worst fit is %p\n", current_worst_fit);
    if (DEBUG && current_worst_fit == NULL)
        printf("New worst fit is NULL\n");
    return usr_address;
}

int Mem_Free(void *ptr, int coalesce)
{
    if (ptr == NULL)
    {
        m_error = E_BAD_POINTER;
        return -1;
    }

    if (DEBUG)
        printf("pointer is %p\n", ptr);
    block *block_ = ptr - BLOCK_SIZE;
    if (DEBUG)
        printf("pointer - 32 bytes  is %p\n", block_);

    uint32_t canary = block_->canary;
    if (canary != (uint32_t)MAGIC_NUMBER)
    {
        if (DEBUG)
            printf("canary value is %d\n", canary);
        m_error = E_BAD_POINTER;
        if (DEBUG)
            printf("Free: invalid canary\n");
        return -1;
    }
    if (block_->free == (uint16_t)1)
    {
        m_error = E_BAD_POINTER;
        return -1;
    }
    block_->free = 1;
    //update merge next
    block *next_block = (void *)block_ + block_->chunck_size + BLOCK_SIZE;
    if (next_block->canary == MAGIC_NUMBER)
    {
        if (DEBUG)
            printf("no coalesce next found!\n");
        next_block->merge = (uint16_t)1;
    }
    //add to free list
    insert_free_list(block_);
    switch (coalesce)
    {
    case NO_COALESCE:
        if (current_worst_fit_ == NULL || block_->chunck_size > current_worst_fit_->chunck_size)
        {
            current_worst_fit_ = block_;
            if (current_worst_fit_->chunck_size > current_worst_fit->chunck_size)
            {
                block *temp = current_worst_fit;
                current_worst_fit = current_worst_fit_;
                current_worst_fit_ = temp;
            }
        }
        break;
    case LOCAL_COALESCE:
        local_coalesce(block_);
        break;
    case GLOBAL_COALESCE:
        global_coalesce();
        break;
    }
    return 0;
}

void Mem_Dump()
{
    block *current = free_mem_head;
    while (current != NULL)
    {
        assert(current->free == 1);
        printf("free pointer %p footer %d size %d merge %d free %d prev %p next %p magic number %d\n", current, current->footer, current->chunck_size, current->merge, current->free, current->free_prev, current->free_next, current->canary);
        current = current->free_next;
    }
}

block *find_worst_fit()
{
    block *target = NULL;
    uint32_t max_size = 0;
    block *current = free_mem_head;
    while (current != NULL)
    {
        assert(current->free == 1);
        if (MATH_MAX(current->chunck_size, max_size) == current->chunck_size)
        {
            max_size = current->chunck_size;
            target = current;
        }

        current = current->free_next;
    }
    return target;
}

void insert_free_list(block *ptr)
{
    block *prev_block;
    block *current = ptr;
    bool prev_found = false;
    /* pointer is memory head */
    if (ptr->footer == 0)
    {
        if (free_mem_head == NULL)
        {
            free_mem_head = ptr;
        }
        else
        {
            free_mem_head->free_prev = ptr;
            ptr->free_next = free_mem_head;
            free_mem_head = ptr;
        }
        return;
    }

    /* middle pointer */
    while (!prev_found)
    {
        prev_block = ((block *)((void *)current - current->footer - BLOCK_SIZE));
        if (prev_block == NULL)
            break;
        if (DEBUG)
            printf("insert prev block updated to footer %d size %d merge %d free %d prev %p next %p\n", prev_block->footer, prev_block->chunck_size, prev_block->merge, prev_block->free, prev_block->free_prev, prev_block->free_next);

        if (prev_block->canary != MAGIC_NUMBER)
        {
            break;
        }
        if (((block *)prev_block)->free)
        {
            prev_found = true;
            break;
        }
        current = prev_block;
    }
    if (prev_found)
    {
        block *temp = ((block *)prev_block)->free_next;
        ((block *)prev_block)->free_next = ptr;
        ptr->free_prev = prev_block;
        if (temp != NULL)
        {
            temp->free_prev = ptr;
            ptr->free_next = temp;
        }
    }
    else
    {
        //head is either NEXT or NULL
        if (free_mem_head == NULL)
        {
            free_mem_head = ptr;
        }
        else
        {
            free_mem_head->free_prev = ptr;
            ptr->free_next = free_mem_head;
            free_mem_head = ptr;
        }
    }
}

void local_coalesce(block *ptr)
{
    block *final_next = ptr->free_next;
    int final_size = 0;
    int counter = 0;
    block *prev = NULL;
    block *current = ptr;
    while (current != NULL && current->canary == MAGIC_NUMBER)
    {
        final_size += current->chunck_size;
        counter++;
        prev = current;
        if (current->footer != 0)
            current = ((block *)((void *)current - current->footer - BLOCK_SIZE));
        else
            current = NULL;
    }
    if (prev == ptr)
        return;
    prev->free_next = final_next;
    prev->chunck_size = final_size + (counter - 1) * BLOCK_SIZE;
    if (current_worst_fit_ == NULL || prev->chunck_size > current_worst_fit_->chunck_size)
    {
        current_worst_fit_ = prev;
        if (current_worst_fit_->chunck_size > current_worst_fit->chunck_size)
        {
            block *temp = current_worst_fit;
            current_worst_fit = current_worst_fit_;
            current_worst_fit_ = temp;
        }
    }
    block *final_neighbor = ((block *)((void *)prev + prev->chunck_size + BLOCK_SIZE));
    if (final_neighbor != NULL && final_neighbor->canary == MAGIC_NUMBER)
    {
        final_neighbor->footer = prev->chunck_size;
    }
}

void global_coalesce()
{
    block *start = free_mem_head;
    block *current;
    block *farthest_prev;
    while (start != NULL && start->canary == MAGIC_NUMBER && start->free == 1)
    {
        current = start;
        farthest_prev = current;
        while (current != NULL && current->canary == MAGIC_NUMBER && current->free == 1)
        {
            farthest_prev = current;
            current = (block *)((void *)current + current->chunck_size + BLOCK_SIZE);
        }
        if (DEBUG)
            printf("Calling local coalesce on %p\n", farthest_prev);
        local_coalesce(farthest_prev);
        start = farthest_prev->free_next;
    }
}