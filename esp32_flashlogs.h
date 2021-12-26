/* file: esp32_flashlogs.h
   ------------------------------------------------------------------------------------
   This is a module for the ESP32 Wifi microcontroller that efficiently implements
   non-volatile FLASH memory storage of event logs. The logs are circular: when
   the log becomes full, adding the next new entry causes 4K bytes of the oldest
   entries to be removed. (FLASH can only be erased in 4K byte blocks.)
   Entries may be retrieved in either order, starting with the newest or the oldest.
   The log data will survive rebooting and FLASHing a new program.
   See the README.txt file for more information.
   -----------------------------------------------------------------------------------*/
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

#include <esp_partition.h>
#define ESP_PARTITION_TYPE_LOG (esp_partition_type_t)0x4D

// This is the flash-resident header at the beginning of the log.
// This is written when the log is created, but it does not need to be
// updated as entries are added, which limits wear on the NOR flash memory.
struct flashlog_hdr_t {
   char id[8];              //"flashlog", so we can recognize an initialized log
   int datasize;            // the size of the user data in each log entry
   int numslots; };         // the total number of slots in the log
#define FLASHLOG_ID "flashlog"
#define FLASHLOG_SLOT0 4096 // the offset in the partition where slot 0 starts

// This is the header at the start of each log entry.
// It currently only stores a sequence number that gives the absolute "age"
// of the entry. (It will wrap around and fail after 4 billion log entries,
// but the FLASH memory will probably have failed before then.)
struct flashlog_entry_hdr_t  {
   uint32_t seqno; };       // 0 for an unused entry
// Following the header are "datasize" bytes of user data

// This is the RAM-resident structure that holds the current state of the log. The
// caller allocates this as a persistent local or global variable, and passes a pointer to it
// to our API functions. It is initialized by reading the whole log when it is opened.
struct flashlog_state_t {
   const esp_partition_t *partition;      // pointer to the ESP32 partition structure for the log
   struct flashlog_entry_hdr_t *entrybuf; // ptr to a buffer that can hold a complete log entry
   void *logdata;                         // ptr to where the user data starts in that buffer
   int datasize;                          // the size of the user data in each log entry
   int numslots;                          // the total number of slots in the log
   uint32_t highest_seqno;                // highest seqno used so far in all the log entries
   int numinuse;                          // how many log slots are currently used, 0..hdr.numslots
   int newest, oldest;                    // newest and oldest slots, 0..numinuse
   int current;                           // currrent slot being read or written, 0..numinuse
   int partition_err; };                  // the last error from esp_partition_xxx routines

// These are the errors that our functions return. If an error represents
// a failure of the ESP32 partition routine we call, "partition_err" in
// the state structure will record the detailed error from it.
enum flashlog_error {
   FLASHLOG_ERR_OK,            // no error
   FLASHLOG_ERR_NO_PARTITION,  // the log FLASH partition wasn't found
   FLASHLOG_ERR_BADSIZE,       // the log entry datasize is not 4 less than a power of two
   FLASHLOG_ERR_READERR,       // can't read log
   FLASHLOG_ERR_NOINIT,        // state not initialized
   FLASHLOG_ERR_WRITEERR,      // can't write log
   FLASHLOG_ERR_ERASEERR,      // can't erase log
   FLASHLOG_ERR_NOMEM,         // memory allocation failure
   FLASHLOG_ERR_BADSLOT };     // slot wasn't in range 0..numinuse

// Open or initialize a log partition with entries of the specified size,
// which must be 4 less than a power of 2 and less than 4K, so one of these: 
// 4, 12, 28, 60, 124, 252, 508, 1020, 2044, or 4092.
// If the log is already initialized with a different entry size, the
// log is reinitialized and all the previous log entries are erased.
enum flashlog_error flashlog_open (
   const char *logname,       // if given, the partition must have this name
   // if NULL, the first partition of type ESP_PARTITION_TYPE_LOG is used
   int datasize,              // the size of the user data in each log entry
   struct flashlog_state_t *state); // where to store the ram-resident state info

// Add a new log entry using the data you put at state->logdata.
// Be careful to put no more than "datasize" bytes there!
enum flashlog_error flashlog_add (struct flashlog_state_t *state);

// Read a log entry's data into state->logdata.
// The log entry is identified by "slot number" state->current,
// which should have been set by one of the flashlog_goto_xxx calls.
enum flashlog_error flashlog_read (struct flashlog_state_t *state);

// Navigate to the oldest/newest/next/previous log entry before
// calling flashlog_read(). If there is no such entry, it
// returns FLASHLOG_ERR_BADSLOT instead of FLASHLOG_ERR_OK.
enum flashlog_error flashlog_goto_oldest(struct flashlog_state_t *);
enum flashlog_error flashlog_goto_newest(struct flashlog_state_t *);
enum flashlog_error flashlog_goto_next(struct flashlog_state_t *);
enum flashlog_error flashlog_goto_prev(struct flashlog_state_t *);

// Close the log and free the buffer that had been allocated for it.
enum flashlog_error flashlog_close(struct flashlog_state_t *state);

//*
