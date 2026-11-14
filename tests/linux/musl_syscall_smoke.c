#define _GNU_SOURCE
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

int main(void) {
  unsigned char random_bytes[32];
  unsigned int cpuid_eax = 1, cpuid_ebx, cpuid_ecx, cpuid_edx;
  struct utsname system_name;
  struct timespec now;
  struct timespec resolution;
  time_t wall_time;
  char cwd[64];
  char task_name[16] = {0};
  char io_buffer[16] = {0};
  char *allocation = malloc(256 * 1024);
  if (!allocation) { puts("musl smoke fail 10 malloc"); return 10; }
  memset(allocation, 0x5a, 256 * 1024);

  __asm__ volatile("cpuid"
                   : "+a"(cpuid_eax), "=b"(cpuid_ebx), "=c"(cpuid_ecx),
                     "=d"(cpuid_edx));
  if (cpuid_ecx & (1u << 30)) {
    if (getrandom(random_bytes, sizeof(random_bytes), 0) !=
        (ssize_t)sizeof(random_bytes))
      { puts("musl smoke fail 11 getrandom-rdrand"); return 11; }
  } else if (getrandom(random_bytes, sizeof(random_bytes), 0) != -1 ||
             errno != EIO) {
    puts("musl smoke fail 11 getrandom-no-rdrand");
    return 11;
  }
  if (uname(&system_name) != 0 || strcmp(system_name.machine, "x86_64") != 0)
    { puts("musl smoke fail 12 uname"); return 12; }
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
      now.tv_nsec < 0 || now.tv_nsec >= 1000000000L)
    { puts("musl smoke fail 13 clock_gettime"); return 13; }
  if (clock_getres(CLOCK_MONOTONIC, &resolution) != 0 ||
      resolution.tv_sec != 0 || resolution.tv_nsec <= 0)
    { puts("musl smoke fail 19 clock_getres"); return 19; }
  if (sched_yield() != 0) { puts("musl smoke fail 20 sched_yield"); return 20; }
  uint64_t affinity_mask = 0;
  if (syscall(204, 0, sizeof(affinity_mask), &affinity_mask) !=
          (long)sizeof(affinity_mask) || affinity_mask != 1) {
    printf("libc smoke fail 60 sched_getaffinity mask=%llx errno=%d\n",
           (unsigned long long)affinity_mask, errno);
    return 60;
  }
  errno = 0;
  if (syscall(204, -1, sizeof(affinity_mask), &affinity_mask) != -1 ||
      errno != EINVAL) { puts("musl smoke fail 61 affinity-invalid-pid"); return 61; }
  errno = 0;
  if (syscall(204, 999999, sizeof(affinity_mask), &affinity_mask) != -1 ||
      errno != ESRCH) { puts("musl smoke fail 62 affinity-missing-pid"); return 62; }
  errno = 0;
  if (syscall(204, 0, sizeof(uint32_t), &affinity_mask) != -1 ||
      errno != EINVAL) { puts("musl smoke fail 63 affinity-short-mask"); return 63; }
  errno = 0;
  if (syscall(204, 0, sizeof(affinity_mask), (void *)1) != -1 ||
      errno != EFAULT) { puts("musl smoke fail 64 affinity-invalid-buffer"); return 64; }
  time_t wall_time_return = time(&wall_time);
  if (wall_time_return < 0 || wall_time_return != wall_time)
    { puts("musl smoke fail 18 time"); return 18; }
  if (!getcwd(cwd, sizeof(cwd))) { puts("musl smoke fail 14 getcwd"); return 14; }
  if (prctl(PR_SET_NAME, "musl-smoke") != 0 ||
      prctl(PR_GET_NAME, task_name) != 0 || strcmp(task_name, "musl-smoke") != 0)
    { puts("musl smoke fail 15 prctl"); return 15; }

  static const char payload[] = "musl-io";
  FILE *stream = fopen("/.musl-abi-smoke", "w+");
  if (!stream) { puts("musl smoke fail 16 fopen"); return 16; }
  if (fwrite(payload, 1, sizeof(payload), stream) != sizeof(payload))
    { puts("musl smoke fail 21 fwrite"); return 21; }
  if (fflush(stream) != 0)
    { puts("musl smoke fail 30 fflush"); return 30; }
  if (fsync(fileno(stream)) != 0 || fdatasync(fileno(stream)) != 0 ||
      syncfs(fileno(stream)) != 0) {
    printf("libc smoke fail 56 fsync/fdatasync/syncfs errno=%d\n", errno);
    return 56;
  }
  struct stat file_status;
  if (fstat(fileno(stream), &file_status) != 0 ||
      file_status.st_size != (off_t)sizeof(payload))
    { puts("musl smoke fail 31 fstat"); return 31; }
  if (stat("/.musl-abi-smoke", &file_status) != 0 ||
      !S_ISREG(file_status.st_mode) ||
      file_status.st_size != (off_t)sizeof(payload))
    { puts("musl smoke fail 32 stat"); return 32; }
  if (access("/.musl-abi-smoke", F_OK) != 0)
    { puts("musl smoke fail 33 access"); return 33; }
  int nofollow_fd = open("/.musl-abi-smoke", O_RDONLY | O_NOFOLLOW);
  if (nofollow_fd < 0 || close(nofollow_fd) != 0)
    { printf("libc smoke fail 40 O_NOFOLLOW errno=%d\n", errno); return 40; }
  int advice_fd = open("/.musl-abi-smoke", O_RDONLY);
  if (advice_fd < 0 ||
      posix_fadvise(advice_fd, 0, 0, POSIX_FADV_NORMAL) != 0 ||
      posix_fadvise(advice_fd, 0, 0, POSIX_FADV_RANDOM) != 0 ||
      posix_fadvise(advice_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0 ||
      posix_fadvise(advice_fd, 0, 0, POSIX_FADV_WILLNEED) != 0 ||
      posix_fadvise(advice_fd, 0, 0, POSIX_FADV_DONTNEED) != 0 ||
      posix_fadvise(advice_fd, 0, 0, POSIX_FADV_NOREUSE) != 0) {
    printf("libc smoke fail 65 fadvise errno=%d\n", errno);
    return 65;
  }
  if (posix_fadvise(advice_fd, 0, 0, -1) != EINVAL) {
    puts("musl smoke fail 66 fadvise-invalid-advice"); return 66;
  }
  if (posix_fadvise(advice_fd, -1, 0, POSIX_FADV_NORMAL) != EINVAL ||
      posix_fadvise(advice_fd, 0, -1, POSIX_FADV_NORMAL) != EINVAL ||
      posix_fadvise(advice_fd, INT64_MAX, 1, POSIX_FADV_NORMAL) != EINVAL) {
    puts("musl smoke fail 69 fadvise-invalid-range"); return 69;
  }
  if (posix_fadvise(31, 0, 0, POSIX_FADV_NORMAL) != EBADF) {
    puts("musl smoke fail 70 fadvise-invalid-fd"); return 70;
  }
  if (close(advice_fd) != 0) {
    puts("musl smoke fail 67 fadvise-close"); return 67;
  }
  int nonblock_fd = open("/.musl-abi-smoke", O_RDONLY | O_NONBLOCK);
  if (nonblock_fd < 0)
    { printf("libc smoke fail 41 O_NONBLOCK-open errno=%d\n", errno); return 41; }
  int status_flags = fcntl(nonblock_fd, F_GETFL);
  if (status_flags < 0 || !(status_flags & O_NONBLOCK)) {
    printf("libc smoke fail 42a O_NONBLOCK-get flags=%x errno=%d\n",
           status_flags, errno);
    return 42;
  }
  int set_status_rc = fcntl(nonblock_fd, F_SETFL, 0);
  if (set_status_rc != 0) {
    printf("libc smoke fail 42b O_NONBLOCK-set flags=%x rc=%d errno=%d\n",
           status_flags, set_status_rc, errno);
    return 42;
  }
  int cleared_flags = fcntl(nonblock_fd, F_GETFL);
  if (cleared_flags < 0 || (cleared_flags & O_NONBLOCK)) {
    printf("libc smoke fail 42c O_NONBLOCK-clear flags=%x errno=%d\n",
           cleared_flags, errno);
    return 42;
  }
  if (close(nonblock_fd) != 0)
    { puts("musl smoke fail 43 close-nonblock"); return 43; }
  if (fseek(stream, 0, SEEK_SET) != 0)
    { puts("musl smoke fail 22 fseek"); return 22; }
  struct iovec positional_iov[2] = {
    { .iov_base = io_buffer, .iov_len = 3 },
    { .iov_base = io_buffer + 3, .iov_len = sizeof(payload) - 3 },
  };
  if (preadv(fileno(stream), positional_iov, 2, 0) !=
          (ssize_t)sizeof(payload) ||
      memcmp(io_buffer, payload, sizeof(payload)) != 0 ||
      lseek(fileno(stream), 0, SEEK_CUR) != 0) {
    puts("musl smoke fail 71 preadv-position-preservation"); return 71;
  }
  static const char patch_a[] = "mus";
  static const char patch_b[] = "l-io\0";
  struct iovec write_iov[2] = {
    { .iov_base = (void *)patch_a, .iov_len = sizeof(patch_a) - 1 },
    { .iov_base = (void *)patch_b, .iov_len = sizeof(patch_b) - 1 },
  };
  if (pwritev(fileno(stream), write_iov, 2, 0) !=
          (ssize_t)sizeof(payload) ||
      lseek(fileno(stream), 0, SEEK_CUR) != 0) {
    puts("musl smoke fail 72 pwritev-position-preservation"); return 72;
  }
  if (syscall(327, fileno(stream), positional_iov, 2, 0, 0, 0) !=
          (long)sizeof(payload) ||
      memcmp(io_buffer, payload, sizeof(payload)) != 0 ||
      lseek(fileno(stream), 0, SEEK_CUR) != 0) {
    printf("libc smoke fail 74 preadv2 flags=0 errno=%d\n", errno);
    return 74;
  }
  errno = 0;
  if (syscall(328, fileno(stream), write_iov, 2, 0, 0, 1) != -1 ||
      errno != EOPNOTSUPP) {
    printf("libc smoke fail 75 pwritev2 unsupported flags errno=%d\n", errno);
    return 75;
  }
  errno = 0;
  ssize_t negative_preadv = syscall(295, fileno(stream), positional_iov, 2,
                                    -1, -1);
  if (negative_preadv != -1 || errno != EINVAL) {
    printf("musl smoke fail 73 preadv-negative-offset rc=%d errno=%d\n",
           (int)negative_preadv, errno);
    return 73;
  }
  if (fread(io_buffer, 1, sizeof(payload), stream) != sizeof(payload))
    { puts("musl smoke fail 23 fread"); return 23; }
  if (memcmp(io_buffer, payload, sizeof(payload)) != 0)
    { puts("musl smoke fail 24 compare"); return 24; }
  if (fclose(stream) != 0)
    { puts("musl smoke fail 25 fclose"); return 25; }
  if (rename("/.musl-abi-smoke", "/.musl-abi-smoke") != 0)
    { puts("musl smoke fail 39 rename-same-path"); return 39; }
  if (rename("/.musl-abi-smoke", "/.musl-abi-smoke-renamed") != 0)
    { printf("libc smoke fail 37 rename errno=%d\n", errno); return 37; }
  if (stat("/.musl-abi-smoke-renamed", &file_status) != 0 ||
      !S_ISREG(file_status.st_mode))
    { puts("musl smoke fail 38 stat-renamed"); return 38; }
  if (unlink("/.musl-abi-smoke-renamed") != 0)
    { puts("musl smoke fail 26 unlink"); return 26; }

  static const char directory_path[] = "/.musl-abi-directory";
  if (mkdir(directory_path, 0700) != 0)
    { printf("libc smoke fail 34 mkdir errno=%d\n", errno); return 34; }
  struct stat directory_status;
  if (stat(directory_path, &directory_status) != 0 ||
      !S_ISDIR(directory_status.st_mode))
    { puts("musl smoke fail 35 stat-directory"); return 35; }
  if (rmdir(directory_path) != 0)
    { printf("libc smoke fail 36 rmdir errno=%d\n", errno); return 36; }

  DIR *directory = opendir("/");
  if (!directory) {
    printf("libc smoke fail 27 opendir errno=%d\n", errno);
    return 27;
  }
  if (!readdir(directory))
    { puts("musl smoke fail 28 readdir"); return 28; }
  if (closedir(directory) != 0)
    { puts("musl smoke fail 29 closedir"); return 29; }

  int pipe_fds[2];
  if (pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) != 0) {
    printf("libc smoke fail 50 pipe2 errno=%d\n", errno);
    return 50;
  }
  if (fcntl(pipe_fds[0], F_GETFD) != FD_CLOEXEC ||
      fcntl(pipe_fds[1], F_GETFD) != FD_CLOEXEC) {
    puts("musl smoke fail 51 pipe2-cloexec"); return 51;
  }
  errno = 0;
  if (fsync(pipe_fds[0]) != -1 || errno != EINVAL) {
    printf("libc smoke fail 57 fsync-pipe errno=%d\n", errno);
    return 57;
  }
  if (posix_fadvise(pipe_fds[0], 0, 0, POSIX_FADV_NORMAL) != ESPIPE) {
    puts("musl smoke fail 68 fadvise-pipe"); return 68;
  }
  errno = 0;
  if (syncfs(pipe_fds[0]) != -1 || errno != EINVAL) {
    printf("libc smoke fail 59 syncfs-pipe errno=%d\n", errno);
    return 59;
  }
  int pipe_status = fcntl(pipe_fds[0], F_GETFL);
  if (pipe_status < 0 || !(pipe_status & O_NONBLOCK)) {
    printf("libc smoke fail 52 pipe2-nonblock flags=%x errno=%d\n",
           pipe_status, errno);
    return 52;
  }
  char pipe_byte = 0;
  errno = 0;
  if (read(pipe_fds[0], &pipe_byte, 1) != -1 || errno != EAGAIN) {
    printf("libc smoke fail 53 pipe-empty read errno=%d\n", errno);
    return 53;
  }
  if (write(pipe_fds[1], "p", 1) != 1 ||
      read(pipe_fds[0], &pipe_byte, 1) != 1 || pipe_byte != 'p') {
    puts("musl smoke fail 54 pipe-roundtrip"); return 54;
  }
  if (close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0) {
    puts("musl smoke fail 55 pipe-close"); return 55;
  }
  errno = 0;
  if (fsync(pipe_fds[0]) != -1 || errno != EBADF) {
    printf("libc smoke fail 58 fsync-closed-fd errno=%d\n", errno);
    return 58;
  }

  free(allocation);
  puts("static Linux libc syscall smoke PASS");
  return 0;
}
