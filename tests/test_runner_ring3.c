#ifdef KERNEL_TEST
#include "framework.h"
#include "initrd.h"
#include "pmm.h"
#include "serial.h"
#include "syscall.h"
#include "task.h"
#include "vfs.h"

extern task_t *task_list;

static int run_linux_elf(const char *name) {
    uint64_t irq_flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
    int pid = launch_initrd_program_argv(name, NULL);
    if (pid < 0) {
        if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
        return -1;
    }

    task_t *linux_task = task_list;
    if (!linux_task) {
        if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
        return -1;
    }
    do {
        if (linux_task->id == pid) break;
        linux_task = linux_task->next;
    } while (linux_task && linux_task != task_list);
    if (!linux_task || linux_task->id != pid) {
        if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
        return -1;
    }
    linux_task->abi = TASK_ABI_LINUX_X86_64;
    if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");

    int status = -1;
    return sys_waitpid(pid, &status, 0) == pid ? status : -1;
}

int test_ring3_processes(void) {
    uint64_t free_before = pmm_get_free_memory();
    uint64_t nodes_before = vfs_live_owned_nodes();
    uint64_t files_before = sys_io_live_file_descriptions();
    uint64_t pipes_before = sys_io_live_pipe_objects();
    int pid = launch_initrd_program_argv("ring3_suite", NULL);
    if (pid < 0) FAIL("ring3_processes", "could not load ring3_suite ELF64");

    int status = -1;
    int waited = sys_waitpid(pid, &status, 0);
    if (waited != pid) FAIL("ring3_processes", "waitpid did not reap test process");
    if (status != 0) FAIL("ring3_processes", "user suite exited with failure");

    pid = launch_initrd_program_argv("pie_probe", NULL);
    if (pid < 0) FAIL("ring3_processes", "could not load ET_DYN PIE probe");
    status = -1;
    waited = sys_waitpid(pid, &status, 0);
    if (waited != pid || status != 0)
        FAIL("ring3_processes", "ET_DYN PIE did not run at high user base");
    serial_print("[RING3 RESULT] ET_DYN PIE executed at USER_PIE_BASE\n");

    if (run_linux_elf("linux_syscall_probe") != 0)
        FAIL("ring3_processes", "Linux SYSCALL ABI probe failed");
    serial_print("[RING3 RESULT] Linux x86-64 SYSCALL getpid/fstat/openat/mmap/munmap/FS-TLS/write PASS\n");

    uint32_t static_elf_size = 0;
    if (initrd_get_file("linux_glibc_hello", &static_elf_size)) {
        if (run_linux_elf("linux_glibc_hello") != 0)
            FAIL("ring3_processes", "static GNU/Linux x86-64 ELF failed");
        serial_print("[RING3 RESULT] unmodified static GNU/Linux ELF PASS\n");
    } else {
        serial_print("[RING3 SKIP] optional static GNU/Linux ELF not supplied\n");
    }

    if (vfs_live_owned_nodes() != nodes_before)
        FAIL("ring3_processes", "VFS lookup nodes leaked across user I/O tests");
    if (sys_io_live_file_descriptions() != files_before)
        FAIL("ring3_processes", "open file descriptions leaked across user exit");
    if (sys_io_live_pipe_objects() != pipes_before)
        FAIL("ring3_processes", "pipe objects leaked across user exit");

    uint64_t free_after = pmm_get_free_memory();
    serial_print("[RING3 MEMORY] PMM free before=");
    serial_print_hex(free_before);
    serial_print(" after=");
    serial_print_hex(free_after);
    serial_print("\n");
    if (free_after < free_before)
        FAIL("ring3_processes", "PMM frames leaked by process/exec lifecycle");

    serial_print("[RING3 RESULT] kernel observed clean user exit\n");
    PASS("ring3_processes");
}
#endif
