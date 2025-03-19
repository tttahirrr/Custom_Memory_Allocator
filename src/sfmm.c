/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"


// returns the minimum index (in sf_free_list_heads) a free block of block_size can be found. from lists 0 to 7
int get_index_first_free_list(size_t block_size)
{
   size_t M = 32; // minimum block size


   if (block_size == M)
       return 0;
   else if (block_size == 2 * M)
       return 1;
   else if (block_size == 3 * M)
       return 2;
   else if (block_size > 3 * M && block_size <= 5 * M)
       return 3;
   else if (block_size > 5 * M && block_size <= 8 * M)
       return 4;
   else if (block_size > 8 * M && block_size <= 13 * M)
       return 5;
   else if (block_size > 13 * M && block_size <= 21 * M)
       return 6;
   else
       return 7;
}

// searches the free lists starting from start_index to list 7, for a non-empty free list. removes the found free block from the list
void *find_free_block(int start_index)
{
   for (int i = start_index; i <= 7; i++)
   {
       sf_block *sentinel = &sf_free_list_heads[i];   // the sentinel node for the current list
       sf_block *current = sentinel->body.links.next; // the first block in the list

       // if the list is non-empty
       if (current != sentinel)
       {
           // remove the current block from the free list
           current->body.links.prev->body.links.next = current->body.links.next;
           current->body.links.next->body.links.prev = current->body.links.prev;

           // now the current block is isolated from the free list
           return current; // return the first block found in this free list
       }
   }

   // no suitable block found in lists start_index to 7
   return NULL;
}

void *check_wilderness_block(size_t block_size)
{
   sf_block *sentinel = &sf_free_list_heads[8];            // sentinel node for the wilderness block
   sf_block *wilderness_block = sentinel->body.links.next; // it should be next to the sentinel


   // check if the wilderness block exists (list 8 is not empty)
   if (sentinel != wilderness_block)
   {
       // get the size of the wilderness by zero-ing out the last 5 bits of the header
       size_t wilderness_block_size = ((wilderness_block->header >> 5) << 5);

       // if it's big enough to allocate block_size, proceed
       if (wilderness_block_size >= block_size)
       {
           // remove the wilderness block from the free list (list 8)
           wilderness_block->body.links.prev->body.links.next = wilderness_block->body.links.next;
           wilderness_block->body.links.next->body.links.prev = wilderness_block->body.links.prev;

           // let's see if it needs to be split
           size_t remainder_size = wilderness_block_size - block_size;

           if (remainder_size >= 32)
           {
               // split the wilderness block. allocate the requested size & create a new free block for the remainder
               wilderness_block->header = block_size | 0x10; // Mark allocated (alloc bit = 1)

               // create a new block for the remainder
               sf_block *new_block = (sf_block *)((char *)wilderness_block + block_size);

               // set the header & footer for the remainder (free block)
               new_block->header = remainder_size | 0x0; // mark as free

               // set the prev_alloc bit of the remainder block, as the allocated block comes before it
               new_block->header |= 0x8; // set prev_alloc bit (bit 3) to 1

               sf_footer *new_footer = (sf_footer *)((char *)new_block + remainder_size - sizeof(sf_footer));
               *new_footer = new_block->header; // Footer matches header

               // insert the remainder back into list 8 (wilderness list)
               sf_block *wilderness_sentinel = &sf_free_list_heads[8];
               new_block->body.links.next = wilderness_sentinel->body.links.next;
               new_block->body.links.prev = wilderness_sentinel;
               wilderness_sentinel->body.links.next->body.links.prev = new_block;
               wilderness_sentinel->body.links.next = new_block;

               // return the address of the allocated block (not the payload)
               wilderness_block->header |= 0x8;
               return wilderness_block;
           }
           else
           {
               // allocate the entire wilderness block
               wilderness_block->header = wilderness_block_size | 0x10; // mark as allocated

               // return the address of the allocated block (not the payload)
               return wilderness_block;
           }
       }
   }
   // if we reached here, there's no wilderness block or it was too small
   return NULL;
}

void insert_into_freelist(sf_block *block)
{
   size_t block_size = ((block->header) >> 5) << 5; // zero out the 5 LSB's to get the current block size

   // find the appropriate free list index for this block size
   int index = get_index_first_free_list(block_size);

   // get the sentinel for the free list at this index
   sf_block *sentinel = &sf_free_list_heads[index];

   // insert block at the front of the list (LIFO)
   block->body.links.next = sentinel->body.links.next; // new block points to the first block
   block->body.links.prev = sentinel;                  // new block's prev points to the sentinel

   // update the first block's prev to point to the new block
   sentinel->body.links.next->body.links.prev = block;

   // update the sentinel to point to the new block
   sentinel->body.links.next = block;
}

void allocate_block(sf_block *block, size_t requested_size)
{
   size_t block_size = ((block->header) >> 5) << 5; // zero out the 5 LSB's to get the current block size

   // calculate the remainder size after allocation
   size_t remainder_size = block_size - requested_size;

   // if the block can be split, split the block: use the lower part & return the upper part to the free list
   if (remainder_size >= 32)
   {
       // mark the lower part as allocated
       block->header = requested_size | 0x10; // mark allocated bit (bit 4) as 1

       // create a new block for the remainder
       sf_block *new_block = (sf_block *)((char *)block + requested_size);

       // set the header & footer for the remainder (free block)
       new_block->header = remainder_size | 0x0; // mark as free


       // set the prev_alloc bit of the remainder block, as the allocated block comes before it
       new_block->header |= 0x8; // set prev_alloc bit (bit 3) to 1


       sf_footer *new_footer = (sf_footer *)((char *)new_block + remainder_size - sizeof(sf_footer));
       *new_footer = new_block->header; // footer matches header

       // insert the remainder into the appropriate free list (helper function)
       insert_into_freelist(new_block);
   }
   else
   {                                      // if the block can't be split, allocate the entire block
       block->header = block_size | 0x10; // mark the entire block as allocated

       // if the next block exists, update its prev_alloc bit
       sf_block *next_block = (sf_block *)((char *)block + block_size);
       if ((char *)next_block < (char *)sf_mem_end())
       {
           next_block->header |= 0x8; // set the prev_alloc bit (bit 3) of the next block to 1
       }
   }
}

void initialize_free_list_heads()
{
   for (int i = 0; i < NUM_FREE_LISTS; i++)
   {
       sf_block *sentinel = &sf_free_list_heads[i]; // get the sentinel node for this free list
       sentinel->body.links.next = sentinel;        // sentinel points to itself
       sentinel->body.links.prev = sentinel;        // sentinel points to itself
   }
}

void mem_init(void)
{


   // initialize the free list heads (sentinel nodes)
   initialize_free_list_heads();


   // allocate the heap space (e.g., 2048 bytes from sf_mem_grow)
   if (sf_mem_grow() == NULL)
   {
       sf_errno = ENOMEM;
       return;
   }


   // get the starting heap address (we assume it's aligned to 16 bytes)
   char *heap_start = sf_mem_start();
   size_t heap_start_addr = (size_t)heap_start;


   // calculate the initial offset for alignment
   size_t offset = 0;


   if (heap_start_addr % 32 == 16)
   {
       // add 1 row (8 bytes) if heap_start is only 16-byte aligned
       offset = 8;
   }
   else if (heap_start_addr % 32 == 0)
   {
       // add 3 rows (24 bytes) if heap_start is 32-byte aligned
       offset = 24;
   }


   // apply the offset to the heap start
   char *aligned_heap_start = heap_start + offset;


   // create the prologue block
   sf_block *prologue = (sf_block *)aligned_heap_start;
   prologue->header = (32 | 0x10); // size 32 bytes, alloc bit set
   // 24 bytes unused payload in prologue block


   // create the wilderness block
   sf_block *wilderness = (sf_block *)(aligned_heap_start + 32); // wilderness initially follows the prologue
   size_t wilderness_size = 1984;                                // size excluding footer, epilogue, & unused rows
   wilderness->header = wilderness_size | 0x0;                   // set wilderness block header size & alloc free
   sf_footer *wilderness_footer = (sf_footer *)((char *)wilderness + wilderness_size - sizeof(sf_footer));
   *wilderness_footer = wilderness->header;                      // copy its header to its footer


   // create the epilogue block
   sf_block *epilogue;
   if (offset == 8)
   {
       // heap was 16 byte aligned, epilogue block is placed at sf_mem_end() - 8 - 16
       epilogue = (sf_block *)(sf_mem_end() - 8 - 16);
   }
   else if (offset == 24)
   {
       // heap was 32 byte aligned, epilogue block is placed at sf_mem_end() - 8
       epilogue = (sf_block *)(sf_mem_end() - 8);
   }
   epilogue->header = 0x10; // epilogue block with size 0 & alloc bit set


   // insert the wilderness block into the free list (list 8)
   sf_block *wilderness_sentinel = &sf_free_list_heads[8];
   wilderness->body.links.next = wilderness_sentinel->body.links.next;
   wilderness->body.links.prev = wilderness_sentinel;
   wilderness_sentinel->body.links.next->body.links.prev = wilderness;
   wilderness_sentinel->body.links.next = wilderness;
}

void remove_from_free_list(sf_block *block)
{
   if (block->body.links.prev != NULL && block->body.links.next != NULL) {
       // update the next pointer of the previous block to point to the next block
       block->body.links.prev->body.links.next = block->body.links.next;

       // update the prev pointer of the next block to point to the previous block
       block->body.links.next->body.links.prev = block->body.links.prev;
   }
}

void allocate_wilderness_block(sf_block *block, size_t requested_size)
{
   size_t block_size = ((block->header) >> 5) << 5; // zero out the 5 LSB's to get the current block size

   remove_from_free_list(block);
   // calculate the remainder size after allocation
   size_t remainder_size = block_size - requested_size;

   // if the block can be split, split the block. use the lower part & return the upper part to list 8
   if (remainder_size >= 32)
   {
       // mark the lower part as allocated
       block->header = requested_size | 0x10; // mark allocated bit (bit 4) as 1


       // create a new block for the remainder
       sf_block *new_block = (sf_block *)((char *)block + requested_size);


       // set the header & footer for the remainder (free block)
       new_block->header = remainder_size | 0x0; // mark as free


       // set the prev_alloc bit of the remainder block, as the allocated block comes before it
       new_block->header |= 0x8; // set prev_alloc bit (bit 3) to 1


       sf_footer *new_footer = (sf_footer *)((char *)new_block + remainder_size - sizeof(sf_footer));
       *new_footer = new_block->header; // footer matches header


       // insert the remainder block into list 8 (wilderness list)
       sf_block *wilderness_sentinel = &sf_free_list_heads[8];
       new_block->body.links.next = wilderness_sentinel->body.links.next;
       new_block->body.links.prev = wilderness_sentinel;
       wilderness_sentinel->body.links.next->body.links.prev = new_block;
       wilderness_sentinel->body.links.next = new_block;
   }
   else
   {
       // if the block can't be split, allocate the entire block
       block->header = block_size | 0x10; // mark the entire block as allocated (alloc bit = 1)
   }
}

void update_prev_alloc_bits()
{
    // determine the initial offset based on the alignment of sf_mem_start()
    size_t offset = ((size_t)sf_mem_start() % 32 == 16) ? 8 : 24;

    // start at the first block after the prologue, accounting for the offset
    sf_block *current = (sf_block *)((char *)sf_mem_start() + offset + 32);

    // continue until reaching the epilogue (identified by a block size of zero)
    while ((current->header & ~0x1F) != 0) {
        // check if the current block is allocated
        unsigned int alloc_bit = current->header & 0x10;  // check if current block is allocated

        // move to the next block using the size of the current block
        size_t block_size = current->header & ~0x1F;  // get the size of the current block
        sf_block *next_block = (sf_block *)((char *)current + block_size);  // move to the next block

        if (alloc_bit) {
            // if the current block is allocated, set the prev_alloc bit of the next block
            next_block->header |= 0x8;
        } else {
            // if the current block is free, clear the prev_alloc bit of the next block
            next_block->header &= ~0x8;

            // update the footer of the current block to match the header (if it’s free)
            sf_footer *footer = (sf_footer *)((char *)current + block_size - sizeof(sf_footer));
            *footer = current->header;  // set footer to match the header
        }

        // move to the next block
        current = next_block;
    }
}

void *sf_malloc(size_t size)
{
   // check if the size is 0
   if (size == 0)
       return NULL;

   // check if this is the first call to malloc, meaning we must initialize the heap
   if (sf_mem_start() == sf_mem_end())
       mem_init();

   size_t block_size = size + sizeof(sf_header); // add space for the header (size + 8)

   // round up the (requested size + header size) to a multiple of 32
   if (block_size % 32 != 0)
       block_size += (32 - (block_size % 32));

   // get the index of the first free list that can hold blocks of this size
   int index = get_index_first_free_list(block_size);

   // find a free block in the segregated free lists
   void *free_block = find_free_block(index);

   // if it's not NULL, then we found a suitable block. so we allocate it & split it & insert the remainder in the appropriate list
   if (free_block != NULL)
   {
       // if a suitable block is found, allocate it (split if necessary)
       // if the block was found, then find_free_block already removed it from the list.
       // allocate_block just has to allocate & add the remainder to the appropriate list
       allocate_block(free_block, block_size);
       update_prev_alloc_bits();
       return (void *)((char *)free_block + sizeof(sf_header)); // return pointer to payload
   }

   // if no suitable block is found in the lists from 0 to 7, check the wilderness block (list 8)
   if (free_block == NULL)
       free_block = check_wilderness_block(block_size);
   update_prev_alloc_bits();

   // if it's found in the wilderness
   if (free_block != NULL)
       return (void *)((char *)free_block + sizeof(sf_header)); // return pointer to payload

   // if we reached this point, no suitable block was found in lists 0 to 8.
   // grow the heap & retry in a loop
   while (1)
   {

       // grow the heap (add a new page)
       if (sf_mem_grow() == NULL) {
           sf_errno = ENOMEM;  // if heap growth fails, set error & return NULL
           return NULL;
       }

       // find the old wilderness block in list 8
       sf_block *wilderness_sentinel = &sf_free_list_heads[8];
       sf_block *wilderness_block = wilderness_sentinel->body.links.next;

       // remove the old wilderness block from list 8 (if it exists)
       if (wilderness_block != wilderness_sentinel) {
           wilderness_block->body.links.prev->body.links.next = wilderness_block->body.links.next;
           wilderness_block->body.links.next->body.links.prev = wilderness_block->body.links.prev;
       }

       // create the new wilderness block
       size_t wilderness_size = 2048;  // assuming one page size is 2048 bytes
       size_t offset = (size_t)sf_mem_start() % 32 == 16 ? 16 : 0; // offset for alignment
       sf_block *new_wilderness_block = (sf_block *)((char *)sf_mem_end() - 8 - offset - wilderness_size);
       new_wilderness_block->header = wilderness_size | 0x0;  // free block, size 2048
       sf_footer *new_wilderness_footer = (sf_footer *)((char *)new_wilderness_block + wilderness_size - sizeof(sf_footer));
       *new_wilderness_footer = new_wilderness_block->header;  // set the footer

       // create the new epilogue block at the end of the extended heap
       sf_block *epilogue = (sf_block *)(sf_mem_end() - 8 - offset);
       epilogue->header = 0x10;  // epilogue block with size 0 & alloc bit set

       // coalescing of old & new wilderness blocks
       if (wilderness_block != wilderness_sentinel) {
           // get the size of the old wilderness block
           size_t old_wilderness_size = wilderness_block->header & ~0x1F;

           // combine the sizes of the old & new wilderness blocks
           size_t total_wilderness_size = old_wilderness_size + wilderness_size;

           // update the header of the old wilderness block to reflect the new size
           wilderness_block->header = total_wilderness_size | 0x0;  // mark as free

           // set the footer at the end of the new wilderness block
           sf_footer *footer = (sf_footer *)((char *)new_wilderness_block + wilderness_size - sizeof(sf_footer));
           *footer = wilderness_block->header;  // footer matches header of the coalesced block

           // now, the coalesced block is the old wilderness block
           new_wilderness_block = wilderness_block;
       }

       // insert the new/coalesced wilderness block into list 8
       new_wilderness_block->body.links.next = wilderness_sentinel->body.links.next;
       new_wilderness_block->body.links.prev = wilderness_sentinel;
       wilderness_sentinel->body.links.next->body.links.prev = new_wilderness_block;
       wilderness_sentinel->body.links.next = new_wilderness_block;

       // retry the allocation in the new/coalesced wilderness block
       if ((new_wilderness_block->header & ~0x1F) >= block_size) {
           allocate_wilderness_block(new_wilderness_block, block_size);
           update_prev_alloc_bits();
           return (void *)((char *)new_wilderness_block + sizeof(sf_header));  // return pointer to payload
       }
   }
   return NULL;
}

// Adapted from Chapter 9 of Computer Systems A Programmer's Perspective, Randal E. Bryant, David R. O'Hallaron
sf_block *coalesce(sf_block *bp) {
   size_t prev_alloc = bp->header & 0x8;  // check prev_alloc bit
   size_t next_alloc = ((sf_block *)((char *)bp + (bp->header & ~0x1F)))->header & 0x10;  // check next block's alloc bit
   size_t size = bp->header & ~0x1F;  // get size of current block

   // case 1: no coalescing needed
   if (prev_alloc && next_alloc) {
       return bp;
   }

   // case 2: coalesce with next block
   else if (prev_alloc && !next_alloc) {
       sf_block *next_block = (sf_block *)((char *)bp + size);

       // remove next block from the free list since it will be merged
       remove_from_free_list(next_block);

       // add the sizes of current block & next block
       size += next_block->header & ~0x1F;
       bp->header = size | 0x0;  // Mark the block as free

       // set the footer for the new coalesced block
       sf_footer *footer = (sf_footer *)((char *)bp + size - sizeof(sf_footer));
       *footer = bp->header;  // footer matches header
   }

   // case 3: coalesce with previous block
   else if (!prev_alloc && next_alloc) {
       // find the previous block using its footer (because prev_alloc == 0)
       sf_footer *prev_footer = (sf_footer *)((char *)bp - sizeof(sf_footer));
       size_t prev_block_size = *prev_footer & ~0x1F;
       sf_block *prev_block = (sf_block *)((char *)bp - prev_block_size);

       // remove previous block from the free list
       remove_from_free_list(prev_block);

       // add the sizes of current block and previous block
       size += prev_block_size;
       prev_block->header = size | 0x0;  // mark the coalesced block as free

       // set the footer for the new coalesced block
       sf_footer *footer = (sf_footer *)((char *)prev_block + size - sizeof(sf_footer));
       *footer = prev_block->header;  // footer matches header

       // update bp to point to the start of the coalesced block
       bp = prev_block;
   }

   // case 4: coalesce with both previous and next blocks
   else {
       // find the previous block using its footer
       sf_footer *prev_footer = (sf_footer *)((char *)bp - sizeof(sf_footer));
       size_t prev_block_size = *prev_footer & ~0x1F;
       sf_block *prev_block = (sf_block *)((char *)bp - prev_block_size);

       // find the next block using its header
       sf_block *next_block = (sf_block *)((char *)bp + size);

       // remove both previous and next blocks from the free list
       remove_from_free_list(prev_block);
       remove_from_free_list(next_block);

       // add the sizes of current block, previous block, and next block
       size += prev_block_size + (next_block->header & ~0x1F);
       prev_block->header = size | 0x0;  // mark the coalesced block as free

       // set the footer for the new coalesced block
       sf_footer *footer = (sf_footer *)((char *)next_block + (next_block->header & ~0x1F) - sizeof(sf_footer));
       *footer = prev_block->header;  // footer matches header

       // update bp to point to the start of the coalesced block
       bp = prev_block;
   }

   return bp;
}

void sf_free(void *ptr)
{
   // validate the pointer
   if (ptr == NULL)
       return abort();  // pointer is NULL


   sf_block *block = (void*)ptr - 8;  // get the block from the pointer


   // check if the pointer is 32-byte aligned
   if ((size_t)ptr % 32 != 0)
       return abort();  // pointer is not 32-byte aligned


   // check if the block size is valid
   unsigned block_size = block->header & ~0x1F;  // get the size by masking out the lower 5 bits
   if (block_size < 32 || block_size % 32 != 0)
       return abort();  // invalid block size


   // check if the block is within the heap range
   if ((char *)block < (char *)sf_mem_start() || (char *)block + block_size > (char *)sf_mem_end())
       return abort();  // block is outside the heap range


   // check if the block is marked as allocated
   if (!(block->header & 0x10))
       return abort();  // block is not marked as allocated



   // check the prev_alloc bit for consistency with the previous block
   if (!(block->header & 0x8))
   {
       sf_footer *prev_footer = (sf_footer *)((char *)block - sizeof(sf_footer));

       if (*prev_footer & 0x10)
       {
           return abort();
       }

       size_t prev_block_size = *prev_footer & ~0x1F;

       sf_block *prev_block = (sf_block *)((char *)block - prev_block_size);

       if (prev_block->header & 0x10)
       {
           return abort();
       }
   }



   // mark the block as free & coalesce it
   block->header &= ~0x10;  // unset the allocated bit
   block = coalesce(block);  // coalesce with adjacent blocks, if possible
   update_prev_alloc_bits();


   // insert the coalesced block into the appropriate free list
   insert_into_freelist(block);

}

void *sf_realloc(void *ptr, size_t reqsize)
{
   // validate the input pointer
   if (ptr == NULL)
   {
       sf_errno = EINVAL;
       return NULL;
   }

   // retrieve block from payload pointer
   sf_block *block = (sf_block *)((char *)ptr - sizeof(sf_header));

   // check if pointer is 32 byte aligned
   if ((size_t)ptr % 32 != 0)
   {
       sf_errno = EINVAL;
       return NULL;
   }

   // check if block size is valid
   size_t current_block_size = block->header & ~0x1F;
   if (current_block_size < 32 || current_block_size % 32 != 0)
   {
       sf_errno = EINVAL;
       return NULL;
   }


   // check if block is within the heap range
   if ((char *)block < (char *)sf_mem_start() || (char *)block + current_block_size > (char *)sf_mem_end())
   {
       sf_errno = EINVAL;
       return NULL;
   }


   // check if block is allocated. if it's not, error
   if ((block->header & 0x10) == 0)
   {
       sf_errno = EINVAL;
       return NULL;
   }


   // check the prev_alloc bit for consistency with the previous block
   if (!(block->header & 0x8))
   { // if prev_alloc bit is 0, previous block is free
       // get the previous block's footer
       sf_footer *prev_footer = (sf_footer *)((char *)block - sizeof(sf_footer));

       // check if the alloc bit in the footer is 0 (free block)
       if (*prev_footer & 0x10)
       {
           sf_errno = EINVAL;
           return NULL;
       }

       // get the size of the previous block from the footer
       size_t prev_block_size = *prev_footer & ~0x1F;


       // find the start of the previous block using its size
       sf_block *prev_block = (sf_block *)((char *)block - prev_block_size);


       // check if the header's alloc bit of the previous block is consistent with the prev_alloc bit
       if (prev_block->header & 0x10)
       {
           sf_errno = EINVAL;
           return NULL;
       }
   }


   // edge case. requested size is 0
   if (reqsize == 0)
   {
       sf_free(ptr); // basically asking to free it
       return NULL;
   }

   // time for the realloc logic
   // get the aligned size for the requested new block
   size_t new_block_size = reqsize + sizeof(sf_header);
   if (new_block_size % 32 != 0)
   {
       new_block_size += (32 - (new_block_size % 32)); // round up to nearest multiple of 32
   }

   // case 1: new size is equal to the current size
   if (new_block_size == current_block_size)
   {
       return ptr; // no resizing needed, just return original pointer
   }
   // case 2: requested size is bigger than current payload size
   else if (new_block_size > current_block_size)
   {
       void *new_block = sf_malloc(reqsize);
       if (new_block == NULL)
       {
           sf_errno = ENOMEM;
           return NULL;
       }
       memcpy(new_block, ptr, reqsize);
       sf_free(ptr);
       return new_block;
   }
   // case 3: requested size is smaller than the current block size
   else {
        size_t remainder_size = current_block_size - new_block_size;

        // if remainder big enough, split
        if (remainder_size >= 32) {
            // update the header of the resized block
            block->header = new_block_size | 0x10; // set allocated bit

            // create a new free block for the remainder
            sf_block *remainder_block = (sf_block *)((char *)block + new_block_size);
            remainder_block->header = remainder_size | 0x0; // set remainder as free

            // set the footer for the remainder block to match the header
            sf_footer *remainder_footer = (sf_footer *)((char *)remainder_block + remainder_size - sizeof(sf_footer));
            *remainder_footer = remainder_block->header;

            // insert the remainder block into the free list & coalesce if needed
            insert_into_freelist(remainder_block);
            update_prev_alloc_bits();
            coalesce(remainder_block);
            update_prev_alloc_bits();
        }

        // return pointer to the resized block's payload
        return (char *)block + sizeof(sf_header);
    }

}


void *sf_memalign(size_t size, size_t align) {
    // validate alignment requirements
    if (align < 32 || (align & (align - 1)) != 0) {
        sf_errno = EINVAL;
        return NULL;
    }

    // get the required block size
    size_t alloc_size = size + align + 32 + sizeof(sf_header) + sizeof(sf_footer);

    // allocate a larger block
    sf_block *block = sf_malloc(alloc_size);
    if (block == NULL) {
        sf_errno = ENOMEM;
        return NULL;
    }

    // find the aligned payload address within the allocated block
    char *payload_start = (char *)block + sizeof(sf_header);
    char *aligned_address = (char *)(((uintptr_t)payload_start + (align - 1)) & ~(align - 1));

    // if aligned address is the same as the payload start, we’re done
    if (aligned_address == payload_start) {
        block->header = (alloc_size | 0x10);  // mark block as allocated
        return (void *)aligned_address;
    }

    // otherwise, split the block
    size_t offset = aligned_address - (char *)block;
    if (offset >= 32) {
        // create a new free block with the space before the aligned address
        sf_block *pre_block = block;
        pre_block->header = offset | 0x0;  // set as free with calculated size
        sf_footer *pre_footer = (sf_footer *)((char *)pre_block + offset - sizeof(sf_footer));
        *pre_footer = pre_block->header;

        // insert into free list & coalesce if possible
        update_prev_alloc_bits();
        insert_into_freelist(pre_block);
        coalesce(pre_block);
        update_prev_alloc_bits();
    }

    // set up the aligned block as allocated
    sf_block *aligned_block = (sf_block *)aligned_address;
    aligned_block->header = (size | 0x10);  // set allocated with the requested size

    // set footer for the aligned block
    sf_footer *aligned_footer = (sf_footer *)((char *)aligned_block + size - sizeof(sf_footer));
    *aligned_footer = aligned_block->header;

    // handle the remaining space after the aligned block if there's enough for a free block
    size_t remainder_size = alloc_size - (offset + size);
    if (remainder_size >= 32) {
        sf_block *remainder_block = (sf_block *)((char *)aligned_block + size);
        remainder_block->header = remainder_size | 0x0;  // set as free
        sf_footer *remainder_footer = (sf_footer *)((char *)remainder_block + remainder_size - sizeof(sf_footer));
        *remainder_footer = remainder_block->header;

        // insert the remainder into the free list & coalesce if needed
        update_prev_alloc_bits();
        insert_into_freelist(remainder_block);
        coalesce(remainder_block);
        update_prev_alloc_bits();
    }

    // return the aligned block’s payload
    return (void *)((char *)aligned_block + sizeof(sf_header));
}



