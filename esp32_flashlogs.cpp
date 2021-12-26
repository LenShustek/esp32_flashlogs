/* file: esp32_flashlogs.cpp
   -----------------------------------------------------------------------------------------------
   This is a module for the ESP32 Wifi microcontroller that efficiently implements
   non-volatile FLASH memory storage of event logs. The logs are circular: when
   the log becomes full, adding the next new entry causes 4K bytes of the oldest
   entries to be removed. (FLASH can only be erased in 4K byte blocks.)
   Entries may be retrieved in either order, starting with the newest or the oldest.
   The log data will survive rebooting and FLASHing a new program.
   See the README.txt file for more information.
   ---------------------------------------------------------------------------------------------*/
/***** CHANGE LOG *****
     24 Dec 2021, L. Shustek, Written for the new version of the pool/spa controller.
*****/
/* Copyright(c) 2021, Len Shustek
   The MIT License(MIT)
   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
   and associated documentation files(the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions :

   The above copyright notice and this permission notice shall be included in all copies or
   substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
   BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "esp32_flashlogs.h"
#include <string.h>

// open or create the log partition with as many entries of the specified size as will fit
enum flashlog_error
flashlog_open (
   const char *logname, // the optional partition name, or if null use the first log-type partition
   int datasize, // the size of user data in each log entry
   struct flashlog_state_t *state) { // where to put the ram-resident state structure

   const esp_partition_t *partition;
   struct flashlog_hdr_t hdr;

   if (!(partition = esp_partition_find_first(ESP_PARTITION_TYPE_LOG, ESP_PARTITION_SUBTYPE_ANY, logname)))
      return FLASHLOG_ERR_NO_PARTITION;
   state->partition = partition; // remember the partition we are to use
   // check that the datasize plus the header is a power of two, up to 4096
   int entrysize = datasize + sizeof(struct flashlog_entry_hdr_t);
   if (entrysize > 4096 || (entrysize & (entrysize - 1)) != 0)
      return FLASHLOG_ERR_BADSIZE;
   state->datasize = datasize;
   // read the header that should be at the start of the log
   if ((state->partition_err = esp_partition_read(partition, 0, &hdr, sizeof(hdr))) != ESP_OK)
      return FLASHLOG_ERR_READERR;
   if (memcmp(hdr.id, FLASHLOG_ID, sizeof(hdr.id)) != 0 // if no header (an uninitialized partition)
   || hdr.datasize != datasize) { // or the log entry data size is different,
      // initialize the log from scratch, starting with a complete erase of the partition
      if ((state->partition_err = esp_partition_erase_range(partition, 0, partition->size)) != ESP_OK)
         return FLASHLOG_ERR_ERASEERR;
      memcpy(hdr.id, FLASHLOG_ID, sizeof(hdr.id));  // initialize and write the log header
      hdr.datasize = datasize;
      hdr.numslots = (partition->size - FLASHLOG_SLOT0) / (datasize + sizeof(struct flashlog_entry_hdr_t));
      if ((state->partition_err = esp_partition_write(partition, 0, &hdr, sizeof(hdr))) != ESP_OK)
         return FLASHLOG_ERR_WRITEERR;
      // initialize the ram-resident state information
      state->numslots = hdr.numslots;
      state->highest_seqno = 0;
      state->oldest = state->newest = state->current = 0;
      state->numinuse = 0; }
   else { // the log exists
      state->numslots = hdr.numslots;
      // read all the entry headers to find out about slots in use
      uint32_t oldest_seqno = UINT32_MAX; // the oldest sequence number is the smallest
      state->highest_seqno = 0; // the newest sequence number is the largest
      state->newest = state->oldest = 0; // in case it's empty
      for (int slot = 0; slot < hdr.numslots; ++slot) {
         struct flashlog_entry_hdr_t entryhdr;
         int offset = FLASHLOG_SLOT0 + slot * (hdr.datasize + sizeof(struct flashlog_entry_hdr_t));
         if ((state->partition_err = esp_partition_read(partition, offset, &entryhdr, sizeof(entryhdr))) != ESP_OK)
            return FLASHLOG_ERR_READERR;
         if (entryhdr.seqno != UINT32_MAX) {  // not an unused entry
            ++state->numinuse;
            if (entryhdr.seqno > state->highest_seqno) { // record the higest seqno
               state->highest_seqno = entryhdr.seqno;
               state->newest = slot; }
            if (entryhdr.seqno < oldest_seqno) { // record the oldest slot (lowest seqno)
               oldest_seqno = entryhdr.seqno;
               state->oldest = slot; } } }  }
   state->current = state->newest;
   // allocate a buffer for an log entry with its header
   if (!(state->entrybuf = (struct flashlog_entry_hdr_t *)malloc(datasize + sizeof(struct flashlog_entry_hdr_t))))
      return FLASHLOG_ERR_NOMEM;
   state->logdata = (char *)state->entrybuf + sizeof(struct flashlog_entry_hdr_t); // where the user data part goes
   return FLASHLOG_ERR_OK; }

// close the log and free the buffer we allocated
enum flashlog_error
flashlog_close (struct flashlog_state_t *state) {
   if (state->entrybuf)
      free((void *)state->entrybuf);
   state->entrybuf = NULL;
   state->logdata = NULL;
   return FLASHLOG_ERR_OK; }

// add a new log entry using the data at state->logdata
enum flashlog_error
flashlog_add (struct flashlog_state_t *state) {
   if (!state->entrybuf)
      return FLASHLOG_ERR_NOINIT;
   if (state->numinuse > 0) { // not empty, so add after newest
      if (++state->newest >= state->numslots) state->newest = 0; }
   int offset = FLASHLOG_SLOT0 + state->newest * (state->datasize + sizeof(struct flashlog_entry_hdr_t));
   int length = state->datasize + sizeof(struct flashlog_entry_hdr_t);
   if (state->numinuse == state->numslots) {
      // log is full: erase the oldest 4K and adjust for the entries thus deleted
      if ((state->partition_err = esp_partition_erase_range(state->partition, offset, 4096)) != ESP_OK)
         return FLASHLOG_ERR_ERASEERR;
      state->numinuse -= 4096 / length;
      state->oldest += 4096 / length;
      if (state->oldest >= state->numslots) state->oldest -= state->numslots; }
   state->entrybuf->seqno = ++state->highest_seqno; // assign a new sequence number
   ++state->numinuse;
   if ((state->partition_err = esp_partition_write(state->partition, offset, state->entrybuf, length)) != ESP_OK)
      return FLASHLOG_ERR_WRITEERR;
   return FLASHLOG_ERR_OK; };

// read log entry number state->current into state->logdata
enum flashlog_error
flashlog_read(struct flashlog_state_t *state) {
   if (!state->entrybuf)
      return FLASHLOG_ERR_NOINIT;
   int current = state->current;
   if (state->numinuse == 0
   || (state->newest >= state->oldest && (current < state->oldest || current > state->newest))
   || (state->newest < state->oldest && (current >= state->numslots || state->current < 0) || (current > state->newest && current < state->oldest)))
      return FLASHLOG_ERR_BADSLOT;
   int length = state->datasize + sizeof(struct flashlog_entry_hdr_t);
   int offset = FLASHLOG_SLOT0 + state->current * (state->datasize + sizeof(struct flashlog_entry_hdr_t));
   if ((state->partition_err = esp_partition_read(state->partition, offset, state->entrybuf, length)) != ESP_OK)
      return FLASHLOG_ERR_READERR;
   return FLASHLOG_ERR_OK; }

// routines to set state->current to a specified slot

enum flashlog_error flashlog_goto_newest(struct flashlog_state_t *state) {
   if (state->numinuse == 0) return FLASHLOG_ERR_BADSLOT;
   state->current = state->newest;
   return FLASHLOG_ERR_OK; }

enum flashlog_error flashlog_goto_oldest(struct flashlog_state_t *state) {
   if (state->numinuse == 0) return FLASHLOG_ERR_BADSLOT;
   state->current = state->oldest;
   return FLASHLOG_ERR_OK; }

enum flashlog_error flashlog_goto_next(struct flashlog_state_t *state) {
   if (state->numinuse == 0
         || state->current == state->newest)
      return FLASHLOG_ERR_BADSLOT;
   if (++state->current >= state->numslots) state->current = 0;
   return FLASHLOG_ERR_OK; }

enum flashlog_error flashlog_goto_prev(struct flashlog_state_t *state) {
   if (state->numinuse == 0
         || state->current == state->oldest)
      return FLASHLOG_ERR_BADSLOT;
   if (--state->current < 0) state->current = state->numslots - 1;
   return FLASHLOG_ERR_OK; }

//*
