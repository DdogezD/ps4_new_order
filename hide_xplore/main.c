/* Hide Xplore: patch /system_data/priv/mms/app.db so the PS4-Xplorer tile
 * (LAPY20009) is hidden (visible 1 -> 0). Patches EVERY tbl_appbrowse_* table
 * (multi-account).
 *
 * Pure in-place SQLite B-tree record patch:
 *   visible serial type 9 (0-byte int 1) -> 8 (0-byte int 0), same record size.
 *   (or type 1 with value byte 0x01 -> 0x00). Then bump change counter (off 24).
 * Verified locally on a copy of the real app.db (integrity_check = ok).
 */
#include <stdarg.h>
#include "ps4.h"
#include "dump.h"

__asm__(".intel_syntax noprefix");
__asm__(".globl my_fsync");
__asm__("my_fsync:");
__asm__("movq rax, 95");
__asm__("jmp syscall_macro");
extern int my_fsync(int fd);

#define APP_DB "/system_data/priv/mms/app.db"
#define VISIBLE_COL 8

static int m_vsnprintf(char *buf, int sz, const char *fmt, va_list ap) {
  int o = 0;
  for (int i = 0; fmt[i] && o < sz - 1; i++) {
    if (fmt[i] != '%') { buf[o++] = fmt[i]; continue; }
    i++; int is_ll = 0;
    if (fmt[i] == 'l') { is_ll = 1; i++; if (fmt[i] == 'l') i++; }
    if (fmt[i] == 's') { const char *s = va_arg(ap, const char*); if (!s) s="(null)"; while (*s && o<sz-1) buf[o++]=*s++; }
    else if (fmt[i] == 'd') { int v = va_arg(ap, int); char t[24]; int tn=0; if (v<0){buf[o++]='-'; v=-v;} do{t[tn++]='0'+v%10;v/=10;}while(v); while(tn&&o<sz-1)buf[o++]=t[--tn]; }
    else if (fmt[i] == 'u') { unsigned long long v = is_ll ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned); char t[24]; int tn=0; do{t[tn++]='0'+v%10;v/=10;}while(v); while(tn&&o<sz-1)buf[o++]=t[--tn]; }
    else if (fmt[i] == 'x') { unsigned long long v = is_ll ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned); char t[24]; int tn=0; do{int d=v%16;t[tn++]=d<10?'0'+d:'a'+d-10;v/=16;}while(v); while(tn&&o<sz-1)buf[o++]=t[--tn]; }
    else if (fmt[i] == 'c') { buf[o++] = (char)va_arg(ap, int); }
    else if (fmt[i] == '%') { buf[o++] = '%'; }
  }
  buf[o] = 0; return o;
}
static void pop(const char *fmt, ...) {
  char buf[256]; va_list ap; va_start(ap, fmt);
  m_vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  printf_notification("%s", buf);
}

static int m_strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static int m_strncmp(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) { if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i]; if (!a[i]) return 0; } return 0; }
static void m_memcpy(void *d, const void *s, int n) { for (int i = 0; i < n; i++) ((char*)d)[i] = ((char*)s)[i]; }

static int read_varint(const unsigned char *p, unsigned long long *out) {
  unsigned long long v = 0; int i;
  for (i = 0; i < 8; i++) { v = (v << 7) | (p[i] & 0x7f); if (!(p[i] & 0x80)) { *out = v; return i+1; } }
  *out = (v << 8) | p[8]; return 9;
}
static int col_size(int t) {
  if (t >= 1 && t <= 4) return t;
  if (t == 5) return 6;
  if (t == 6 || t == 7) return 8;
  if (t >= 12) return (t - 12) / 2;
  return 0;
}
static int col_value_offset(const int *types, int idx) {
  int off = 0, i;
  for (i = 0; i < idx; i++) off += col_size(types[i]);
  return off;
}

static int page_sz, db_fd;
static unsigned long long file_sz;
static unsigned char page_buf[8192];

static int read_page(int pgno) {
  long off = (long)(pgno - 1) * page_sz;
  if (off < 0 || off + page_sz > (long)file_sz) return -1;
  lseek(db_fd, off, 0); read(db_fd, page_buf, page_sz); return 0;
}

/* cell parsing result */
struct cell {
  int pgno, cell_off;
  int types[48], tcount;
  int rec_file_off;      /* absolute file offset where the record starts */
  int hl;                /* record header length (bytes) */
  const unsigned char *rec; /* record pointer (into page_buf) */
};

/* parse the cell at (pgno, cell_off) into `c`. page must be already read into
 * page_buf via read_page(pgno). */
static void parse_cell_at(int pgno, int cell_off, struct cell *c) {
  unsigned long long v, hlen; int p = 0, hp;
  c->pgno = pgno; c->cell_off = cell_off;
  p += read_varint(page_buf + cell_off, &v);         /* payload len */
  p += read_varint(page_buf + cell_off + p, &v);     /* rowid */
  c->rec_file_off = (pgno - 1) * page_sz + cell_off + p;
  c->rec = page_buf + cell_off + p;
  hp = read_varint(c->rec, &hlen);                    /* header len */
  c->hl = (int)hlen;
  c->tcount = 0;
  while (hp < (int)hlen && c->tcount < 48) {
    int w = read_varint(c->rec + hp, &v);
    c->types[c->tcount++] = (int)v; hp += w;
  }
}

static int col_text(const struct cell *c, int idx, char *out, int out_sz) {
  if (idx >= c->tcount) { out[0] = 0; return -1; }
  int t = c->types[idx];
  if (t >= 13) {
    int sz = (t - 12) / 2;
    int voff = col_value_offset(c->types, idx);
    if (sz > out_sz - 1) sz = out_sz - 1;
    m_memcpy(out, c->rec + c->hl + voff, sz); out[sz] = 0; return 0;
  }
  out[0] = 0; return -1;
}

/* file offset (relative to record start) of the serial-type byte for column idx */
static int serial_type_rel(const struct cell *c, int idx) {
  unsigned long long v;
  int p = 0;
  int hdr_w = read_varint(c->rec, &v); /* header-len varint width */
  int rel = hdr_w;
  for (int i = 0; i < idx && i < c->tcount; i++) {
    int w = read_varint(c->rec + rel, &v);
    rel += w;
  }
  return rel;
}

/* walk a b-tree; visit() returns 1 to stop. */
typedef int (*visit_fn)(const struct cell *c);
static int walk(int pgno, int depth, visit_fn visit) {
  int h, t, n_cells, i;
  if (depth > 60) return 0;
  if (read_page(pgno) < 0) return 0;
  h = (pgno == 1) ? 100 : 0;
  t = page_buf[h];
  n_cells = (page_buf[h+3] << 8) | page_buf[h+4];
  if (t == 5) {
    int rightmost = (page_buf[h+8] << 24) | (page_buf[h+9] << 16) | (page_buf[h+10] << 8) | page_buf[h+11];
    for (i = 0; i < n_cells && i < 4000; i++) {
      int co = (page_buf[h+12 + i*2] << 8) | page_buf[h+12 + i*2 + 1];
      int child = (page_buf[co] << 24) | (page_buf[co+1] << 16) | (page_buf[co+2] << 8) | page_buf[co+3];
      if (child > 0) {
        if (walk(child, depth + 1, visit)) return 1;
        read_page(pgno);   /* recursion overwrote page_buf - restore current page */
      }
    }
    if (rightmost > 0 && walk(rightmost, depth + 1, visit)) return 1;
  } else if (t == 13) {
    for (i = 0; i < n_cells && i < 4000; i++) {
      int co = (page_buf[h+8 + i*2] << 8) | page_buf[h+8 + i*2 + 1];
      struct cell c; parse_cell_at(pgno, co, &c);
      if (visit(&c)) return 1;
    }
  }
  return 0;
}

/* --- find ALL tbl_appbrowse* root pages (multi-account: several tables exist) --- */
static int table_root = 0;
static int roots[8]; static int n_roots = 0;
static int find_root_visit(const struct cell *c) {
  char name[128];
  if (c->tcount > 4 && col_text(c, 1, name, sizeof(name)) == 0) {
    if (m_strncmp(name, "tbl_appbrowse", 13) == 0) {
      int t = c->types[3];
      int rp = 0;
      if (t == 9) rp = 1;
      else if (t == 8) rp = 0;
      else if (t >= 1 && t <= 6) {
        int voff = col_value_offset(c->types, 3);
        unsigned long long rv = 0;
        for (int i = 0; i < t; i++) rv = (rv << 8) | c->rec[c->hl + voff + i];
        rp = (int)rv;
      }
      if (rp > 0 && n_roots < 8) roots[n_roots++] = rp; /* collect ALL, do NOT stop */
    }
  }
  return 0; /* never stop - scan the whole sqlite_master */
}

/* --- record the visible serial-type byte's absolute file offset --- */
struct hit { int file_off; int cur_type; };
static struct hit hits[8]; static int n_hits = 0;
static int target_visit(const struct cell *c) {
  char title[64];
  if (VISIBLE_COL >= c->tcount) return 0;
  if (col_text(c, 0, title, sizeof(title)) != 0) return 0;
  if (m_strcmp(title, "LAPY20009") != 0) return 0;  /* PS4-Xplorer */
  int rel = serial_type_rel(c, VISIBLE_COL);
  int abs_off = c->rec_file_off + rel;
  unsigned char b = 0;
  lseek(db_fd, abs_off, 0); read(db_fd, &b, 1);
  if (n_hits < 8) { hits[n_hits].file_off = abs_off; hits[n_hits].cur_type = b; n_hits++; }
  return 0;
}

int _main(struct thread *td) {
  UNUSED(td);
  initKernel(); initLibc(); initSysUtil(); jailbreak();
  pop("Hide Xplore: running");

  db_fd = open(APP_DB, O_RDWR, 0);
  if (db_fd < 0) { pop("Hide Xplore: open FAILED (%d)", db_fd); return 0; }

  unsigned char hdr[100]; read(db_fd, hdr, 100);
  page_sz = (hdr[16] << 8) | hdr[17]; if (page_sz == 1) page_sz = 65536;
  file_sz = lseek(db_fd, 0, 2);
  pop("Hide Xplore: db open, page_sz=%d size=%llu", page_sz, (unsigned long long)file_sz);

  n_roots = 0;
  walk(1, 0, find_root_visit);
  pop("Hide Xplore: found %d tbl_appbrowse tables", n_roots);
  if (n_roots == 0) { pop("Hide Xplore: no tbl_appbrowse table"); close(db_fd); return 0; }

  int patched = 0;
  for (int r = 0; r < n_roots; r++) {
    table_root = roots[r];
    if (table_root <= 0 || table_root > 4096) { pop("Hide Xplore: table %d bad root %d, skip", r, table_root); continue; }
    pop("Hide Xplore: table %d root=%d", r, table_root);
    n_hits = 0;
    walk(table_root, 0, target_visit);
    pop("Hide Xplore: table %d found %d target rows", r, n_hits);
    if (n_hits < 1) { pop("Hide Xplore: table %d no LAPY, skip", r); continue; }
    int tpatched = 0;
    for (int i = 0; i < n_hits; i++) {
      int off = hits[i].file_off, cur = hits[i].cur_type;
      if (cur == 9) {
        lseek(db_fd, off, 0); { char z = 8; write(db_fd, &z, 1); }
        pop("Hide Xplore: #%d visible 9->8 @%d", i, off); tpatched++;
      } else if (cur == 1) {
        lseek(db_fd, off + 1, 0); { char z = 0; write(db_fd, &z, 1); }
        pop("Hide Xplore: #%d visible val->0 @%d", i, off + 1); tpatched++;
      } else if (cur == 8) {
        pop("Hide Xplore: #%d already 0", i); tpatched++;
      } else {
        pop("Hide Xplore: #%d unexpected type %d @%d", i, cur, off);
      }
    }
    patched += tpatched;
  }

  unsigned char cc[4];
  lseek(db_fd, 24, 0); read(db_fd, cc, 4);
  unsigned int ccv = ((cc[0] << 24) | (cc[1] << 16) | (cc[2] << 8) | cc[3]) + 1;
  cc[0] = (ccv >> 24) & 0xff; cc[1] = (ccv >> 16) & 0xff; cc[2] = (ccv >> 8) & 0xff; cc[3] = ccv & 0xff;
  lseek(db_fd, 24, 0); write(db_fd, cc, 4);

  int fs = my_fsync(db_fd);
  close(db_fd);
  pop("Hide Xplore: done patched=%d fsync=%d", patched, fs);
  return 0;
}
