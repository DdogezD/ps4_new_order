/* appdbdump v2: copy /system_data/priv/mms/app.db to USB reliably.
 * Uses the proven USB write recipe: pad to 32MB + real fsync + sync + sleep.
 * v2: ALWAYS re-dumps (no db.complete skip guard) so a fresh copy is taken
 * every run - for debugging why a tile patch finds 0 rows.
 */
#include <stdarg.h>
#include "ps4.h"
#include "dump.h"

#define PAYLOAD_VER 2
#define APP_DB "/system_data/priv/mms/app.db"

__asm__(".intel_syntax noprefix");
__asm__(".globl my_fsync"); __asm__("my_fsync:"); __asm__("movq rax, 95"); __asm__("jmp syscall_macro");
__asm__(".globl my_sync"); __asm__("my_sync:"); __asm__("movq rax, 36"); __asm__("jmp syscall_macro");
extern int my_fsync(int fd);
extern void my_sync(void);

int nthread_run = 1; int notify_time = 20; char notify_buf[512] = {0};
void *nthread_func(void *arg) {
  UNUSED(arg); time_t t1 = 0;
  while (nthread_run) {
    if (notify_buf[0]) { time_t t2 = time(NULL); if ((t2-t1)>=notify_time) { t1=t2; printf_notification("%s", notify_buf); } }
    else t1 = 0;
    sceKernelSleep(1);
  }
  return NULL;
}

int _main(struct thread *td) {
  UNUSED(td);
  char usb_name[7] = {0}, usb_path[13] = {0};
  char out_root[128] = {0}, saveFile[128] = {0}, complete[128] = {0};
  initKernel(); initLibc(); initSysUtil(); jailbreak(); initPthread();
  ScePthread nthread; memset_s(&nthread, sizeof(ScePthread), 0, sizeof(ScePthread));
  scePthreadCreate(&nthread, NULL, nthread_func, NULL, "nthread");
  printf_notification("appdbdump v%d: waiting for usb", PAYLOAD_VER);
  wait_for_usb(usb_name, usb_path); notify_buf[0] = '\0';
  printf_notification("appdbdump: usb=%s", usb_path);
  snprintf_s(out_root, sizeof(out_root), "%s/PS4", usb_path); mkdir(out_root, 0777);
  snprintf_s(out_root, sizeof(out_root), "%s/DB", out_root); mkdir(out_root, 0777);
  printf_notification("appdbdump: dirs created");
  snprintf_s(saveFile, sizeof(saveFile), "%s/app.db", out_root);
  snprintf_s(complete, sizeof(complete), "%s/db.complete", out_root);
  /* v2: no db.complete guard - always re-dump a fresh copy */
  unlink(saveFile);

  int src = open(APP_DB, O_RDONLY, 0);
  if (src < 0) { printf_notification("appdbdump: open app.db FAILED (%d)", src); return 0; }
  printf_notification("appdbdump: app.db open OK");
  int fd = open(saveFile, O_WRONLY|O_CREAT|O_TRUNC, 0777);
  if (fd < 0) { printf_notification("appdbdump: out open FAILED"); close(src); return 0; }

  notify_time = 2;
  char *buf = (char *)malloc(16384);
  long total = 0; int r;
  while ((r = read(src, buf, 16384)) > 0) {
    write(fd, buf, (size_t)r); total += r;
  }
  close(src);
  printf_notification("appdbdump: read app.db, padding to 32MB");
  /* pad to 32MB so the USB write actually flushes (dbcopy used 32MB reliably) */
  memset(buf, 0, 16384);
  while (total < 32*1024*1024) {
    long n = ((32*1024*1024 - total) > 16384) ? 16384 : (32*1024*1024 - total);
    write(fd, buf, (size_t)n); total += n;
  }
  notify_buf[0] = '\0'; nthread_run = 0;
  my_fsync(fd); close(fd);
  touch_file(complete);
  my_sync();
  sceKernelUsleep(3*1000*1000);
  printf_notification("appdbdump: done %ld bytes", total);
  return 0;
}
