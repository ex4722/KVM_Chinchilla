#include <stdint.h>
#include <stdbool.h>
#include "list.h"

#include "paging.h"
#include "buddy.h"

#include "printf.h"
#include "multiboot.h"
#include "kernel32.h"
#include "assert.h"


struct free_area free_area = {0};


// store the start and end of kernel image so we dont' accidently free these pages
extern uint64_t kernel64_start;
extern uint64_t kernel64_end;

// takes a list head and extracts the page_struct out of it
static inline struct page_struct * list_to_page(struct list_head * list){
    return list_entry(list, struct page_struct, list);
}

// returns list attribute of page 
static inline struct list_head * page_to_list(struct page_struct * page){
    return &page->list;
}


// the page_order is incremented by 1 as we store order 0 as 1 so unfreed pages can be stored as 0 has nice side effect of returning order of 0-MAX_ORDER and -1 for not freed 
static inline void set_page_order(struct page_struct * page, u64 order){
    page->flags |=  (((order + 1)& PAGE_ORDER_MASK) << PAGE_ORDER);
}

// returns 0-MAX_ORDER if page is allocated and -1 if free 
static inline u64 get_page_order(struct page_struct * page){
    return ((page->flags >> PAGE_ORDER) & PAGE_ORDER_MASK) - 1;

}

static inline void remove_page_in_list(struct page_struct *page){
    DELETE_LIST_HEAD(page_to_list(page));
}

static inline void add_page_to_order(struct page_struct * page, u64 order){
    set_page_order(page, order);
    ADD_LIST(GET_ORDER_HEAD(order), page_to_list(page));
}

// returns the largest order size will fill up 
u64 page_order(u64 size){
    if(size >> PAGE_SHIFT == 0){
        return 0;
    }
    // counts the number of leading zero's and adds PAGE_SHIFT to account for order 0 being page_shift 
    u64 index = __builtin_clz(size) + PAGE_SHIFT;
    return 31-index;
}


void init_free_area(){
    for(u32 i = 0; i < MAX_ORDER; i++){
        INIT_LIST_HEAD(&free_area.orders[i]);
    }
}

void init_page_structs(u64 start_addr, u64 len){
    for(u64 current = start_addr; current < start_addr + len; current += PAGE_SIZE){
        ASSERT(current > (4ul * GIGABYTE), "System only supports up to 4B of memory");
        struct page_struct * page= PHYS_TO_PAGE(current);
        page->flags = 0;
        page->address = current; 
        INIT_LIST_HEAD(&page->list);
    }
}

// function free's all unused memory to free_area
void init_memory(struct kernel_32_info* info, multiboot_info_t* multiboot){
   init_free_area(); 

    // loop over memory regions specificed by mutliboot_info 
    ASSERT(!(multiboot->flags & (1 << 6)), "mmap_* fields are invalid, no memory found");
    struct multiboot_mmap_entry* base = (struct multiboot_mmap_entry*)(u64)multiboot->mmap_addr;
    u64 entry_count = multiboot->mmap_length / sizeof(struct multiboot_mmap_entry);
    for(u32 i = 0; i < entry_count; i++){
        if(base[i].type != MULTIBOOT_MEMORY_AVAILABLE){
            continue;
        }
        u64 start = base[i].addr;
        u64 end = base[i].addr + base[i].len;
    }
}

u64 add_and_coalecse(struct page_struct * page, u64 order){
    u64 pfn = PAGE_TO_PFN(page);
    u64 buddy_pfn = get_buddy(pfn, order);
    struct page_struct * buddy_page = PFN_TO_PAGE(buddy_pfn);
    u64 buddy_order = get_page_order(buddy_page);

    if(order >= MAX_ORDER){
        // if MAX_ORDER no more orders to merge into 
        goto base_case;
    }

    // if buddy is NOT freed OR not the same order, just add buddy and exit
    if(buddy_order == order){
        struct page_struct * head_page = PFN_TO_PAGE(MIN(pfn, buddy_pfn));
        struct page_struct * tail_page = PFN_TO_PAGE(MAX(pfn, buddy_pfn));
        // TODO Do we really need this?
        if(get_page_order(tail_page) != -1){
            remove_page_in_list(tail_page);
        }
        add_and_coalecse(head_page, order +1);
    };
 
base_case:
    add_and_coalecse(page, order);
    return 0;
}

u64 free_page(u64 page_addr, u64 page_len){
    while(page_len != 0){
        u64 order = MIN(page_order(page_len), MAX_ORDER) ;
        u64 page_size =  ORDER_TO_SIZE(order);
        struct page_struct * page = PHYS_TO_PAGE(page_addr);

        add_and_coalecse(page, order);

        page_len -= page_size;
        page_addr -= page_size;
    }
    return 0;
}

