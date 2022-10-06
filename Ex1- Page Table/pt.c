#include "os.h"

#define NUMBER_OF_LEVELS 5
#define NUMBER_OF_ROWS 512

/* Description: implementation of OS code that handles a multi-level (trie-based)
page table. Creates/destroys virtual memory mappings in a page table and checks if
an address is mapped in a page table.*/

/* =====================  Declarations ===================== */
/* Returns the vpn that matches the entry of the given level*/
uint64_t cut_vpn(uint64_t vpn, int level);

/* Returns a pointer to the virtual page address of a ppn with offset zero */
uint64_t *get_ptr(uint64_t ppn);

/* Returns false if there exist at least one valid entry
 * true- otherwise*/
int check_frame_validation(uint64_t *frame_pointer);

/* Check frames from the leaf to the root of the table
 * if a frame has no entry with valid bit- free it, otherwise- end the function
 * because freeing frames in a lower level will destroy it*/
void free_unused_frames(uint64_t *vpn_to_destroy);

uint64_t page_table_query(uint64_t pt, uint64_t vpn);
void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn);

/* =====================  Functions ===================== */
/* Receives: pt- physical page number of the page table root
             vpn- virtual page number the caller wishes to map/unmap
             ppn- 2 options: (1) NO MAPPING- then vpn’s mapping (if it exists) should be destroyed
                             (2) physical page number that vpn should be mapped to
   The function creates/destroys virtual memory mappings in a page table*/
void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn)
{
    uint64_t *trie_node = (uint64_t *)phys_to_virt(pt << 12);
    uint64_t pte;

    /* An array that will be used in case of NO_MAPPING. it will represent: (pte= page table entry)
    ppn of level 0 | pte in level 0 | ppn of level 1 | pte in level 1 | ppn of level 2 | pte in level 2 |
        ppn of level 3 | pte in level 3 | ppn of level 4*/
    uint64_t vpn_to_destroy[(NUMBER_OF_LEVELS * 2) - 1];
    vpn_to_destroy[0] = pt << 12; // ppn of level 0
    int i = 1;
    for (int current_level = 0; current_level < NUMBER_OF_LEVELS; current_level++)
    {

        // Cut vpn according to current level and get only the correct 9th bits (with & 0x1ff=111111111)
        pte = cut_vpn(vpn, current_level) & 0x1ff;

        // Update virtual memory mapping
        if (ppn != NO_MAPPING)
        {
            // Creates the mapping of the physical address to the last level and update valid bit
            if (current_level == NUMBER_OF_LEVELS - 1)
            {
                // Make offset of ppn zero and valid bit 
                trie_node[pte] = (ppn << 12) + 1;
                // Done
                return;
            }
            else
            {
                // The entry is empty- need to create a new frame for it
                if (trie_node[pte] == NO_MAPPING || trie_node[pte] == 0)
                {
                    // Allocate new physical page frame for the new node in the next level
                    uint64_t new_node_ppn = alloc_page_frame();
                    // Set the pointer to the new node (with offset 0) and valid bit
                    trie_node[pte] = (new_node_ppn << 12) + 1;
                }
            }
        }
        // Destroy virtual memory mapping
        else
        {
            // In level 4- in the right pte- NO_MAPPING and free the frames that can be freed (with free_unused_frames)
            if (current_level == NUMBER_OF_LEVELS - 1)
            {
                trie_node[pte] = NO_MAPPING;
                free_unused_frames(vpn_to_destroy);
                return;
            }
            else
            {
                // No need to destroy mapping- it already doesn't exist
                if (trie_node[pte] == NO_MAPPING || trie_node[pte] == 0)
                {
                    return;
                }
                // Entry is valid- make valid bit zero and add the pte of level 'current_level' and ppn of 'current_level'+1 to the array
                else
                {
                    vpn_to_destroy[i] = pte;
                    vpn_to_destroy[i + 1] = trie_node[pte];
                    i = i + 2;
                    // Make valid bit to zero
                    trie_node[pte] = ((trie_node[pte] >> 1) << 1);
                }
            }
        }
        // Continue the tree walk
        trie_node = get_ptr(trie_node[pte]);
    }
}

/* Receives: pt- physical page number of the page table root
             vpn- virtual page number the caller wishes to map/unmap
   Returns  the physical page number that vpn is mapped to, or NO MAPPING if no mapping exists*/
uint64_t page_table_query(uint64_t pt, uint64_t vpn)
{
    uint64_t *trie_node = (uint64_t *)phys_to_virt(pt << 12);
    uint64_t pte;
    for (int current_level = 0; current_level < NUMBER_OF_LEVELS; current_level++)
    {
        // Cut vpn according to current level and get only the correct 9th bits (with & 0x1ff=111111111)
        pte = cut_vpn(vpn, current_level) & 0x1ff;

        // Only two option for NO_MAPPING vpn- empty or NO-MAPPING
        // No valid bit or no mapping for this vpn
        if (trie_node[pte] == NO_MAPPING || trie_node[pte] == 0)
        {
            return NO_MAPPING;
        }

        // ppn level- return the valid address
        if (current_level == NUMBER_OF_LEVELS - 1)
        {
            return (trie_node[pte] >> 12);
        }

        // Else- continue tree walk
        trie_node = get_ptr(trie_node[pte]);
    }
    return NO_MAPPING;
}

uint64_t cut_vpn(uint64_t vpn, int level)
{
    // Assume vpn has no offset
    return (vpn >> (((NUMBER_OF_LEVELS - (level + 1)) * 9)));
}

uint64_t *get_ptr(uint64_t ppn)
{
    return phys_to_virt(ppn & 0xFFFFFFFFFFFFF000);
}

/* Recieves: frame_pointer- pointer to current frame in current level
   Return 1- if all entrys are invalid, else-0*/
int check_frame_validation(uint64_t *frame_pointer)
{
    for (int i = 0; i < NUMBER_OF_ROWS; i++)
    {
        // A bit valid of updated cell is 1
        if ((frame_pointer[i] != NO_MAPPING) && (frame_pointer[i] & 1) == 1)
        {
            return 0;
        }
    }
    return 1;
}

/* Recieves: vpn_to_destroy- An array of frames to destroy
   Frees these frames*/
void free_unused_frames(uint64_t *vpn_to_destroy)
{
    /* Keep current frame's pointer and previous frame's pointer
    so an entry that now points to a freed frame will become NO_MAPPING*/
    uint64_t *current_frame;
    uint64_t *prev_frame;
    for (int current_level = (NUMBER_OF_LEVELS * 2) - 2; current_level > 0; current_level = current_level - 2)
    {
        current_frame = get_ptr(vpn_to_destroy[current_level]);
        // Frame can be free
        if (check_frame_validation(current_frame) == 1)
        {
            free_page_frame(vpn_to_destroy[current_level] >> 12);
            prev_frame = get_ptr(vpn_to_destroy[current_level - 2]);
            prev_frame[vpn_to_destroy[current_level - 1]] = NO_MAPPING;
        }
        // Frame cannot be free so the frames in lower levels cannot either- done
        else
            break;
    }
    return;
}