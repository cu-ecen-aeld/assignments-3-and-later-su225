/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <string.h>
#include <stdlib.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    __u8 i = 0;
    size_t seen_so_far = 0;
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        __u8 idx = (i + buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        size_t max_index = seen_so_far + buffer->entry[idx].size - 1;
        if (char_offset <= max_index) {
            size_t idx_internal = char_offset - seen_so_far;
            *entry_offset_byte_rtn = idx_internal;
            return &(buffer->entry[idx]);
        }
        seen_so_far += buffer->entry[idx].size;
    }
    return NULL;
}

static void __aesd_circular_buffer_maybe_free_entry_for_replace(struct aesd_circular_buffer *buffer, __u8 idx)
{
    /* If there was no entry to begin with, then don't worry */
    if (buffer->entry[idx].buffptr == NULL)
        return;
    
    /*
     * Userspace and kernel use different dynamic memory allocation
     * routines. Hence, all these things
     */
#ifdef __KERNEL__
    kfree(buffer->entry[idx].buffptr);
#else
    free(buffer->entry[idx].buffptr);
#endif
}


/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location. Once the entry is submitted, it should be thought of as belonging to the circular
 * buffer and the caller should not manipulate the entry further.
 *
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    __u8 idx = buffer->in_offs;

    /**
     * If the current entry is already allocated, then we have to free it
     * to avoid memory leaks. In case of kernel module, this is more serious 
     */
    __aesd_circular_buffer_maybe_free_entry_for_replace(buffer, idx);

    buffer->entry[idx].size = add_entry->size;
    buffer->entry[idx].buffptr = add_entry->buffptr;
    buffer->in_offs = (idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        return;
    }
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = 1;
    }
}
/**
 * Initializes the circular buffer described by @param buffer to an empty struct.
 * This way we make sure that all the ring buffer entries are NULL which is very
 * useful for checking if a slot is occupied or not.
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

/**
 * Frees all the resources associated with the circular buffer described by
 * @param buffer. It frees all the memory allocated to the circular buffer entries.
 */
void aesd_circular_buffer_destroy(struct aesd_circular_buffer *buffer)
{
    __u8 idx = 0;
    while (idx < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        __aesd_circular_buffer_maybe_free_entry_for_replace(buffer, idx);
        idx++;
    }
}