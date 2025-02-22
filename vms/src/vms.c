#include "vms.h"

#include "mmu.h"
#include "pages.h"

#include <stdio.h>
#include <string.h>

static int page_using_count[MAX_PAGES] = {0};

/* A debugging helper that will print information about the pointed to PTE
   entry. */
static void print_pte_entry(uint64_t* entry);

void handle_page_copy_on_write( void* page_curr, uint64_t* entry_curr) {
        void* page_new = vms_new_page();
        if (page_new == NULL) {
            return;
        }

        if (page_curr != NULL) {
            memcpy(page_new, page_curr, PAGE_SIZE);  
        }

        uint64_t ppn_new = vms_page_to_ppn(page_new);
        vms_pte_set_ppn(entry_curr, ppn_new); 
}


void page_fault_handler(void* virtual_address, int level, void* page_table) {
    if(level > 0) return;

    uint64_t* entry_curr = vms_page_table_pte_entry(page_table, virtual_address, level);


    if (!vms_pte_valid(entry_curr) || !vms_pte_custom(entry_curr)) return;

    int page_index_curr = vms_get_page_index(vms_ppn_to_page(vms_pte_get_ppn(entry_curr)));

    if (page_using_count[page_index_curr] == 0) {
        vms_pte_custom_clear(entry_curr);
        vms_pte_write_set(entry_curr);
        return;
    }

    void* page_curr = vms_ppn_to_page(vms_pte_get_ppn(entry_curr));
    page_using_count[page_index_curr]--;
    handle_page_copy_on_write( page_curr, entry_curr);
    vms_pte_write_set(entry_curr); 
    vms_pte_custom_clear(entry_curr);
    
}

void common_recursive_copy(void* P_page, void* C_page, int level, int is_copy_on_write) {
    if (level < 0) return;

    for (int i = 0; i < NUM_PTE_ENTRIES; ++i) {
        uint64_t* P_entry_curr = vms_page_table_pte_entry_from_index(P_page, i);

        if (!vms_pte_valid(P_entry_curr)) continue;

        uint64_t* C_entry_curr = vms_page_table_pte_entry_from_index(C_page, i);
        vms_pte_valid_set(C_entry_curr);
        if (vms_pte_read(P_entry_curr)) vms_pte_read_set(C_entry_curr);
        if (vms_pte_custom(P_entry_curr)) vms_pte_custom_set(C_entry_curr);

        if (level > 0) {
            void* C_Ln = vms_new_page();
            uint64_t P_ppn = vms_pte_get_ppn(P_entry_curr);
            void* Ptemp = vms_ppn_to_page(P_ppn);

            vms_pte_set_ppn(C_entry_curr, vms_page_to_ppn(C_Ln));
            common_recursive_copy(Ptemp, C_Ln, level - 1, is_copy_on_write);
        } else {
            if (is_copy_on_write) {
                uint64_t P_ppn = vms_pte_get_ppn(P_entry_curr);
                void* P_page_new = vms_ppn_to_page(P_ppn);
                int page_index_curr = vms_get_page_index(P_page_new);
                page_using_count[page_index_curr]++;

                vms_pte_set_ppn(C_entry_curr, vms_page_to_ppn(P_page_new));

                if (vms_pte_write(P_entry_curr)){
                vms_pte_custom_set(P_entry_curr);
                vms_pte_custom_set(C_entry_curr);
                }
                vms_pte_write_clear(C_entry_curr);
                vms_pte_write_clear(P_entry_curr);
            } else {
                void* C_page_new = vms_new_page();
                uint64_t P_ppn = vms_pte_get_ppn(P_entry_curr);
                void* P_page_new = vms_ppn_to_page(P_ppn);

                if (vms_pte_write(P_entry_curr)) vms_pte_write_set(C_entry_curr);
                memcpy(C_page_new, P_page_new, PAGE_SIZE);
                vms_pte_set_ppn(C_entry_curr, vms_page_to_ppn(C_page_new));
            }
        }
    }
}

void* vms_fork_copy() {
    void* P_page = vms_get_root_page_table();
    void* C_page = vms_new_page();
    common_recursive_copy(P_page, C_page, 2, 0);  
    return C_page;
}

void* vms_fork_copy_on_write() {
    void* P_page = vms_get_root_page_table();
    void* C_page = vms_new_page();
    common_recursive_copy(P_page, C_page, 2 , 1);  
    return C_page;
}

static void print_pte_entry(uint64_t* entry) {
    const char* dash = "-";
    const char* custom = dash;
    const char* write = dash;
    const char* read = dash;
    const char* valid = dash;
    if (vms_pte_custom(entry)) {
        custom = "C";
    }
    if (vms_pte_write(entry)) {
        write = "W";
    }
    if (vms_pte_read(entry)) {
        read = "R";
    }
    if (vms_pte_valid(entry)) {
        valid = "V";
    }

    printf("PPN: 0x%lX Flags: %s%s%s%s\n",
           vms_pte_get_ppn(entry),
           custom, write, read, valid);
}
