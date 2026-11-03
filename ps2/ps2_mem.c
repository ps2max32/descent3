/* ps2_mem.c: with MEMLOG=1 (the link wraps malloc, calloc, realloc,
   memalign and free) every block gets a header with its caller and size:
   the allocations of 64 KiB and more are logged with their caller and the
   bytes in use, and at the first failure (and at ps2_mem_report) the
   callers that hold the most. Always: the virtual memory's counters (with
   the pinned memory, libgl's textures and arenas) at ps2_mem_report. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mx/vm.h>

int mx_printf(const char *fmt, ...);

/* the VM's counters (pinned memory: libgl's textures and arenas) */
static void status(const char *when) {
  if (!mx_vm_active())
    return;
  struct mx_vm_stats v;
  mx_vm_stats(&v);
  mx_printf("mem: %s: vm: %u frames (%u free, %u pinned memory, %u pinned pages), faults %u, zero "
            "fills %u, swap in %u out %u, drops %u, file in %u; %u ms swap in, %u ms out\n",
            when, v.frames, v.free_frames, v.pinned_frames, v.pinned, v.faults, v.zero_fills,
            v.swap_ins, v.swap_outs, v.drops, v.file_ins, v.us_swap_in / 1000, v.us_swap_out / 1000);
}

#ifdef PS2_MEMLOG
void *__real_malloc(size_t n);
void *__real_memalign(size_t a, size_t n);
void __real_free(void *p);

/* 16 bytes before the block; offset: from the start of what __real_*
   returned */
struct hdr {
  void *caller;
  uint32_t size, offset, magic;
};
#define MAGIC 0x6d656d31u

static size_t in_use, peak;

#define NCALLERS 2048
static struct {
  void *caller;
  int64_t live;
  unsigned count;
} callers[NCALLERS];
static int dumped;

static int slot(void *caller) {
  unsigned h = ((unsigned)(size_t)caller >> 2) % NCALLERS;
  for (int i = 0; i < NCALLERS; i++, h = (h + 1) % NCALLERS)
    if (callers[h].caller == caller || !callers[h].caller) {
      callers[h].caller = caller;
      return (int)h;
    }
  return -1;
}

void ps2_mem_reset_peak(void) { peak = in_use; }

void ps2_mem_report(const char *when) {
  static char used[NCALLERS];
  status(when);
  memset(used, 0, sizeof used);
  mx_printf("mem: %s: %u KiB in use (peak %u); the callers that hold the most:\n", when,
            (unsigned)(in_use >> 10), (unsigned)(peak >> 10));
  for (int k = 0; k < 30; k++) {
    int best = -1;
    for (int i = 0; i < NCALLERS; i++)
      if (callers[i].caller && !used[i] && callers[i].live > 0 &&
          (best < 0 || callers[i].live > callers[best].live))
        best = i;
    if (best < 0)
      break;
    used[best] = 1;
    mx_printf("mem: live %u bytes in %u blocks by %p\n", (unsigned)callers[best].live,
              callers[best].count, callers[best].caller);
  }
}

static void *take(void *raw, size_t off, size_t n, void *caller) {
  if (!raw) {
    mx_printf("mem: %u bytes by %p FAILED, in use %u KiB\n", (unsigned)n, caller,
              (unsigned)(in_use >> 10));
    if (!dumped && n < 8 * 1024 * 1024) {
      dumped = 1;
      ps2_mem_report("first failure");
    }
    return NULL;
  }
  struct hdr *h = (struct hdr *)((char *)raw + off) - 1;
  h->caller = caller, h->size = (uint32_t)n, h->offset = (uint32_t)off, h->magic = MAGIC;
  int s = slot(caller);
  if (s >= 0)
    callers[s].live += (int64_t)n, callers[s].count++;
  in_use += n;
  if (in_use > peak) {
    if ((in_use >> 20) != (peak >> 20))
      mx_printf("mem: peak %u MiB by %p\n", (unsigned)(in_use >> 20), caller);
    peak = in_use;
  }
  if (n >= 64 * 1024)
    mx_printf("mem: %u bytes by %p, in use %u KiB\n", (unsigned)n, caller, (unsigned)(in_use >> 10));
  return h + 1;
}

static void drop(void *p) {
  struct hdr *h = (struct hdr *)p - 1;
  if (h->magic != MAGIC) {
    mx_printf("mem: free of %p without a header\n", p);
    return;
  }
  h->magic = 0;
  int s = slot(h->caller);
  if (s >= 0)
    callers[s].live -= h->size, callers[s].count--;
  in_use -= h->size;
  __real_free((char *)(h + 1) - h->offset);
}

void *__wrap_malloc(size_t n) {
  return take(__real_malloc(n + 16), 16, n, __builtin_return_address(0));
}
void *__wrap_calloc(size_t n, size_t s) {
  void *p = take(__real_malloc(n * s + 16), 16, n * s, __builtin_return_address(0));
  if (p)
    memset(p, 0, n * s);
  return p;
}
void *__wrap_memalign(size_t a, size_t n) {
  if (a < 16)
    a = 16;
  return take(__real_memalign(a, n + a), a, n, __builtin_return_address(0));
}
void *__wrap_realloc(void *p, size_t n) {
  void *q = take(__real_malloc(n + 16), 16, n, __builtin_return_address(0));
  if (p && q) {
    struct hdr *h = (struct hdr *)p - 1;
    memcpy(q, p, h->size < n ? h->size : n);
  }
  if (p && (q || !n))
    drop(p);
  return q;
}
void __wrap_free(void *p) {
  if (p)
    drop(p);
}
#else
void ps2_mem_report(const char *when) { status(when); }
void ps2_mem_reset_peak(void) {}
#endif
