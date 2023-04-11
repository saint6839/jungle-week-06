/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
// @@@@@ explicit @@@@@
#include <sys/mman.h>
#include <errno.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "team 3",
    /* First member's full name */
    "saint6839",
    /* First member's email address */
    "saint6839@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};
#define WSIZE 4 
#define DSIZE 8
#define CHUNKSIZE (1<<12) 
#define MAX(x, y) ((x) > (y)? (x):(y))
#define PACK(size, alloc) ((size) | (alloc))

#define GET(p)     (*(unsigned int *)(p)) 
#define PUT(p,val) (*(unsigned int *)(p) = (val)) 

#define GET_SIZE(p)  ((GET(p) >> 1) << 1)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(ptr) ((char *)(ptr) - WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE) 

#define NEXT_BLKP(ptr) ((char *)(ptr) + GET_SIZE(((char *)(ptr) - WSIZE)))
#define PREV_BLKP(ptr) ((char *)(ptr) - GET_SIZE(((char *)(ptr) - DSIZE)))

#define PRED_FREEP(ptr) (*(void**)(ptr))
#define SUCC_FREEP(ptr) (*(void**)(ptr + WSIZE))

static void *heap_listp = NULL; // heap 시작주소 pointer
static void *free_listp = NULL; // free list head - 가용리스트 시작부분

static void *coalesce(void *ptr);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *ptr, size_t asize);
void remove_block(void *ptr);
void put_free_block(void *ptr);


int mm_init(void)
{   
    heap_listp = mem_sbrk(6*WSIZE);
    if (heap_listp == (void*)-1){
        return -1;
    }
    PUT(heap_listp, 0); //Unused padding
    PUT(heap_listp + WSIZE, PACK(2*DSIZE,1)); // 프롤로그 헤더 16/1
    PUT(heap_listp + 2*WSIZE,NULL); // 프롤로그 PRED 포인터 NULL로 초기화
    PUT(heap_listp + 3*WSIZE,NULL); // 프롤로그 SUCC 포인터 NULL로 초기화
    PUT(heap_listp + 4*WSIZE,PACK(2*DSIZE,1)); // 프롤로그 풋터 16/1
    PUT(heap_listp + 5*WSIZE,PACK(0,1)); // 에필로그 헤더 0/1

    free_listp = heap_listp + DSIZE; 

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}
//연결
static void *coalesce(void *ptr)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr))); 
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));  

    // case 1 - 이전, 다음 가용 블록 없을 경우 맨 밑에서 freelist에 넣어줌
    // case 2 - 이전 블록은 할당 되어있고, 다음 블록만 가용할 경우
    if(prev_alloc && !next_alloc){
        remove_block(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size,0));
        PUT(FTRP(ptr), PACK(size,0));
    }
    // case 3 - 이전 블록만 가용하고, 다음 블록은 할당 되어있을 경우
    else if(!prev_alloc && next_alloc){
        remove_block(PREV_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        ptr = PREV_BLKP(ptr);
        PUT(HDRP(ptr), PACK(size,0));
        PUT(FTRP(ptr), PACK(size,0));

    }
    // case 4 - 이전 블록과 다음 블록 모두 가용할 경우
    else if(!prev_alloc && !next_alloc){
        remove_block(PREV_BLKP(ptr));
        remove_block(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + 
                GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        ptr = PREV_BLKP(ptr); //ptr를 prev로 옮겨줌
        PUT(HDRP(ptr), PACK(size,0));
        PUT(FTRP(ptr), PACK(size,0));
    }
    put_free_block(ptr); // 연결이 된 블록을 free list 에 추가
    return ptr;
}

static void *extend_heap(size_t words)
{
    char *ptr;
    size_t size;
    
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if (((ptr = mem_sbrk(size)) == (void*)-1)) //size를 불러올 수 없으면
        return NULL;
    
    PUT(HDRP(ptr), PACK(size,0)); // Free block header
    PUT(FTRP(ptr), PACK(size,0)); // Free block footer
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0,1)); // New epilogue header

    // Coalesce(연결후 합침)
    return coalesce(ptr);
}

// find_fit함수, first-fit
static void *find_fit(size_t asize){
    void *ptr;
    // 가용리스트 내부의 유일한 할당블록인 프롤로그 블록을 만나면 종료
    for(ptr = free_listp; GET_ALLOC(HDRP(ptr)) == 0; ptr = SUCC_FREEP(ptr)){
        if(GET_SIZE(HDRP(ptr)) >= asize){
            return ptr;
        }
    }
    return NULL;
}
//place 함수
static void place(void *ptr, size_t asize){
    size_t csize = GET_SIZE(HDRP(ptr));
    //할당블록은 freelist에서 지운다
    remove_block(ptr);
    if ((csize - asize) >= (2*DSIZE)){
        PUT(HDRP(ptr), PACK(asize,1));//현재 크기를 헤더에 집어넣고
        PUT(FTRP(ptr), PACK(asize,1));
        ptr = NEXT_BLKP(ptr);
        PUT(HDRP(ptr), PACK(csize-asize,0));
        PUT(FTRP(ptr), PACK(csize-asize,0));
        put_free_block(ptr); // free list 첫번째에 분할된 블럭을 넣는다.
    }
    else{
        PUT(HDRP(ptr), PACK(csize,1));
        PUT(FTRP(ptr), PACK(csize,1));
    }
}

void *mm_malloc(size_t size)
{
    size_t asize; 
    size_t extendsize;
    void *ptr;

    if(size <= 0) 
        return NULL;
    
    if(size <= DSIZE) 
        asize = 2*DSIZE; 
    else              
        asize = DSIZE * ((size+(DSIZE)+(DSIZE-1)) / DSIZE);

    if((ptr = find_fit(asize))!=NULL){
        place(ptr,asize); 
        return ptr;
    }

    extendsize = MAX(asize,CHUNKSIZE);
    if((ptr = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(ptr,asize);
    return ptr;
}

// LIFO 방식으로 새로 반환되거나 생성된 가용 블록을 가용리스트 맨 앞에 추가
void put_free_block(void *ptr){
    SUCC_FREEP(ptr) = free_listp;
    PRED_FREEP(ptr) = NULL;
    PRED_FREEP(free_listp) = ptr;
    free_listp = ptr;
}


void remove_block(void *ptr){
    if(ptr == free_listp){
        PRED_FREEP(SUCC_FREEP(ptr)) = NULL;
        free_listp = SUCC_FREEP(ptr);
    }else{
        SUCC_FREEP(PRED_FREEP(ptr)) = SUCC_FREEP(ptr);
        PRED_FREEP(SUCC_FREEP(ptr)) = PRED_FREEP(ptr);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size,0));
    PUT(FTRP(ptr), PACK(size,0));
    coalesce(ptr);    
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size){
    if(size <= 0){ 
        mm_free(ptr);
        return 0;
    }
    if(ptr == NULL){
        return mm_malloc(size); 
    }
    void *newp = mm_malloc(size); 
    if(newp == NULL){
        return 0;
    }
    size_t oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize){
    	oldsize = size; 
	}
    memcpy(newp, ptr, oldsize); 
    mm_free(ptr);
    return newp;
}