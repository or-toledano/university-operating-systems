#include "os.h"

#define TABLE_ADDR_SIZE 9 /* (4096B page)/(8B PTE) = 512 PTEs, log(512) = 9 */
#define TABLE_ADDR_MSK 0x1ff /* 9 activated bits */
#define NLEVELS 5 /* (45 bit page address )/(9 bit page address space per level) = 5 levels  */
#define VLD_MSK 1 /* valid bit mask*/
#define OFF_SIZE 12

void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn) {
    _Bool valid;
    int i = NLEVELS - 1;
    uint64_t vpn_part_for_level;
    uint64_t *current_table = phys_to_virt(pt << OFF_SIZE);
    if (ppn == NO_MAPPING) {
        for (; i >= 0; --i) {
            vpn_part_for_level = (vpn >> i * TABLE_ADDR_SIZE) & TABLE_ADDR_MSK;
            if (i == 0)
                current_table[vpn_part_for_level] &= ~VLD_MSK; /* invalidate the PTE on the last level */
            else
                current_table = phys_to_virt(current_table[vpn_part_for_level] & (~VLD_MSK));
        }
    } else {
        for (; i >= 0; --i) {
            vpn_part_for_level = (vpn >> i * TABLE_ADDR_SIZE) & TABLE_ADDR_MSK;
            valid = current_table[vpn_part_for_level] & VLD_MSK;
            if (i == 0)
                current_table[vpn_part_for_level] = (ppn << OFF_SIZE) | VLD_MSK;
            else {
                if (!valid)
                    current_table[vpn_part_for_level] =
                            (alloc_page_frame() << OFF_SIZE) | VLD_MSK; /* new, valid frame */
                current_table = phys_to_virt(current_table[vpn_part_for_level] & (~VLD_MSK));
            }
        }
    }
}

uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
    _Bool valid;
    int i = NLEVELS - 1;
    uint64_t vpn_part_for_level, current_pte;
    uint64_t *current_table = phys_to_virt(pt << OFF_SIZE);
    for (; i >= 0; --i) {
        vpn_part_for_level = (vpn >> i * TABLE_ADDR_SIZE) & TABLE_ADDR_MSK;
        current_pte = current_table[vpn_part_for_level];
        valid = current_pte & VLD_MSK;
        if (!valid)
            return NO_MAPPING;
        if (i == 0)
            return current_pte >> OFF_SIZE;
        current_table = phys_to_virt(current_pte & (~VLD_MSK));
    }
    return -1; /* should never get here */
}
