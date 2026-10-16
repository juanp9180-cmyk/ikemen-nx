// Nintendo Switch (Horizon) port support.
//
// The Go runtime is compiled for linux/arm64 and every SVC instruction is
// redirected to x_nx_syscall (see runtime/nx_linux_arm64.s). This file
// emulates the subset of the Linux system call ABI that the Go runtime and
// the os/syscall packages use, on top of libnx and newlib.

#ifdef __SWITCH__

#include <switch.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <stdalign.h>

typedef unsigned long uptr;
typedef long sptr;

// ---------------------------------------------------------------------------
// Linux ABI constants (arm64).

enum {
	L_EPERM = 1, L_ENOENT = 2, L_EINTR = 4, L_EIO = 5, L_EBADF = 9,
	L_EAGAIN = 11, L_ENOMEM = 12, L_EACCES = 13, L_EFAULT = 14,
	L_EBUSY = 16, L_EEXIST = 17, L_EXDEV = 18, L_ENOTDIR = 20,
	L_EISDIR = 21, L_EINVAL = 22, L_EMFILE = 24, L_ENOSPC = 28,
	L_ESPIPE = 29, L_EROFS = 30, L_ERANGE = 34, L_ENAMETOOLONG = 36,
	L_ENOSYS = 38, L_ENOTEMPTY = 39, L_ETIMEDOUT = 110,
};

#define L_AT_FDCWD (-100)
#define L_AT_REMOVEDIR 0x200
#define L_O_CREAT 0x40
#define L_O_EXCL 0x80
#define L_O_TRUNC 0x200
#define L_O_APPEND 0x400
#define L_O_NONBLOCK 0x800
#define L_O_DIRECTORY 0x4000

#define L_PROT_NONE 0
#define L_MAP_FIXED 0x10
#define L_MAP_ANONYMOUS 0x20

#define L_FUTEX_WAIT 0
#define L_FUTEX_WAKE 1
#define L_FUTEX_CMD_MASK 0x7f

#define L_CLOCK_REALTIME 0

#define L_EPOLL_CTL_ADD 1
#define L_EPOLL_CTL_DEL 2
#define L_EPOLLIN 1

#define L_DT_DIR 4
#define L_DT_REG 8

// Fake descriptors live far above newlib's descriptor range.
#define FD_EVENT 0x4000
#define FD_EPOLL 0x4001
#define FD_DIR_BASE 0x5000
#define MAX_DIRS 64

struct linux_timespec { sptr tv_sec; sptr tv_nsec; };

struct linux_stat {
	unsigned long st_dev, st_ino;
	unsigned int st_mode, st_nlink, st_uid, st_gid;
	unsigned long st_rdev, pad1;
	long st_size;
	int st_blksize, pad2;
	long st_blocks;
	struct linux_timespec st_atim, st_mtim, st_ctim;
	int reserved[2];
};

struct linux_epoll_event { unsigned int events; unsigned int pad; unsigned long data; };

static sptr err(void) {
	switch (errno) {
	case ENOSYS: return -L_ENOSYS;
	case ENOTEMPTY: return -L_ENOTEMPTY;
	case ENAMETOOLONG: return -L_ENAMETOOLONG;
	case ETIMEDOUT: return -L_ETIMEDOUT;
	case 0: return -L_EIO;
	default:
		// newlib and Linux agree on the classic errno values (1-34).
		if (errno > 0 && errno <= 34) return -errno;
		return -L_EIO;
	}
}

// ---------------------------------------------------------------------------
// Thread pointer. libnx keeps the ELF TLS pointer in its thread vars and
// leaves TPIDR_EL0 alone, but Go reads TPIDR_EL0 directly. Mirror it on
// every thread before Go code can run there.

void* __aarch64_read_tp(void);

static void nx_set_tpidr(void) {
	uptr tp = (uptr)__aarch64_read_tp();
	__asm__ volatile("msr tpidr_el0, %0" :: "r"(tp));
}

typedef struct { ThreadFunc entry; void *arg; } nx_thunk_args;

static void nx_thread_thunk(void *p) {
	nx_thunk_args a = *(nx_thunk_args*)p;
	free(p);
	nx_set_tpidr();
	a.entry(a.arg);
}

Result __real_threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stack_mem, size_t stack_sz, int prio, int cpuid);

// Linked with -Wl,--wrap=threadCreate so that every thread (pthreads, SDL,
// audio, Go's own Ms) gets TPIDR_EL0 set up.
Result __wrap_threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stack_mem, size_t stack_sz, int prio, int cpuid) {
	nx_thunk_args *a = malloc(sizeof *a);
	if (!a) return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
	a->entry = entry;
	a->arg = arg;
	Result rc = __real_threadCreate(t, nx_thread_thunk, a, stack_mem, stack_sz, prio, cpuid);
	if (R_FAILED(rc)) free(a);
	return rc;
}

static void nx_threadentry(void) {
	nx_set_tpidr();
	// Let the kernel spread Go threads over the three application cores.
	svcSetThreadCoreMask(CUR_THREAD_HANDLE, -3, 0x7);
}

static void nx_inittls(void **tlsg, void **tlsbase) {
	(void)tlsg; (void)tlsbase;
	nx_set_tpidr();
}

// Hooks declared weak in gcc_unix.c.
void (*x_cgo_threadentry_platform)(void) = nx_threadentry;
void (*x_cgo_inittls)(void **tlsg, void **tlsbase) = nx_inittls;

__attribute__((constructor(101)))
static void nx_main_thread_init(void) {
	nx_set_tpidr();
}

// ---------------------------------------------------------------------------
// argv/envp/auxv handed to the runtime (see _rt0_arm64_lib).

static unsigned char nx_random16[16];

uptr x_nx_argv[] = {
	0,                  // argc (skipped)
	0,                  // argv terminator
#ifndef NX_GODEBUG_EXTRA
#define NX_GODEBUG_EXTRA ""
#endif
	(uptr)"GODEBUG=asyncpreemptoff=1,cgocheck=0" NX_GODEBUG_EXTRA,
#ifdef NX_GO_EXTRA_ENV
	(uptr)NX_GO_EXTRA_ENV,
#endif
#ifdef NX_GO_EXTRA_ENV2
	(uptr)NX_GO_EXTRA_ENV2,
#endif
	(uptr)"HOME=/switch/ikemen",
	0,                  // envp terminator
	6, 4096,            // AT_PAGESZ
	16, (1<<0)|(1<<1)|(1<<3)|(1<<4)|(1<<5)|(1<<6)|(1<<7), // AT_HWCAP: fp asimd aes pmull sha1 sha2 crc32
	25, (uptr)nx_random16, // AT_RANDOM
	0, 0,               // AT_NULL
};

__attribute__((constructor(102)))
static void nx_random_init(void) {
	randomGet(nx_random16, sizeof nx_random16);
}

// ---------------------------------------------------------------------------
// Memory. Go wants a large virtual address space that it reserves and later
// commits. Horizon applications only get a single heap, so a large block is
// taken from it once and handed out with page granularity.

// Bytes reserved for the Go heap. Override by defining nx_go_heap_bytes in
// the application; 0 means 40% of the free heap, at most 1 GiB.
__attribute__((weak)) size_t nx_go_heap_bytes = 0;

#define NX_PAGE 4096UL
static Mutex mem_lock;
static unsigned char *mem_base;
static uptr mem_pages;
static unsigned long *mem_used; // bitmap, 1 = page in use

static int page_used(uptr i) { return (mem_used[i / 64] >> (i % 64)) & 1; }

// Zero memory with plain stores. newlib's memset uses DC ZVA for large
// blocks, which does not reliably clear memory on Horizon emulators, and Go
// requires fresh mappings to read as zero. p and n are 64-byte aligned.
static void zero_pages(void *p, size_t n) {
	unsigned char *q = p, *end = q + n;
	for (; q < end; q += 64) {
		__asm__ volatile(
			"stp xzr, xzr, [%0]\n\tstp xzr, xzr, [%0, #16]\n\t"
			"stp xzr, xzr, [%0, #32]\n\tstp xzr, xzr, [%0, #48]"
			:: "r"(q) : "memory");
	}
}

static void mark_pages(uptr first, uptr n, int used) {
	for (uptr i = first; i < first + n; i++) {
		if (used) mem_used[i / 64] |= 1UL << (i % 64);
		else mem_used[i / 64] &= ~(1UL << (i % 64));
	}
}

static void log_write(const void *p, size_t n);

static int mem_init(void) {
	if (mem_base) return 1;
	size_t want = nx_go_heap_bytes;
	// libnx gives the whole heap to newlib at startup: what is left is
	// the unused part of the sbrk range plus malloc's free list.
	extern char *fake_heap_end;
	struct mallinfo mi = mallinfo();
	char *cur = sbrk(0);
	size_t freeb = (size_t)mi.fordblks + (cur != (char*)-1 && fake_heap_end > cur ? (size_t)(fake_heap_end - cur) : 0);
	if (want == 0) {
		// Leave most of the heap to Mesa: GPU textures and buffers come
		// from the same heap, and a fight needs far more than a menu.
		want = freeb / 10 * 4;
		if (want > (1UL << 30)) want = 1UL << 30;
	}
	want &= ~((size_t)(64 << 20) - 1);
	while (want >= (64UL << 20)) {
		mem_base = memalign(64 << 20, want);
		if (mem_base) break;
		want -= 64UL << 20;
	}
	{
		u64 total = 0, used = 0;
		svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
		svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
		char msg[160];
		int k = snprintf(msg, sizeof msg,
			"nx: memoria total=%lu MB usada=%lu MB heap libre=%lu MB go=%lu MB applet=%d\n",
			(unsigned long)(total >> 20), (unsigned long)(used >> 20),
			(unsigned long)(freeb >> 20), (unsigned long)(mem_base ? want >> 20 : 0),
			(int)appletGetAppletType());
		log_write(msg, k);
	}
	if (!mem_base) return 0;
	zero_pages(mem_base, want);
	mem_pages = want / NX_PAGE;
	mem_used = calloc((mem_pages + 63) / 64, sizeof(unsigned long));
	return mem_used != NULL;
}

static void log_write(const void *p, size_t n);

static int range_free(uptr first, uptr n) {
	if (first + n > mem_pages) return 0;
	for (uptr i = first; i < first + n; i++)
		if (page_used(i)) return 0;
	return 1;
}

void* mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
	(void)prot; (void)off;
	if (fd != -1 || !(flags & L_MAP_ANONYMOUS) || len == 0) {
		errno = ENODEV;
		return (void*)-1;
	}
#ifdef NX_SIMPLE_MMAP
	// Debug allocator: every reservation is a fresh heap block, never reused.
	if (flags & L_MAP_FIXED) return addr;
	{
		size_t sz = (len + NX_PAGE - 1) & ~(NX_PAGE - 1);
		void *p = memalign(NX_PAGE, sz);
		if (!p) { errno = ENOMEM; return (void*)-1; }
		zero_pages(p, sz);
		return p;
	}
#endif
	uptr n = (len + NX_PAGE - 1) / NX_PAGE;
	void *ret = (void*)-1;
	mutexLock(&mem_lock);
	if (!mem_init()) {
		errno = ENOMEM;
		goto out;
	}
	uptr a = (uptr)addr;
	int inside = a >= (uptr)mem_base && a < (uptr)mem_base + mem_pages * NX_PAGE && (a % NX_PAGE) == 0;
	if (flags & L_MAP_FIXED) {
		// Commit of a range Go reserved earlier.
		if (!inside || (a - (uptr)mem_base) / NX_PAGE + n > mem_pages) {
			errno = ENOMEM;
			goto out;
		}
		mark_pages((a - (uptr)mem_base) / NX_PAGE, n, 1);
		ret = addr;
		goto out;
	}
	if (inside && range_free((a - (uptr)mem_base) / NX_PAGE, n)) {
		mark_pages((a - (uptr)mem_base) / NX_PAGE, n, 1);
		ret = addr;
		goto out;
	}
	// First fit.
	for (uptr i = 0; i + n <= mem_pages;) {
		if (mem_used[i / 64] == ~0UL && i % 64 == 0) { i += 64; continue; }
		if (page_used(i)) { i++; continue; }
		uptr j = i;
		while (j < i + n && !page_used(j)) j++;
		if (j == i + n) {
			mark_pages(i, n, 1);
			ret = mem_base + i * NX_PAGE;
			goto out;
		}
		i = j + 1;
	}
	errno = ENOMEM;
out:
#ifdef NX_CHECK_ZERO
	// Debug: Go requires fresh mappings to read as zero.
	if (ret != (void*)-1) {
		const unsigned long *w = ret, *e = (const unsigned long*)((char*)ret + n * NX_PAGE);
		for (; w < e; w++) {
			if (*w) {
				char msg[128];
				int k = snprintf(msg, sizeof msg, "nx: mmap(%p,%#lx,flags=%#x) -> %p NOT ZERO at %p: %#lx\n",
					addr, (unsigned long)len, flags, ret, (void*)w, *w);
				log_write(msg, k);
				break;
			}
		}
		char msg[128];
		int k = snprintf(msg, sizeof msg, "nx: mmap(%p,%#lx,%#x) -> %p\n", addr, (unsigned long)len, flags, ret);
		log_write(msg, k);
	}
#endif
	mutexUnlock(&mem_lock);
	return ret;
}

int munmap(void *addr, size_t len) {
#ifdef NX_SIMPLE_MMAP
	return 0; // leak: blocks may be partially unmapped
#endif
	uptr a = (uptr)addr;
	mutexLock(&mem_lock);
	if (mem_base && a >= (uptr)mem_base && a < (uptr)mem_base + mem_pages * NX_PAGE) {
		uptr first = (a - (uptr)mem_base) / NX_PAGE;
		uptr n = (len + NX_PAGE - 1) / NX_PAGE;
		if (first + n > mem_pages) n = mem_pages - first;
		// Freed pages must read as zero when mapped again.
		zero_pages(mem_base + first * NX_PAGE, n * NX_PAGE);
		mark_pages(first, n, 0);
	}
	mutexUnlock(&mem_lock);
	return 0;
}

// ---------------------------------------------------------------------------
// Files. Plain newlib descriptors are used directly; directories get fake
// descriptors so getdents64 can be served from readdir.

typedef struct {
	DIR *dir;
	char *path;
	// Entry read but not returned because the caller's buffer was full.
	// Kept here instead of seekdir(): on libnx that reopens the directory
	// and rereads it from the start, which is quadratic on big folders.
	int has_pend;
	unsigned char pend_type;
	char pend_name[NAME_MAX + 1];
} nx_dir;
static nx_dir dirs[MAX_DIRS];
static Mutex dir_lock;

static nx_dir* get_dir(sptr fd) {
	if (fd < FD_DIR_BASE || fd >= FD_DIR_BASE + MAX_DIRS) return NULL;
	nx_dir *d = &dirs[fd - FD_DIR_BASE];
	return d->dir ? d : NULL;
}

// fix_path turns p into an absolute, normalized "device:/a/b" path. The real
// Horizon FS service rejects paths with "//" or "." segments that emulators
// accept, and libnx passes relative paths through mostly unchanged. The
// result lives in a small per-thread ring of buffers.
#define NX_PATH_MAX 1024
static __thread char path_ring[4][NX_PATH_MAX];
static __thread unsigned path_ring_idx;

static const char* fix_path(const char *p) {
	char tmp[NX_PATH_MAX * 2];
	if (strchr(p, ':')) {
		snprintf(tmp, sizeof tmp, "%s", p);
	} else if (p[0] == '/') {
		snprintf(tmp, sizeof tmp, "sdmc:%s", p);
	} else {
		char cwd[NX_PATH_MAX];
		if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "sdmc:/");
		snprintf(tmp, sizeof tmp, "%s/%s", cwd, p);
	}
	char *out = path_ring[path_ring_idx++ % 4];
	char *colon = strchr(tmp, ':');
	size_t dev = colon ? (size_t)(colon - tmp) + 1 : 0;
	if (dev >= NX_PATH_MAX - 2) return p;
	memcpy(out, tmp, dev);
	size_t n = dev;
	out[n++] = '/';
	// Copy segments, dropping empty and "." ones and resolving "..".
	const char *s = tmp + dev;
	while (*s) {
		while (*s == '/') s++;
		const char *e = s;
		while (*e && *e != '/') e++;
		size_t len = e - s;
		if (len == 0 || (len == 1 && s[0] == '.')) {
			// skip
		} else if (len == 2 && s[0] == '.' && s[1] == '.') {
			if (n > dev + 1) {
				n--; // drop trailing '/'
				while (n > dev + 1 && out[n - 1] != '/') n--;
			}
		} else {
			if (n + len + 1 >= NX_PATH_MAX) return p;
			memcpy(out + n, s, len);
			n += len;
			out[n++] = '/';
		}
		s = e;
	}
	if (n > dev + 1) n--; // no trailing '/' except for the root
	out[n] = 0;
	return out;
}

static void fill_stat(struct linux_stat *ls, const struct stat *st) {
	memset(ls, 0, sizeof *ls);
	ls->st_dev = st->st_dev;
	ls->st_ino = st->st_ino;
	ls->st_mode = st->st_mode;
	ls->st_nlink = st->st_nlink ? st->st_nlink : 1;
	ls->st_size = st->st_size;
	ls->st_blksize = 512;
	ls->st_blocks = (st->st_size + 511) / 512;
	ls->st_atim.tv_sec = st->st_atime;
	ls->st_mtim.tv_sec = st->st_mtime;
	ls->st_ctim.tv_sec = st->st_ctime;
}

// ---------------------------------------------------------------------------
// Read cache for read-only files. Every read() is an IPC round trip to the
// Horizon FS service, and Ikemen parses sprite files with many tiny reads
// and seeks (around ten per sprite while scanning for portraits), which made
// startup with a large roster take tens of minutes. Small reads are served
// from a per-descriptor block; the file position is tracked here and the
// real descriptor is always positioned explicitly before a real read.

#define RC_FDS 256
#define RC_BLOCK 4096

typedef struct {
	Mutex lock;
	off_t pos;   // logical file position
	off_t base;  // file offset of buf[0]
	size_t len;  // valid bytes in buf
	unsigned char buf[RC_BLOCK];
} nx_rcache;

static nx_rcache *rcache[RC_FDS];

static nx_rcache* rc_get(sptr fd) {
	return (fd >= 0 && fd < RC_FDS) ? rcache[fd] : NULL;
}

// rc_pread reads n bytes at off, through the cache.
static sptr rc_pread(nx_rcache *c, int fd, unsigned char *dst, size_t n, off_t off) {
	size_t done = 0;
	while (done < n) {
		off_t at = off + done;
		if (c->len && at >= c->base && at < c->base + (off_t)c->len) {
			size_t k = c->base + c->len - at;
			if (k > n - done) k = n - done;
			memcpy(dst + done, c->buf + (at - c->base), k);
			done += k;
			continue;
		}
		if (lseek(fd, at, SEEK_SET) < 0) return done ? (sptr)done : err();
		if (n - done >= RC_BLOCK) {
			// Large read: straight into the caller's buffer.
			ssize_t r = read(fd, dst + done, n - done);
			if (r < 0) return done ? (sptr)done : err();
			done += r;
			break;
		}
		ssize_t r = read(fd, c->buf, RC_BLOCK);
		if (r < 0) return done ? (sptr)done : err();
		c->base = at;
		c->len = r;
		if (r == 0) break; // end of file
	}
	return done;
}

static sptr rc_lseek(nx_rcache *c, int fd, off_t off, int whence) {
	off_t np;
	if (whence == SEEK_SET) {
		np = off;
	} else if (whence == SEEK_CUR) {
		np = c->pos + off;
	} else {
		struct stat st;
		if (fstat(fd, &st) < 0) return err();
		np = st.st_size + off;
	}
	if (np < 0) return -L_EINVAL;
	c->pos = np;
	return np;
}

static sptr sys_openat(sptr dirfd, const char *path, sptr lflags, sptr mode) {
	if (dirfd != L_AT_FDCWD && path[0] != '/' && !strchr(path, ':')) return -L_ENOSYS;
	path = fix_path(path);
	struct stat st;
	int isdir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
	if (isdir || (lflags & L_O_DIRECTORY)) {
		if (!isdir) return -L_ENOTDIR;
		DIR *d = opendir(path);
		if (!d) return err();
		mutexLock(&dir_lock);
		for (int i = 0; i < MAX_DIRS; i++) {
			if (!dirs[i].dir) {
				dirs[i].dir = d;
				dirs[i].path = strdup(path);
				dirs[i].has_pend = 0;
				mutexUnlock(&dir_lock);
				return FD_DIR_BASE + i;
			}
		}
		mutexUnlock(&dir_lock);
		closedir(d);
		return -L_EMFILE;
	}
	int flags = lflags & 3;
	if (lflags & L_O_CREAT) flags |= O_CREAT;
	if (lflags & L_O_EXCL) flags |= O_EXCL;
	if (lflags & L_O_TRUNC) flags |= O_TRUNC;
	if (lflags & L_O_APPEND) flags |= O_APPEND;
	int fd = open(path, flags, (int)mode);
	if (fd < 0) return err();
	if ((lflags & 3) == 0 && fd < RC_FDS) {
		nx_rcache *c = calloc(1, sizeof *c);
		if (c) {
			free(rcache[fd]);
			rcache[fd] = c;
		}
	}
	return fd;
}

static sptr sys_close(sptr fd) {
	if (fd == FD_EVENT || fd == FD_EPOLL) return 0;
	nx_dir *d = get_dir(fd);
	if (d) {
		mutexLock(&dir_lock);
		closedir(d->dir);
		free(d->path);
		d->dir = NULL;
		d->path = NULL;
		mutexUnlock(&dir_lock);
		return 0;
	}
	if (fd >= 0 && fd < RC_FDS && rcache[fd]) {
		free(rcache[fd]);
		rcache[fd] = NULL;
	}
	return close(fd) < 0 ? err() : 0;
}

static sptr sys_getdents64(sptr fd, unsigned char *buf, sptr count) {
	nx_dir *d = get_dir(fd);
	if (!d) return -L_ENOTDIR;
	sptr n = 0;
	for (;;) {
		const char *name;
		unsigned char type;
		if (d->has_pend) {
			name = d->pend_name;
			type = d->pend_type;
		} else {
			struct dirent *e = readdir(d->dir);
			if (!e) break;
			name = e->d_name;
			type = e->d_type == DT_DIR ? L_DT_DIR : L_DT_REG;
		}
		size_t namelen = strlen(name);
		size_t reclen = (19 + namelen + 1 + 7) & ~7UL;
		if (n + (sptr)reclen > count) {
			if (!d->has_pend) {
				memcpy(d->pend_name, name, namelen + 1);
				d->pend_type = type;
				d->has_pend = 1;
			}
			if (n == 0) return -L_EINVAL;
			break;
		}
		d->has_pend = 0;
		unsigned char *r = buf + n;
		memset(r, 0, reclen);
		*(unsigned long*)(r + 0) = 1 + n;  // d_ino, must be non-zero
		*(long*)(r + 8) = n + reclen;       // d_off
		*(unsigned short*)(r + 16) = reclen;
		r[18] = type;
		memcpy(r + 19, name, namelen + 1);
		n += reclen;
	}
	return n;
}

static sptr sys_fstatat(sptr dirfd, const char *path, struct linux_stat *ls, sptr flags) {
	(void)flags;
	struct stat st;
	if (path == NULL || path[0] == 0) {
		// fstat via AT_EMPTY_PATH
		nx_dir *d = get_dir(dirfd);
		if (d) {
			if (stat(d->path, &st) < 0) return err();
		} else if (fstat(dirfd, &st) < 0) {
			return err();
		}
	} else {
		if (dirfd != L_AT_FDCWD && path[0] != '/' && !strchr(path, ':')) return -L_ENOSYS;
		const char *fp = fix_path(path);
		if (stat(fp, &st) < 0) {
			int e = errno;
			if (e != ENOENT) {
				char msg[NX_PATH_MAX + 64];
				int k = snprintf(msg, sizeof msg, "nx: stat(%s) failed, errno %d\n", fp, e);
				log_write(msg, k);
			}
			errno = e;
			return err();
		}
	}
	fill_stat(ls, &st);
	return 0;
}

static sptr sys_fstat(sptr fd, struct linux_stat *ls) {
	return sys_fstatat(fd, "", ls, 0);
}

// ---------------------------------------------------------------------------
// Standard output. Go writes panics and prints to fds 1 and 2; keep a copy in
// a log file next to the executable since there is no console.

static FILE *log_file;
static Mutex log_lock;

static void log_write(const void *p, size_t n) {
	mutexLock(&log_lock);
	if (!log_file) log_file = fopen("go_stdout.log", "a");
	if (log_file) {
		fwrite(p, 1, n, log_file);
		fflush(log_file);
	}
	mutexUnlock(&log_lock);
}

// nx_log_close closes the Go log so the application can reopen it (Horizon
// does not let a file open for writing be opened again for reading). A
// later write reopens it in append mode.
void nx_log_close(void) {
	mutexLock(&log_lock);
	if (log_file) {
		fclose(log_file);
		log_file = NULL;
	}
	mutexUnlock(&log_lock);
}

// ---------------------------------------------------------------------------
// eventfd + epoll: just enough for the runtime network poller with no
// pollable descriptors. Regular files are rejected with EPERM so Go falls
// back to blocking I/O for them.

static u32 event_count;
static unsigned long event_data;
static int event_registered;

static sptr sys_epoll_ctl(sptr epfd, sptr op, sptr fd, struct linux_epoll_event *ev) {
	(void)epfd;
	if (fd != FD_EVENT) return -L_EPERM;
	if (op == L_EPOLL_CTL_ADD) {
		event_data = ev->data;
		event_registered = 1;
	} else if (op == L_EPOLL_CTL_DEL) {
		event_registered = 0;
	}
	return 0;
}

static sptr sys_epoll_pwait(struct linux_epoll_event *evs, sptr maxevents, sptr timeout_ms) {
	if (maxevents <= 0) return -L_EINVAL;
	if (__atomic_load_n(&event_count, __ATOMIC_ACQUIRE) == 0 && timeout_ms != 0) {
		s64 ns = timeout_ms < 0 ? -1 : timeout_ms * 1000000LL;
		svcWaitForAddress(&event_count, ArbitrationType_WaitIfEqual, 0, ns);
	}
	if (__atomic_load_n(&event_count, __ATOMIC_ACQUIRE) != 0 && event_registered) {
		evs[0].events = L_EPOLLIN;
		evs[0].data = event_data;
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------

// Last system calls, for crash reports.
static sptr trace_nr[32];
static unsigned trace_pos;

static void dump_trace(FILE *f) {
	fprintf(f, "ultimas syscalls:");
	for (unsigned i = 0; i < 32; i++) fprintf(f, " %ld", (long)trace_nr[(trace_pos + i) % 32]);
	fprintf(f, "\n");
}

// Keep the message on screen until + is pressed; the console is gone otherwise.
static void wait_plus(void) {
	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	printf("\nPulsa + para salir.\n");
	while (appletMainLoop()) {
		consoleUpdate(NULL);
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
		svcSleepThread(16000000);
	}
}

// Optional application hook, e.g. to show the log on screen before exiting.
__attribute__((weak)) void nx_on_exit(int code);

static void exit_process(int code) {
	if (log_file) {
		if (code != 0) dump_trace(log_file);
		fflush(log_file);
	}
	if (nx_on_exit) {
		nx_on_exit(code);
		exit(code);
	}
	if (code != 0) {
		printf("\n[nx] Go termino con codigo %d\n", code);
		dump_trace(stdout);
		if (log_file) dump_trace(log_file);
		wait_plus();
	}
	if (log_file) fflush(log_file);
	exit(code);
}

// Crash handler (libnx). Runs on its own stack.
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
extern char _start[]; // module base (libnx's __start__ is an absolute symbol)

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
	uptr base = (uptr)_start;
	FILE *f = fopen("go_crash.log", "w");
	FILE *outs[2] = { stdout, f };
	for (int k = 0; k < 2; k++) {
		FILE *o = outs[k];
		if (!o) continue;
		fprintf(o, "\n*** CRASH tipo 0x%x ***\n", ctx->error_desc);
		fprintf(o, "pc=%lx (elf+%lx) lr=%lx (elf+%lx)\n", (uptr)ctx->pc.x, (uptr)ctx->pc.x - base, (uptr)ctx->lr.x, (uptr)ctx->lr.x - base);
		fprintf(o, "sp=%lx far=%lx\n", (uptr)ctx->sp.x, (uptr)ctx->far.x);
		for (int i = 0; i < 29; i++)
			fprintf(o, "x%-2d=%016lx%s", i, (uptr)ctx->cpu_gprs[i].x, i % 3 == 2 ? "\n" : " ");
		fprintf(o, "\n");
		dump_trace(o);
	}
	if (f) fclose(f);
	wait_plus();
	exit(1);
}

static sptr dispatch(sptr nr, sptr a0, sptr a1, sptr a2, sptr a3, sptr a4, sptr a5) {
	(void)a5;
	trace_nr[__atomic_fetch_add(&trace_pos, 1, __ATOMIC_RELAXED) % 32] = nr;
	switch (nr) {
	case 63: // read
		if (a0 == FD_EVENT) {
			if ((sptr)a2 < 8) return -L_EINVAL;
			u32 v = __atomic_exchange_n(&event_count, 0, __ATOMIC_ACQ_REL);
			if (v == 0) return -L_EAGAIN;
			*(unsigned long*)a1 = v;
			return 8;
		}
		{
			nx_rcache *c = rc_get(a0);
			if (c) {
				mutexLock(&c->lock);
				sptr r = rc_pread(c, a0, (unsigned char*)a1, a2, c->pos);
				if (r > 0) c->pos += r;
				mutexUnlock(&c->lock);
				return r;
			}
			sptr r = read(a0, (void*)a1, a2); return r < 0 ? err() : r;
		}
	case 64: // write
		if (a0 == FD_EVENT) {
			__atomic_add_fetch(&event_count, 1, __ATOMIC_ACQ_REL);
			svcSignalToAddress(&event_count, SignalType_Signal, 0, -1);
			return 8;
		}
		if (a0 == 1 || a0 == 2) {
			log_write((void*)a1, a2);
			write(a0, (void*)a1, a2);
			return a2;
		}
		{ sptr r = write(a0, (void*)a1, a2); return r < 0 ? err() : r; }
	case 67: // pread64
		{
			nx_rcache *c = rc_get(a0);
			if (c) {
				mutexLock(&c->lock);
				sptr r = rc_pread(c, a0, (unsigned char*)a1, a2, a3);
				mutexUnlock(&c->lock);
				return r;
			}
		}
		{ off_t cur = lseek(a0, 0, SEEK_CUR); if (cur < 0 || lseek(a0, a3, SEEK_SET) < 0) return err();
		  sptr r = read(a0, (void*)a1, a2); int e = errno; lseek(a0, cur, SEEK_SET); errno = e; return r < 0 ? err() : r; }
	case 68: // pwrite64
		{ off_t cur = lseek(a0, 0, SEEK_CUR); if (cur < 0 || lseek(a0, a3, SEEK_SET) < 0) return err();
		  sptr r = write(a0, (void*)a1, a2); int e = errno; lseek(a0, cur, SEEK_SET); errno = e; return r < 0 ? err() : r; }
	case 56: return sys_openat(a0, (const char*)a1, a2, a3);
	case 57: return sys_close(a0);
	case 61: return sys_getdents64(a0, (unsigned char*)a1, a2);
	case 62: // lseek
		{ nx_dir *d = get_dir(a0); if (d) { if (a1 == 0 && a2 == SEEK_SET) { rewinddir(d->dir); d->has_pend = 0; return 0; } return -L_ESPIPE; }
		  nx_rcache *c = rc_get(a0);
		  if (c) { mutexLock(&c->lock); sptr r = rc_lseek(c, a0, a1, a2); mutexUnlock(&c->lock); return r; }
		  off_t r = lseek(a0, a1, a2); return r < 0 ? err() : r; }
	case 79: return sys_fstatat(a0, (const char*)a1, (struct linux_stat*)a2, a3);
	case 80: return sys_fstat(a0, (struct linux_stat*)a1);
	case 34: // mkdirat
		return mkdir(fix_path((const char*)a1), a2) < 0 ? err() : 0;
	case 35: // unlinkat
		if (a2 & L_AT_REMOVEDIR) return rmdir(fix_path((const char*)a1)) < 0 ? err() : 0;
		return unlink(fix_path((const char*)a1)) < 0 ? err() : 0;
	case 38: case 276: // renameat, renameat2
		return rename(fix_path((const char*)a1), fix_path((const char*)a3)) < 0 ? err() : 0;
	case 48: case 439: // faccessat, faccessat2
		{ struct stat st; return stat(fix_path((const char*)a1), &st) < 0 ? err() : 0; }
	case 17: // getcwd
		return getcwd((char*)a0, a1) ? (sptr)strlen((char*)a0) + 1 : err();
	case 49: return chdir(fix_path((const char*)a0)) < 0 ? err() : 0;
	case 46: return ftruncate(a0, a1) < 0 ? err() : 0;
	case 82: case 83: return fsync(a0) < 0 ? err() : 0;
	case 25: // fcntl
		if (a1 == 3) return 2; // F_GETFL: O_RDWR
		return 0;
	case 29: return -L_ENOSYS; // ioctl (isatty etc.)
	case 78: return -L_EINVAL; // readlinkat: no symlinks
	case 88: case 53: case 54: return 0; // utimensat, fchmodat, fchownat

	case 98: { // futex
		u32 *addr = (u32*)a0;
		switch (a1 & L_FUTEX_CMD_MASK) {
		case L_FUTEX_WAIT: {
			s64 ns = -1;
			if (a3) {
				struct linux_timespec *ts = (struct linux_timespec*)a3;
				ns = ts->tv_sec * 1000000000LL + ts->tv_nsec;
			}
			Result rc = svcWaitForAddress(addr, ArbitrationType_WaitIfEqual, (s32)a2, ns);
			if (R_SUCCEEDED(rc)) return 0;
			if (R_VALUE(rc) == KERNELRESULT(TimedOut)) return -L_ETIMEDOUT;
			return -L_EAGAIN;
		}
		case L_FUTEX_WAKE: {
			s32 cnt = (a2 <= 0 || a2 >= 0x7fffffff) ? -1 : (s32)a2;
			svcSignalToAddress(addr, SignalType_Signal, 0, cnt);
			return 0;
		}
		}
		return -L_ENOSYS;
	}
	case 124: // sched_yield
		svcSleepThread(YieldType_WithoutCoreMigration);
		return 0;
	case 101: case 115: { // nanosleep, clock_nanosleep
		struct linux_timespec *ts = (struct linux_timespec*)(nr == 101 ? a0 : a2);
		s64 ns = ts->tv_sec * 1000000000LL + ts->tv_nsec;
		svcSleepThread(ns > 0 ? ns : YieldType_WithoutCoreMigration);
		return 0;
	}
	case 113: { // clock_gettime
		struct linux_timespec *ts = (struct linux_timespec*)a1;
		if (a0 == L_CLOCK_REALTIME) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			ts->tv_sec = tv.tv_sec;
			ts->tv_nsec = tv.tv_usec * 1000;
		} else {
			u64 ns = armTicksToNs(armGetSystemTick());
			ts->tv_sec = ns / 1000000000ULL;
			ts->tv_nsec = ns % 1000000000ULL;
		}
		return 0;
	}
	case 114: { // clock_getres
		if (a1) { struct linux_timespec *ts = (struct linux_timespec*)a1; ts->tv_sec = 0; ts->tv_nsec = 1; }
		return 0;
	}
	case 169: { // gettimeofday
		struct timeval tv;
		gettimeofday(&tv, NULL);
		if (a0) { sptr *p = (sptr*)a0; p[0] = tv.tv_sec; p[1] = tv.tv_usec; }
		return 0;
	}
	case 278: // getrandom
		randomGet((void*)a0, a1);
		return a1;

	case 222: { // mmap
		void *p = mmap((void*)a0, a1, a2, a3, a4, a5);
		return p == (void*)-1 ? -L_ENOMEM : (sptr)p;
	}
	case 215: return munmap((void*)a0, a1);
	case 226: case 233: return 0; // mprotect, madvise
	case 232: return -L_ENOSYS;   // mincore

	case 123: // sched_getaffinity: three application cores
		if (a1 < 8) return -L_EINVAL;
		memset((void*)a2, 0, a1);
		*(unsigned char*)a2 = 0x7;
		return 8;
	case 172: case 173: return 1;   // getpid, getppid
	case 178: { // gettid
		u64 id = 0;
		svcGetThreadId(&id, CUR_THREAD_HANDLE);
		return (sptr)(id & 0x7fffffff);
	}
	case 174: case 175: case 176: case 177: return 0; // uids
	case 160: { // uname
		char *u = (char*)a0;
		memset(u, 0, 65 * 6);
		strcpy(u, "Linux");
		strcpy(u + 65, "switch");
		strcpy(u + 130, "5.10.0-horizon");
		strcpy(u + 195, "#1");
		strcpy(u + 260, "aarch64");
		return 0;
	}

	// Signals do not exist on Horizon.
	case 134: // rt_sigaction
		if (a2) memset((void*)a2, 0, 32);
		return 0;
	case 135: // rt_sigprocmask
		if (a2) memset((void*)a2, 0, a3);
		return 0;
	case 132: // sigaltstack
		if (a1) { sptr *o = (sptr*)a1; o[0] = 0; o[1] = 2 /* SS_DISABLE */; o[2] = 0; }
		return 0;
	case 129: case 130: case 131: { // kill, tkill, tgkill
		sptr sig = nr == 131 ? a2 : a1;
		if (sig == 0 || sig == 23 /* SIGURG */ || sig == 27 /* SIGPROF */) return 0;
		char msg[64];
		int n = snprintf(msg, sizeof msg, "\nfatal signal %ld, exiting\n", (long)sig);
		log_write(msg, n);
		exit_process(128 + (int)sig);
		return 0;
	}
	case 103: case 107: case 110: case 111: return 0; // itimers, posix timers
	case 93: case 94: // exit, exit_group
		exit_process((int)a0);
		return 0;

	case 19: return FD_EVENT;  // eventfd2
	case 20: return FD_EPOLL;  // epoll_create1
	case 21: return sys_epoll_ctl(a0, a1, a2, (struct linux_epoll_event*)a3);
	case 22: return sys_epoll_pwait((struct linux_epoll_event*)a1, a2, a3);

	case 96: return 1;         // set_tid_address
	case 99: case 167: return 0; // set_robust_list, prctl
	case 163: case 261: return -L_ENOSYS; // getrlimit, prlimit64
	}
	{
		char msg[64];
		int n = snprintf(msg, sizeof msg, "nx: unimplemented syscall %ld\n", (long)nr);
		log_write(msg, n);
	}
	return -L_ENOSYS;
}

// Used by runtime/cgo when starting threads; there are no signals to mask.
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) {
	if (old) memset(old, 0, sizeof *old);
	return 0;
}

// Called by _nx_svc through asmcgocall, on the system stack.
void x_nx_syscall(sptr *args) {
	int saved = errno;
	args[0] = dispatch(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
	errno = saved;
}

#endif

// Size of the Go heap block, so the engine can set GOMEMLIMIT below it:
// the Go GC does not know about this hard cap and would otherwise let
// garbage grow past it.
size_t nx_go_heap_size(void) {
	return mem_pages * NX_PAGE;
}

// Hang watchdog. The engine bumps nx_hb_frames once per frame (main
// thread) and nx_hb_go once per second (a goroutine). This plain Horizon
// thread logs when either stops, which tells a stuck main loop from a
// stuck Go runtime.
unsigned nx_hb_frames, nx_hb_go;
static Thread wd_thread;

static void wd_main(void *arg) {
	(void)arg;
	unsigned lf = 0, lg = 0, stalled = 0;
	for (;;) {
		svcSleepThread(5000000000LL);
		unsigned f = __atomic_load_n(&nx_hb_frames, __ATOMIC_RELAXED);
		unsigned g = __atomic_load_n(&nx_hb_go, __ATOMIC_RELAXED);
		if (f == lf || g == lg) {
			char msg[96];
			int k = snprintf(msg, sizeof msg, "nx wd: %s%s (frames=%u go=%u)\n",
				f == lf ? "MAIN PARADO " : "", g == lg ? "GO PARADO" : "", f, g);
			if (stalled < 24) log_write(msg, k); // at most 2 minutes of it
			stalled++;
		} else {
			stalled = 0;
		}
		lf = f; lg = g;
	}
}

void nx_watchdog_start(void) {
	if (R_SUCCEEDED(threadCreate(&wd_thread, wd_main, NULL, NULL, 0x4000, 0x2C, -2)))
		threadStart(&wd_thread);
}
