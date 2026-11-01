#ifdef KERNEL_TEST
#include "framework.h"
#include "pmm.h"
#include "serial.h"
#include "syscall.h"
#include "task.h"
#include "vfs.h"

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
