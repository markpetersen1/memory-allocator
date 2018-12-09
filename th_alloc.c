/* -*- mode:c; c-file-style:"k&r"; c-basic-offset: 4; tab-width:4; indent-tabs-mode:nil; mode:auto-fill; fill-column:78; -*- */
/* vim: set ts=4 sw=4 et tw=78 fo=cqt wm=0: */

/* @* Place your name here, and any other comments *@
 * @* that deanonymize your work inside this syntax *@
 *      Mark Petersen
 */

/* Tar Heels Allocator
 * 
 * Simple Hoard-style malloc/free implementation.
 * Not suitable for use for large allocatoins, or 
 * in multi-threaded programs.
 * 
 * to compile/use:
 * $ rm test
 * $ make
 * $ export LD_PRELOAD=/path/to/th_alloc.so ./test
 */

/* Hard-code some system parameters */

#define SUPER_BLOCK_SIZE 4096
#define SUPER_BLOCK_MASK (~(SUPER_BLOCK_SIZE-1))
#define MIN_ALLOC 32 /* Smallest real allocation.  Round smaller mallocs up */
#define MAX_ALLOC 2048 /* Fail if anything bigger is attempted.  
                        * Challenge: handle big allocations */
#define RESERVE_SUPERBLOCK_THRESHOLD 2

#define FREE_POISON 0xab
#define ALLOC_POISON 0xcd

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <sys/mman.h>
 
#define assert(cond) if (!(cond)) __asm__ __volatile__ ("int $3")

/* Object: One return from malloc/input to free. */
struct __attribute__((packed)) object {
    union {
        struct object *next; // For free list (when not in use)
        char * raw; // Actual data
    };
};

/* Super block bookeeping; one per superblock.  "steal" the first
 * object to store this structure
 */
struct __attribute__((packed)) superblock_bookkeeping {
    struct superblock_bookkeeping * next; // next super block
    struct object *free_list;
    // Free count in this superblock
    uint8_t free_count; // Max objects per superblock is 128-1, so a byte is sufficient
    uint8_t level;
};
  
/* Superblock: a chunk of contiguous virtual memory.
 * Subdivide into allocations of same power-of-two size. */
struct __attribute__((packed)) superblock {
    struct superblock_bookkeeping bkeep;
    void *raw;  // Actual data here
};


/* The structure for one pool of superblocks.  
 * One of these per power-of-two */
struct superblock_pool {
    struct superblock_bookkeeping *next;
    uint64_t free_objects; // Total number of free objects across all superblocks
    uint64_t whole_superblocks; // Superblocks with all entries free
};

// 10^5 -- 10^11 == 7 levels
#define LEVELS 7
static struct superblock_pool levels[LEVELS] = {{NULL, 0, 0},
                                                {NULL, 0, 0},
                                                {NULL, 0, 0},
                                                {NULL, 0, 0},
                                                {NULL, 0, 0},
                                                {NULL, 0, 0},
                                                {NULL, 0, 0}};

    /* 
     * size2Level converts allocation size (in bytes) to the the right power of two
     * We essentially want to map a particular size to the correct "level" (array index) in the superblock pool
     * Level 0 corresponds to 32, Level 1 corresponds to 64, etc.
     */
static inline int size2level (ssize_t size) {
    
    if (size <= 32) { return 0; } //2^5 bytes
    else if (size <= 64) { return 1; } //2^6 bytes
    else if (size <= 128) { return 2; } //2^7 bytes
    else if (size <= 256) { return 3; } //2^8 bytes
    else if (size <= 512) { return 4; } //2^9 bytes
    else if (size <= 1024) { return 5; } //2^10 bytes
    else { return 6; } //2^1 bytes
}

    /*
     * alloc_super is a helper function that is called whenever there are no more free objects in a superblock pool
     * this allocates a new superblock so that malloc can have the space to put the new object on
     */
static inline
struct superblock_bookkeeping * alloc_super (int power) {

    void *page;
    struct superblock* sb;
    int free_objects = 0, bytes_per_object = 0;
    char *cursor;
    
    page = mmap(NULL, SUPER_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); //map the superblock size (which is constant) to memory

    sb = (struct superblock*) page;
    // Put this one the list.
    sb->bkeep.next = levels[power].next;
    levels[power].next = &sb->bkeep;
    levels[power].whole_superblocks++;
    sb->bkeep.level = power;
    sb->bkeep.free_list = NULL;

    bytes_per_object = 1 << (5 + power); //use bitwise shifting to map to correct # of bytes
    free_objects = (SUPER_BLOCK_SIZE / bytes_per_object) - 1; //the number of free objects depends on how many bytes each object in this superblock has. Subtract 1 for the initial bookkeeping device
    levels[power].free_objects += free_objects;
    sb->bkeep.free_count = free_objects;

    cursor = (char *) sb;
    // skip the first object
    for (cursor += bytes_per_object; free_objects--; cursor += bytes_per_object) {
        // Place the object on the free list
        struct object* tmp = (struct object *) cursor;
        tmp->next = sb->bkeep.free_list;
        sb->bkeep.free_list = tmp;
    }
    return &sb->bkeep;
}

    /* malloc allocates memory so that objects can be stored in memory. We do this using the Hoard memory allocator
     * We first find which superblock pool we want using the size2level function
     * If there is no more space on any superblocks in this superblock pool, we create a new superblock with alloc_super
     * Finally, we traverse through the superblock pool list and find the first superblock that has available space
     */
void *malloc(size_t size) {
    struct superblock_pool *pool;
    struct superblock_bookkeeping *bkeep;
    void *rv = NULL;
    int power = size2level(size); //find which superblock pool we want
  
    // Check that the allocation isn't too big
    if (size > MAX_ALLOC) {
        errno = -ENOMEM;
        return NULL;
    }

    pool = &levels[power];

    if (!pool->free_objects) {
        bkeep = alloc_super(power);
    } else
        bkeep = pool->next;

    while (bkeep != NULL) { //traverse through our list of superblocks until we find one with space
        if (bkeep->free_count) {
            struct object *cursor = bkeep->free_list;
            int bytes_per_object = 1 << (5 + power);
            int max_objects = (SUPER_BLOCK_SIZE / bytes_per_object) - 1;

            //if superblock has maximum number of objects it can hold, we can decrement because we know we're about to malloc one 
            if (bkeep->free_count == max_objects) { 
                levels[power].whole_superblocks--;
            }

            bkeep->free_list = cursor->next; //cursor no longer has any references to it and we can safely malloc it

            levels[power].free_objects--;
            bkeep->free_count--;
            rv = cursor;

            break;
        }
        bkeep = bkeep->next;
    }

    // assert that rv doesn't end up being NULL at this point
    assert(rv != NULL);

    /* rv is the starting address - we can simply add one because it will then skip over the first 8 bytes and poison 
     * the rest of the memory address. From there, we are poisoning the size of an object in this level using 
     * bitshifting minus the size of the struct object that we want to perserve
     */
    memset(rv + 1, ALLOC_POISON, (1 << (power + 5)) - sizeof(struct object *));
    
    return rv;
}

static inline
struct superblock_bookkeeping * obj2bkeep (void *ptr) {
    uint64_t addr = (uint64_t) ptr;
    addr &= SUPER_BLOCK_MASK;
    return (struct superblock_bookkeeping *) addr;
}

    /* free gives this memory back so that this memory can be used for future calls to malloc
     * Given the pointer to the memory address we want to free, we simply find the superblock pool this corresponds to with obj2bkeep
     * We then know the size of the memory we need to free and place the object back on the free list
     */
void free(void *ptr) {
    struct superblock_bookkeeping *bkeep = obj2bkeep(ptr);

    int power = bkeep->level;
    int bytes_per_object = 1 << (5 + power);
    int max_objects = (SUPER_BLOCK_SIZE / bytes_per_object) - 1;

    //if superblock is one off max number of objects it can hold, we can increment whole superblocks because we know we're about to add that final object back to it
    if (bkeep->free_count == max_objects - 1) { 
        levels[power].whole_superblocks++;
    }

    /* important to free poison the variable before actually putting it back on free list
     * Starting at the pointer, free the size of the objects found in this superblock
     */
    memset(ptr, FREE_POISON, (1 << (power + 5)) - sizeof(struct object *));

    //now we can simply insert into the beginning of the linked list and place this object back at the beginning of our free list
    ((struct object *)ptr)->next = bkeep->free_list;
    bkeep->free_list = ptr;

    levels[power].free_objects++;
    bkeep->free_count++;

    //only keep a total of 2 whole superblocks at a time per superblock pool. If we exceed two, give extras back to the OS
    while (levels[bkeep->level].whole_superblocks > RESERVE_SUPERBLOCK_THRESHOLD) {

        struct superblock_bookkeeping* curr_bkeep = levels[bkeep->level].next; //first superblock of list
        struct superblock_bookkeeping* prev_bkeep = levels[bkeep->level].next; //also set it to first superblock to avoid null pointers
        while (curr_bkeep->free_count != max_objects) { //traverse through list to find the first whole superblock
            prev_bkeep = curr_bkeep;
            curr_bkeep = curr_bkeep->next;
        }
        prev_bkeep->next = curr_bkeep->next;

        if (munmap(curr_bkeep, SUPER_BLOCK_SIZE) != 0) { //curr now represents the exact region we want to unmap and give back to the OS
            errno = -ENOMEM;
        }

        levels[bkeep->level].free_objects -= max_objects; //decrement total free object amount by how many free objects could've been used in that superblock
        levels[bkeep->level].whole_superblocks--;

    }
  
}

// Do NOT touch this - this will catch any attempt to load this into a multi-threaded app
int pthread_create(void __attribute__((unused)) *x, ...) {
    exit(-ENOSYS);
}
