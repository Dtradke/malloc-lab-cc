/*
 * mm-implicit.c -  Simple allocator based on implicit free lists,
 *                  first fit placement, and boundary tag coalescing.
 *
 * Each block has header and footer of the form:
 *
 *      31                     3  2  1  0
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      -----------------------------------
 *
 * where s are the meaningful size bits and a/f is set
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                          end
 * heap                                                           heap
 *  -----------------------------------------------------------------
 * |  pad   | hdr(8:a) | ftr(8:a) | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *          |       prologue      |                       | epilogue |
 *          |         block       |                       | block    |
 *
 * The allocated prologue and epilogue blocks are overhead that
 * eliminate edge conditions during coalescing.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "mm.h"
#include "memlib.h"

/* Team structure */
team_t team = {
    "TBay Bombers",
    "David Radke", "david.radke@coloradocollege.edu",
    "John Doe", "john.doe@coloradocollege.edu"
};


/* Basic constants and macros */
#define WSIZE       4       /* word size (bytes) */
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE   32      /* initial heap size (bytes) */
#define OVERHEAD    32      /* overhead of header and footer (bytes) */


#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define NEXT_FREE(bp) (*(void **)(bp + (2*WSIZE)))    //computes address for next free block...linked list!!
#define PREV_FREE(bp) (*(void **)(bp))            //computes address for previous free block...linked list!!

#define ALIGN(size) ((size + 7) & ~0x7) //rounds to nearest multiple of 8

/* Global variables */
static char *heap_listp;  /* pointer to first block */
static char *free_listp;  //pointer to the start of the freelist

/* function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void freelist(void *bp);
static void delete_block(void *bp);
static int check_block(void *bp);

/*
 * mm_init - Initialize the memory manager
 */
int mm_init(void)
{
    /* create the initial empty heap */
    if( ( heap_listp = mem_sbrk( 2*OVERHEAD ) ) == NULL )
        return -1;
    PUT( heap_listp, 0 );                        /* alignment padding */
    PUT( heap_listp+WSIZE, PACK( OVERHEAD, 1 ) );  /* prologue header */
    PUT( heap_listp + DSIZE + WSIZE, 0);    //next pointer
    PUT( heap_listp + DSIZE, 0);            //previous pointer
    PUT( heap_listp+DSIZE, PACK( OVERHEAD, 1 ) );  /* prologue footer */
    PUT( heap_listp+WSIZE+DSIZE, PACK( 0, 1 ) );   /* epilogue header */
    free_listp = heap_listp + DSIZE; //initializes free list pointer as heap_listp plus double word size

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if( extend_heap( CHUNKSIZE/WSIZE ) == NULL )
        return -1;
    return 0;

}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if( size <= 0 )
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if( size <= DSIZE )
        asize = DSIZE + OVERHEAD;
    else
        asize = DSIZE * ( ( size + (OVERHEAD) + ( DSIZE-1 ) ) / DSIZE );

    /* Search the free list for a fit */
    if( ( bp = find_fit( asize ) ) != NULL ) {
        place( bp, asize );
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX( asize, CHUNKSIZE );
    if( ( bp = extend_heap( extendsize/WSIZE ) ) == NULL )
        return NULL;
    place( bp, asize );
    return bp;
}

/*
 * mm_free - Free a block
 */
void mm_free(void *bp)
{
    if(!bp)
      return;

    size_t size = GET_SIZE( HDRP( bp ) );

    PUT( HDRP( bp ), PACK( size, 0 ) );
    PUT( FTRP( bp ), PACK( size, 0 ) );
    coalesce( bp );
}

/*
 * mm_realloc - naive implementation of mm_realloc
 */
void *mm_realloc(void *ptr, size_t size)
{

    void *newp;
    size_t copySize;


    if( ( newp = mm_malloc( size ) ) == NULL ) {
        printf( "ERROR: mm_malloc failed in mm_realloc\n" );
        exit( 1 );
    }
    copySize = GET_SIZE( HDRP( ptr ) );
    if( size < copySize )
        copySize = size;
    memcpy( newp, ptr, copySize );
    mm_free( ptr );
    return newp;
}

/*
 * mm_checkheap - Check the heap for consistency
 */
int mm_checkheap(void)
{
    void *bp = heap_listp;
    printf("Heap (%p): \n", heap_listp);//prints address of heap

    if((GET_SIZE(HDRP(heap_listp)) != OVERHEAD) || !GET_ALLOC(HDRP(heap_listp)))//If first block header size wrong
    {
        printf("Bad prologue header\n");
        return 0;
    }
    if(check_block(heap_listp) == 0)//check block
        return 0;

    for(bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp))//goes through all of blocks in free list
    {
         if(check_block(bp) == 0)//if block is not good
                return 0;
    }
    return 1;//block is good
}

/* The remaining routines are internal helper routines */

/*
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap( size_t words )
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = ( words % 2 ) ? ( words+1 ) * WSIZE : words * WSIZE;

    if( ( bp = mem_sbrk( size ) ) == (void *)-1 )
       return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT( HDRP( bp ), PACK( size, 0 ) );         /* free block header */
    PUT( FTRP( bp ), PACK( size, 0 ) );         /* free block footer */
    PUT( HDRP( NEXT_BLKP( bp ) ), PACK( 0, 1 ) ); /* new epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce( bp );
}

/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE( HDRP( bp ) ); //get size of free block


    if( ( csize - asize ) >= OVERHEAD ) { //if total size minus requested size is bigger than OVERHEAD, split it
        PUT( HDRP( bp ), PACK( asize, 1 ) );
        PUT( FTRP( bp ), PACK( asize, 1 ) );
        delete_block(bp);//remove block from free list
        bp = NEXT_BLKP( bp );
        PUT( HDRP( bp ), PACK( csize-asize, 0 ) );
        PUT( FTRP( bp ), PACK( csize-asize, 0 ) );
        coalesce(bp);
    }
    else {//if space is not big enough anyways... dont split
        PUT( HDRP( bp ), PACK( csize, 1 ) );
        PUT( FTRP( bp ), PACK( csize, 1 ) );
        delete_block(bp);//remove block from free list
    }
}

/*
 * find_fit - Find a fit for a block with asize bytes
 */
static void *find_fit(size_t asize)
{
    /* first fit search */
    void *bp;


    for( bp = free_listp; GET_ALLOC( HDRP( bp ) ) == 0; bp = NEXT_FREE( bp ) ) {//goes through the whole list
        if( asize <= GET_SIZE( HDRP( bp ) ) )  {//if the free block is big enough, return the pointer
            return bp;
        }
    }
    return NULL; /* no fit */
}

/*
 * coalesce - boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC( FTRP( PREV_BLKP( bp ) ) ) || PREV_BLKP(bp) == bp;
    size_t next_alloc = GET_ALLOC( HDRP( NEXT_BLKP( bp ) ) );
    size_t size = GET_SIZE( HDRP( bp ) );

    if( prev_alloc && next_alloc ) {            /* Case 1 */
        freelist(bp);
        return bp;
    }
    else if( prev_alloc && !next_alloc ) {      // Case 2: block next to current block is free
        size += GET_SIZE( HDRP( NEXT_BLKP( bp ) ) );
        delete_block(NEXT_BLKP(bp));//remove next block from free list
        PUT( HDRP( bp ), PACK( size, 0 ) );
        PUT( FTRP( bp ), PACK( size, 0 ) );
    }


    else if( !prev_alloc && next_alloc ) {      // Case 3: block before the current block is free
        size += GET_SIZE( HDRP( PREV_BLKP( bp ) ) );
        bp = PREV_BLKP(bp);
        delete_block(bp);//remove previous block from free list
        PUT( HDRP(bp), PACK(size, 0));
        PUT( FTRP( bp ), PACK( size, 0 ) );
    }


    else if(!prev_alloc && !next_alloc) {       // Case 4
        size += GET_SIZE( HDRP( PREV_BLKP( bp ) ) ) +
            GET_SIZE( HDRP( NEXT_BLKP( bp ) ) );
        delete_block(PREV_BLKP(bp));//remove previous block from free list
        delete_block(NEXT_BLKP(bp));//remove next block from free list
        bp = PREV_BLKP(bp);
        PUT( HDRP( bp ), PACK( size, 0 ) );
        PUT( FTRP( bp ), PACK( size, 0 ) );
    }

    freelist(bp);//adds block to the freelist

    return bp;
}

static void freelist(void *bp)
{
  // This function is to insert into the front of the freelist and update the info required for a linked list
  NEXT_FREE(bp) = free_listp; //sets next to start of the free list
  PREV_FREE(free_listp) = bp; //sets current previous printer to the added block
  PREV_FREE(bp) = NULL;//old free pointer set to null
  free_listp = bp;//sets start of the new free list to the block just added so that block is the first one in the list
}

static void delete_block(void *bp)//takes a block out of the free list
{
  if(PREV_FREE(bp) != NULL)//if previous block
  {
    NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);//skips and sets pointer of the previous to the next block
  }
  else
  {
    free_listp = NEXT_FREE(bp);//if there is no previous, sets the free list pointer to point at the next block
  }
  PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);//previous block pointer to the next, set to the previous

}

static int check_block(void *bp){
    if(NEXT_FREE(bp) < mem_heap_lo() || NEXT_FREE(bp) > mem_heap_hi())//If next free pointer is out of the range of the memory
        return 0;

    if(PREV_FREE(bp) < mem_heap_lo() || PREV_FREE(bp) > mem_heap_hi())//If previous free pointer is out of the range of memory
        return 0;

    if((size_t)bp % 8)//If no alignment is done
        return 0;

    if(GET(HDRP(bp)) != GET(FTRP(bp)))//If header and footer do not match
        return 0;

    return 1;//else, block is good
}
