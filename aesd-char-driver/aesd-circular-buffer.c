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
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"
#include <stdio.h>
#include <stdlib.h>
/**
 * @param buffer the buffer to search for corresponding offset. Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset. This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    //index of the buffer where the entry_offset_byte_rtn was found
    uint8_t byte_match_idx = buffer->out_offs;
    //iterate while char_offset is bigger than the size of the buffptr stored in the buffer
    while(char_offset >= buffer->entry[byte_match_idx].size)
    {
        char_offset -= buffer->entry[byte_match_idx].size;
        //increment index because we know the byte isn't in the current index
	    byte_match_idx++;

        //we've reached the end of the buffer, start from the beginning
        if(byte_match_idx == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            byte_match_idx = 0;
        }
        //we've done a full cycle of the buffer and the offset still wasn't found
        if(buffer->out_offs == byte_match_idx)
        {
            char_offset = -1;
            break;
        }
    }
    //if offset was found
    if(char_offset != -1)
    {
    	if(entry_offset_byte_rtn != NULL)
    	{
            *entry_offset_byte_rtn = char_offset;
            return &buffer->entry[byte_match_idx];
        }
    }
    
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    //check to see if the circular buffer is already full
    if(buffer->full)
    {
        buffer->entry[buffer->in_offs] = *add_entry;
        buffer->in_offs++;
        //circular buffer is full, start overwriting from the beginning
        if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buffer->in_offs = 0;
        }
        buffer->out_offs++;
        //circular buffer is full, start reading from the beginning
        if(buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buffer->out_offs = 0;
        }
    }
    else
    {
        buffer->entry[buffer->in_offs] = *add_entry;
        buffer->in_offs++;
        //circular buffer is full, start overwriting from the beginning
        if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buffer->full = true;
            buffer->in_offs = 0; 
        }
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
