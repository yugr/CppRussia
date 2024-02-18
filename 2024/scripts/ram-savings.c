// This program measures RAM savings of shared libraries
// currently used by system. It needs to be run under root.
//
// Based on https://gist.github.com/zvrba/33893e14b4536a8d55d1
//
// See https://www.kernel.org/doc/Documentation/vm/pagemap.txt

#include <dirent.h>
#include <unistd.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int v = 1;

#define CHECK(cond, msg) do {                                  \
    if (!(cond)) {                                             \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, msg); \
      assert(0);                                               \
      exit(1);                                                 \
    }                                                          \
  } while (0)

int *collect_pids(int *n) {
  int pid_max = 0;
  int *pids = 0;
  int pid_count = 0;

  DIR *proc = opendir("/proc");
  struct dirent *de;

  while (de = readdir(proc)) {
    int pid = atoi(de->d_name);
    if (!pid)
      continue;
    if (++pid_count > pid_max) {
      pid_max += 500;
      pids = realloc(pids, pid_max * sizeof(int));
    }
    pids[pid_count - 1] = pid;
  }

  closedir(proc);

  *n = pid_count;
  return pids;
}

void analyze_pid(int pid, int *counts, long max_pages, long pagesize,
                 FILE *kpagecount, int data) {
  char maps_name[PATH_MAX];
  snprintf(maps_name, sizeof(maps_name), "/proc/%d/maps", pid);
  FILE *maps = fopen(maps_name, "rb");
  CHECK(maps, "failed to open maps file");

  char pagemap_name[PATH_MAX];
  snprintf(pagemap_name, sizeof(maps_name), "/proc/%d/pagemap", pid);
  FILE *pagemap = fopen(pagemap_name, "rb");
  CHECK(pagemap, "failed to open pagemap file");

  while (!feof(maps)) {
    char line[PATH_MAX + 100];
    char *tmp = fgets(line, sizeof(line), maps);
    if (!tmp)
      break;

    // Parse entry
    // 56129b372000-56129b3a9000 r--p 00000000 08:03 3411032    /usr/bin/vim.basic

    long begin, end, off, major, minor, inode;
    char perms[5] = {};
    int prefix_len;
    sscanf(line, "%lx-%lx %4s %ld %ld:%ld %ld%n",
           &begin, &end, perms, &off,
           &major, &minor, &inode, &prefix_len);

    prefix_len += strspn(&line[prefix_len], " ");
    char *soname = &line[prefix_len];
    char *soname_end = strchr(soname, '\n');
    if (soname_end)
      *soname_end = 0;

    int is_shareable = strcmp("r-xp", perms) == 0;
    if (data) {
      is_shareable |= strcmp("r--p", perms) == 0;
    }

    if (!is_shareable || !inode || !strstr(soname, ".so"))
      continue;

    // Analyze range

    begin /= pagesize;
    end /= pagesize;

    for (long page = begin; page < end; ++page) {
      int ret = fseek(pagemap, 8 * page, SEEK_SET);
      CHECK(ret == 0, "pagemap fseek failed");

      uint64_t flags;
      size_t nread = fread(&flags, sizeof(flags), 1, pagemap);
      CHECK(nread == 1, "pagemap fread failed");

      if ((flags >> 63) != 1) // Page present?
        continue;

      uint64_t pfn = flags & ((1UL << 55) - 1);
      CHECK(pfn < max_pages, "PFN too large");

      if (counts[pfn])
        continue;

      ret = fseek(kpagecount, 8 * pfn, SEEK_SET);
      CHECK(ret == 0, "kpagecount fseek failed");

      uint64_t count;
      nread = fread(&count, sizeof(count), 1, kpagecount);
      CHECK(nread == 1, "kpagecount fread failed");
//printf("%d %ld %ld\n", pid, pfn, count);

      counts[pfn] = count;
    }
  }

  fclose(maps);
  fclose(pagemap);
}

int main(int argc, char *argv[]) {
  // Parse options

  int pid = -1;
  int data = 0;

  int opt;
  while ((opt = getopt(argc, argv, "hdp:v")) != -1) {
    switch (opt) {
      case 'd':
        data = 1;
        break;
      case 'p':
        pid = atoi(optarg);
        break;
      case 'v':
        ++v;
        break;
      case 'h':
      default:
        fprintf(stderr, "Usage: %s [-h] [-d] [-p pid] [-v]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  CHECK(getuid() == 0, "need to run under root");

  // Get system info

  long pagesize = sysconf(_SC_PAGESIZE);
  CHECK(pagesize > 0, "failed to obtain pagesize");

  long max_pages = 2 * sysconf(_SC_PHYS_PAGES);  // FIXME: why PFN count does not match _SC_PHYS_PAGES ?!
  CHECK(max_pages > 0, "failed to obtain page count");

  int *counts = calloc(max_pages, sizeof(int));

  if (v)
    printf("Memory size: %ld\n", pagesize * max_pages);

  // Collect pids

  int pid_count;
  int *pids;

  if (pid != -1) {
    pids = malloc(sizeof(int));
    pids[0] = pid;
    pid_count = 1;
  } else {
    pids = collect_pids(&pid_count);
    if (v) {
      printf("Pids: ");
      for (int i = 0; i < pid_count; ++i)
        printf("%d ", pids[i]);
      printf("(total %d)\n", pid_count);
    }
  }

  // Compute page counts

  FILE *kpagecount = fopen("/proc/kpagecount", "rb");
  CHECK(kpagecount, "failed to open /proc/kpagecount");

  for (int i = 0; i < pid_count; ++i) {
    analyze_pid(pids[i], counts, max_pages, pagesize, kpagecount, data);
  }

  fclose(kpagecount);
  free(pids);

  // Report stats

  long saved = 0;
  for (long pfn = 0; pfn < max_pages; ++pfn) {
    if (counts[pfn])
      saved += counts[pfn] - 1;
  }

  printf("Saved: %ld MB\n", saved * pagesize / 1024 / 1024);

  free(counts);

  return 0;
}
