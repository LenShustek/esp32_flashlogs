//file: esp32_flashlogs.ino
//
// test program for esp32_flashlogs
//
#include "esp32_flashlogs.h"
struct flashlog_state_t state;

void dprint(const char *format, ...) {
   char buf[200];
   va_list argptr;
   va_start(argptr, format);
   vsnprintf(buf, sizeof(buf), format, argptr);
   Serial.print(buf);
   va_end(argptr); }

void chkerr(int err) {
   if (err == FLASHLOG_ERR_OK) return;
   dprint("err: %d, partition err %d\n", err, state.partition_err);
   while (1) ; }

void print_entry() {
   chkerr(flashlog_read(&state)); // read the log entry state->current
   dprint("slot %d, seqno %d: %s\n", state.current, state.entrybuf->seqno, state.logdata); }

void printlog_forward(void) {
   dprint("the log in forward order: \n");
   if (flashlog_goto_oldest(&state) != FLASHLOG_ERR_OK)
      dprint("log empty\n");
   else do {
         print_entry(); }
      while (flashlog_goto_next(&state) == FLASHLOG_ERR_OK); }

void printlog_backward(void) {
   dprint("the log in backwards order: \n");
   if (flashlog_goto_newest(&state) != FLASHLOG_ERR_OK)
      dprint("log empty\n");
   else do {
         print_entry(); }
      while (flashlog_goto_prev(&state) == FLASHLOG_ERR_OK) ; }

void show_log(int cnt) {
   if (flashlog_goto_oldest(&state) != FLASHLOG_ERR_OK)
      dprint("log empty\n");
   else if (state.numinuse <= 2 * cnt) {
      printlog_forward();
      printlog_backward(); }
   else {
      dprint("showing first/last; high seq %d, numinuse %d, newest %d, oldest %d, current %d\n",
             state.highest_seqno, state.numinuse, state.newest, state.oldest, state.current);
      print_entry(); // oldest
      for (int i = 0; i < cnt - 1; ++i) {
         chkerr(flashlog_goto_next(&state)); print_entry(); } // next oldest
      dprint("...\n");
      flashlog_goto_newest(&state);
      for (int i = 0; i < cnt - 1; ++i)
         chkerr(flashlog_goto_prev(&state));
      do
         print_entry();
      while (flashlog_goto_next(&state) == FLASHLOG_ERR_OK); } }

void addline(int n, bool talk) {
   sprintf((char *)state.logdata, "v2 line %d", n);
   if (talk) dprint("\nadding: %s\n", (char *)state.logdata);
   chkerr(flashlog_add(&state)); }

void setup (void) {
   delay(1000);
   Serial.begin(115200);
   delay(1000);
   Serial.println("starting esp32_flashlogs test");
   flashlog_open(NULL, 252, &state);
   dprint("log is open: high seq %d, numinuse %d, newest %d, oldest %d\n",
          state.highest_seqno, state.numinuse, state.newest, state.oldest);
   show_log(3);

   addline(1, true);
   show_log(3);

   addline(2, true);
   show_log(3);

   dprint("\nadding until log is full...\n");
   for (int linenum = 3; state.numinuse < state.numslots; ++linenum)
      addline(linenum, false);
   dprint("log is now full, with %d entries\n", state.numinuse);
   show_log(3);

   addline(1000, true);
   show_log(3);

   addline(1001, true);
   show_log(3);

   flashlog_close(&state);
   dprint("done\n"); }

void loop (void) {}
