#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

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
    ""};

#define WSIZE 4
#define DSIZE 8
#define MINIMUM 8
#define CHUNKSIZE (1 << 12)
#define LISTLIMIT 20

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) ((GET(p) >> 1) << 1)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(ptr) ((char *)(ptr)-WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE)

#define NEXT_BLKP(ptr) ((char *)(ptr) + GET_SIZE(((char *)(ptr)-WSIZE)))
#define PREV_BLKP(ptr) ((char *)(ptr)-GET_SIZE(((char *)(ptr)-DSIZE)))

#define PREC_FREEP(ptr) (*(void **)(ptr))
#define SUCC_FREEP(ptr) (*(void **)(ptr + WSIZE))

static char *heap_listp;
static void *seg_list[LISTLIMIT];

static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void *find_fit(size_t asize);
static void place(void *ptr, size_t newsize);

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);

int mm_init(void)
{
    for (int i = 0; i < LISTLIMIT; i++)
    {
        seg_list[i] = NULL;
    }
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(MINIMUM, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(MINIMUM, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp = heap_listp + 2 * WSIZE;

    if (extend_heap(4) == NULL)
    {
        return -1;
    }

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    {
        return -1;
    }
    return 0;
}

static void *extend_heap(size_t words)
{
    char *ptr;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : (words)*WSIZE; // size를 짝수 word & byte 형태로 만든다.
    if ((long)(ptr = mem_sbrk(size)) == -1)
    {
        return NULL;
    }

    PUT(HDRP(ptr), PACK(size, 0));         // 가용 블록 header 만들기
    PUT(FTRP(ptr), PACK(size, 0));         // 가용 블록 footer 만들기
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1)); // 에필로그 헤더 위치 변경

    return coalesce(ptr);
}
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *ptr;

    /* 의미 없는 요청 처리 안함 */
    if (size == 0)
    {
        return NULL;
    }
    if (size <= DSIZE)
    {
        asize = 2 * DSIZE;
    }
    else
    {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    if ((ptr = find_fit(asize)) != NULL)
    {
        place(ptr, asize);
        return ptr;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize / WSIZE)) == NULL)
    {
        return NULL;
    }
    place(ptr, asize);
    return ptr;
}

static void *coalesce(void *ptr)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    // case 1 : 둘 다 할당 되어 있을 경우
    // case 2 : 직전 블록 할당, 직후 블록 가용
    if (prev_alloc && !next_alloc)
    {
        remove_block(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    { // case 3 : prev block not allocated, next block allocated
        remove_block(PREV_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));

        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    else if (!prev_alloc && !next_alloc)
    { // case 4 : prev, next both not allocated
        remove_block(PREV_BLKP(ptr));
        remove_block(NEXT_BLKP(ptr));

        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    put_free_block(ptr, size);
    return ptr;
}
static void *find_fit(size_t asize)
{
    void *ptr;
    int index = 0;
    size_t searchsize = asize;

    while (index < LISTLIMIT)
    {
        if ((index == LISTLIMIT - 1) || (searchsize <= 1) && (seg_list[index] != NULL))
        {
            ptr = seg_list[index];
            while ((ptr != NULL) && (asize > GET_SIZE(HDRP(ptr))))
            {
                ptr = SUCC_FREEP(ptr);
            }
            if (ptr != NULL)
            {
                return ptr;
            }
        }
        searchsize >>= 1;
        index++;
    }
    // no fit
    return NULL;
}

static void place(void *ptr, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(ptr));
    remove_block(ptr);
    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));
        ptr = NEXT_BLKP(ptr);
        PUT(HDRP(ptr), PACK(csize - asize, 0));
        PUT(FTRP(ptr), PACK(csize - asize, 0));
        coalesce(ptr);
    }
    else
    {
        PUT(HDRP(ptr), PACK(csize, 1));
        PUT(FTRP(ptr), PACK(csize, 1));
    }
}
// 오름차순으로 정렬된 seq list에서 크기에 맞게 탐색하여 삽입
void put_free_block(void *ptr, size_t size)
{
    int idx = 0;
    void *search_ptr;
    void *insert_ptr = NULL; // search_ptr 값 임시 저장. 가용 블록이 삽입될 위치

    int temp = size;
    // size의 비트를 1씩 오른쪽으로 shift 시키며, size class 리스트에 할당될 인덱스 설정
    while ((idx < LISTLIMIT - 1) && (temp > 1))
    {
        temp >>= 1;
        idx++;
    }

    search_ptr = seg_list[idx];

    while ((search_ptr != NULL) && (size > GET_SIZE(HDRP(search_ptr))))
    {
        insert_ptr = search_ptr;
        search_ptr = SUCC_FREEP(search_ptr);
    }

    // size class의 가용 블록들의 끝에 도달하기 전에 size보다 큰크기를 가진 블록을 만났을 경우
    if (search_ptr != NULL)
    {
        // size class의 중간에 들어가야하는 경우
        if (insert_ptr != NULL)
        {
            SUCC_FREEP(ptr) = search_ptr;
            PREC_FREEP(ptr) = insert_ptr;
            PREC_FREEP(search_ptr) = ptr;
            SUCC_FREEP(insert_ptr) = ptr;
        }
        else // size class의 맨 앞에 와야 하는 경우
        {
            SUCC_FREEP(ptr) = search_ptr;
            PREC_FREEP(ptr) = NULL;
            PREC_FREEP(search_ptr) = ptr;
            seg_list[idx] = ptr;
        }
    }
    // search_ptr이 null인 경우 -> size class의 끝에 위치해야할 경우 or size class에 해당하는 가용 블록이 없을 경우
    else
    {
        // 해당 size class에 가용 블록들이 있을 경우
        if (insert_ptr != NULL)
        {
            SUCC_FREEP(ptr) = NULL;
            PREC_FREEP(ptr) = insert_ptr;
            SUCC_FREEP(insert_ptr) = ptr;
        }
        else // 해당 size class에 가용 블록이 없을 경우
        {
            SUCC_FREEP(ptr) = NULL;
            PREC_FREEP(ptr) = NULL;
            seg_list[idx] = ptr;
        }
    }
    return;
}
// remove_block(ptr) : 할당되거나 연결되는 가용 블록을 free list에서 없앤다.
void remove_block(void *ptr)
{
    int idx = 0;
    size_t size = GET_SIZE(HDRP(ptr));

    while ((idx < LISTLIMIT - 1) && (size > 1))
    { // 지우려는 list idx 탐색
        size >>= 1;
        idx++;
    }

    if (SUCC_FREEP(ptr) != NULL)
    {
        if (PREC_FREEP(ptr) != NULL)
        { // 중간에있는걸 지우는경우
            PREC_FREEP(SUCC_FREEP(ptr)) = PREC_FREEP(ptr);
            SUCC_FREEP(PREC_FREEP(ptr)) = SUCC_FREEP(ptr);
        }
        else
        { // 맨 앞 블록 지우는 경우
            PREC_FREEP(SUCC_FREEP(ptr)) = NULL;
            seg_list[idx] = SUCC_FREEP(ptr);
        }
    }
    else
    {
        if (PREC_FREEP(ptr) != NULL)
        { // 맨 뒤 블록 지우는 경우
            SUCC_FREEP(PREC_FREEP(ptr)) = NULL;
        }
        else
        { // 하나만 있었을 경우
            seg_list[idx] = NULL;
        }
    }
    return;
}

void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0)); // 헤더 갱신
    PUT(FTRP(ptr), PACK(size, 0)); // 푸터 갱신
    coalesce(ptr);                 // 합치기
}

void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    if (size == 0)
    {
        mm_free(ptr);
        return;
    }

    void *new_ptr = mm_malloc(size);
    if (new_ptr == NULL)
    {
        return NULL;
    }
    size_t csize = GET_SIZE(HDRP(ptr));
    if (size < csize)
    {                 // 재할당 요청에 들어온 크기보다, 기존 블록의 크기가 크다면
        csize = size; // 기존 블록의 크기를 요청에 들어온 크기 만큼으로 줄인다.
    }
    memcpy(new_ptr, ptr, csize); // ptr 위치에서 csize만큼의 크기를 new_ptr의 위치에 복사함
    mm_free(ptr);                // 기존 ptr의 메모리는 할당 해제해줌
    return new_ptr;
}