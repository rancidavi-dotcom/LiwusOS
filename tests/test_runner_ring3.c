#ifdef KERNEL_TEST
#include "framework.h"
#include "initrd.h"
#include "sdfs.h"
#include "pmm.h"
#include "serial.h"
#include "syscall.h"
#include "task.h"
#include "vfs.h"

extern task_t *task_list;

static int prepare_permission_fixture(void) {
    static uint8_t read_allow[] = { 'R' };
    static uint8_t read_deny[] = { 'r' };
    static uint8_t write_allow[] = { 'w' };
    static uint8_t write_deny[] = { 'd' };
    static uint8_t protected_entry[] = { 'p' };
    static uint8_t rename_source[] = { 'm' };
    uint32_t exec_size = 0;
    void *exec_data;

    if (sdfs_create_dir_as("/perm_probe", 0, 0, 0777) != 0 ||
        sdfs_create_dir_as("/perm_probe/allow-dir", 0, 0, 0777) != 0 ||
        sdfs_create_dir_as("/perm_probe/deny-dir", 0, 0, 0700) != 0)
        return -1;
#define PERM_FILE(path, bytes, mode) \
    (sdfs_create_file_as((path), 0, 0, (mode)) == 0 && \
     sdfs_write_file((path), (bytes), sizeof(bytes)) == sizeof(bytes))
    if (!PERM_FILE("/perm_probe/read-allow", read_allow, 0644) ||
        !PERM_FILE("/perm_probe/read-deny", read_deny, 0600) ||
        !PERM_FILE("/perm_probe/write-allow", write_allow, 0666) ||
        !PERM_FILE("/perm_probe/write-deny", write_deny, 0444) ||
        !PERM_FILE("/perm_probe/deny-dir/entry", protected_entry, 0600) ||
        !PERM_FILE("/perm_probe/allow-dir/rename-src", rename_source, 0644))
        return -1;
#undef PERM_FILE

    exec_data = initrd_get_file("exec_target", &exec_size);
    if (!exec_data || !exec_size) return -2;
    if (sdfs_create_file_as("/perm_probe/exec-allow.lxe", 0, 0, 0755) != 0)
        return -3;
    if (sdfs_write_file("/perm_probe/exec-allow.lxe", exec_data, exec_size) != exec_size)
        return -4;
    return 0;
}

static int run_linux_elf(const char *name, char *const argv[]) {
    uint64_t irq_flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
    int pid = launch_initrd_program_argv(name, argv);
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
    linux_task->process->abi = TASK_ABI_LINUX_X86_64;
    if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");

    int status = -1;
    return sys_waitpid(pid, &status, 0) == pid ? status : -1;
}

int test_ring3_processes(void) {
    int fixture_rc = prepare_permission_fixture();
    if (fixture_rc < 0) {
        serial_print("[RING3 DIAG] permission fixture rc=");
        serial_print_hex((uint64_t)(int64_t)fixture_rc);
        serial_print("\n");
        FAIL("ring3_processes", "could not prepare native VFS permission fixtures");
    }
    uint64_t free_before = pmm_get_free_memory();
    uint64_t nodes_before = vfs_live_owned_nodes();
    uint64_t files_before = sys_io_live_file_descriptions();
    uint64_t pipes_before = sys_io_live_pipe_objects();
    uint32_t external_linux_size = 0;
    void *external_linux_elf = initrd_get_file("linux_glibc_hello",
                                               &external_linux_size);
    if (external_linux_elf) {
        sdfs_delete("/linux_glibc_hello");
        if (!external_linux_size ||
            sdfs_create_file_as("/linux_glibc_hello", 0, 0, 0755) != 0 ||
            sdfs_write_file("/linux_glibc_hello", external_linux_elf,
                            external_linux_size) != external_linux_size)
            FAIL("ring3_processes", "could not prepare external Linux ELF for execve");
    }
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

    int linux_syscall_status = run_linux_elf("linux_syscall_probe", NULL);
    if (linux_syscall_status != 0) {
        serial_print("[RING3 DIAG] Linux syscall probe exit status=");
        serial_print_hex((uint64_t)(int64_t)linux_syscall_status);
        serial_print("\n");
        FAIL("ring3_processes", "Linux SYSCALL ABI probe failed");
    }
    serial_print("[RING3 RESULT] Linux x86-64 SYSCALL getrandom/close_range/xstate-context/prctl-name/ids/resuid-resgid-groups/umask/rlimit/prlimit/time/sysinfo/sleep/yield/clocks/clock_nanosleep/access/faccessat/faccessat2/open/openat/unlink/unlinkat/pread64/pwrite64/dirfd-mkdir-stat/fork/execve/wait4/getcwd/chdir/fchdir/getcpu/getpid/uname/fstat-pipe/pipe/dup/dup2/dup3/fcntl/readv/writev/getdents64/mmap/mprotect/brk-futex-bitset/requeue/munmap/FS-TLS/initial-stack-auxv/write-errno PASS\n");

    int clone_status = run_linux_elf("linux_clone_probe", NULL);
    if (clone_status != 0) {
        serial_print("[RING3 DIAG] Linux clone probe exit status=");
        serial_print_hex((uint64_t)(int64_t)clone_status);
        serial_print("\n");
        FAIL("ring3_processes", "Linux clone/TLS/thread-exit probe failed");
    }
    serial_print("[RING3 RESULT] Linux clone, private futex WAIT/WAKE/WAIT_BITSET/timeout, set_tid_address clear/wake, shared-mm, independent FS TLS, thread-local prctl comm, thread exit PASS\n");

    uint32_t static_elf_size = 0;
    if (initrd_get_file("linux_glibc_hello", &static_elf_size)) {
        char *external_probe_argv[] = {
            "linux_syscall_probe", "x-external-run", NULL
        };
        if (run_linux_elf("linux_syscall_probe", external_probe_argv) != 0)
            FAIL("ring3_processes", "external static Linux ELF failed after Linux execve");
        serial_print("[RING3 RESULT] unmodified static GNU/Linux ELF via Linux execve/auxv PASS\n");
    } else {
        serial_print("[RING3 SKIP] optional static GNU/Linux ELF not supplied\n");
    }

    /* Native SDK artifact: the .lxe suffix names an ELF64 image. */
    {
        uint64_t irq_flags;
        asm volatile("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
        int pid = launch_initrd_program_argv("native-hello.lxe", NULL);
        if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
        if (pid < 0)
            FAIL("ring3_processes", "native LIWUS .lxe ELF not launched/executed");
        int native_status = -1;
        if (sys_waitpid(pid, &native_status, 0) != pid)
            FAIL("ring3_processes", "native LIWUS .lxe ELF not reaped");
        if (native_status != 0)
            FAIL("ring3_processes", "native LIWUS .lxe ELF exited with non-zero status");
        serial_print("[RING3 RESULT] native LIWUS .lxe ELF via crt0+SDK wrappers executed\n");
    }

    /* Run real QuickJS inline page code in ring 3, serialize the mutated DOM,
     * and return it to the caller over an ordinary LIWUS pipe. */
    {
        static char page[] =
            "<!doctype html><html><head><title>Old title</title><script>"
            "const output=document.getElementById('out');"
            "if(window!==globalThis||self!==globalThis||"
            "navigator.platform!=='LIWUS'||navigator.onLine!==true||"
            "location.href!=='about:blank'||location.protocol!=='about:'||"
            "location.pathname!=='blank'||document.URL!=='about:blank')"
            "throw new Error('basic web globals/location failed');"
            "if(output.textContent!=='old & text'||"
            "output.getAttribute('data-label')!=='A & B')"
            "throw new Error('HTML entities were not decoded in the DOM');"
            "document.title='Script title';"
            "output.textContent='mutated <&';"
            "if(output.textContent!=='mutated <&')"
            "throw new Error('mutated textContent changed on readback');"
            "const section=document.getElementById('siblings');"
            "const first=section.querySelector('i');"
            "const picked=section.querySelector('span.pick');"
            "if(!first||!picked||section.querySelector('i + span.pick')!==picked||"
            "section.querySelector('i ~ b')!==section.querySelector('b'))"
            "throw new Error('sibling selector traversal failed');"
            "if(section.querySelector('[data-kind~=beta]')!==first)"
            "throw new Error('attribute word selector failed');"
            "if(section.querySelector('[data-kind^=alpha]')!==first)"
            "throw new Error('attribute prefix selector failed');"
            "if(section.querySelector('[data-kind$=beta]')!==first)"
            "throw new Error('attribute suffix selector failed');"
            "if(section.querySelector('[data-kind*=lph]')!==first)"
            "throw new Error('attribute substring selector failed');"
            "if(section.querySelector('[data-kind*=ALP i]')!==first)"
            "throw new Error('case-insensitive attribute selector failed');"
            "if(section.querySelector('[lang|=en]')!==first)"
            "throw new Error('attribute dash selector failed');"
            "if(section.querySelector(':nth-child(2)')!==picked||"
            "section.querySelector('i:first-of-type')!==first||"
            "section.querySelector('span:not(.absent)')!==picked||"
            "document.querySelector(':root')!==document.documentElement)"
            "throw new Error('structural pseudo-class failed');"
            "if(!first.hasAttributes())throw new Error('hasAttributes failed');"
            "if(!first.hasChildNodes())throw new Error('hasChildNodes failed');"
            "if(first.getAttributeNames().length!==2)"
            "throw new Error('getAttributeNames failed');"
            "if(!first.isSameNode(section.querySelector('i')))"
            "throw new Error('isSameNode failed');"
            "const cloned=first.cloneNode(true);"
            "if(cloned.isSameNode(first))throw new Error('cloneNode reused node');"
            "if(first.textContent!==cloned.textContent)"
            "throw new Error('cloneNode text differs');"
            "if(first.getAttribute('data-kind')!==cloned.getAttribute('data-kind')||"
            "first.getAttribute('lang')!==cloned.getAttribute('lang'))"
            "throw new Error('cloneNode attributes differ');"
            "if(first.childNodes.length!==cloned.childNodes.length)"
            "throw new Error('cloneNode child count differs');"
            "if(!first.firstChild.isEqualNode(cloned.firstChild))"
            "throw new Error('cloneNode text-node equality failed');"
            "if(!first.isEqualNode(cloned))"
            "throw new Error('isEqualNode failed '+first.outerHTML+' / '+"
            "cloned.outerHTML);"
            "if(first.getRootNode().nodeType!==9)"
            "throw new Error('getRootNode failed');"
            "if(first.compareDocumentPosition(picked)!==4||"
            "picked.compareDocumentPosition(first)!==2)"
            "throw new Error('compareDocumentPosition order failed');"
            "const fragment=document.createDocumentFragment();"
            "const em=document.createElement('em');fragment.appendChild(em);"
            "section.appendChild(fragment);"
            "if(fragment.childNodes.length!==0||section.lastChild!==em)"
            "throw new Error('DocumentFragment insertion failed');"
            "const normalizeTarget=document.createElement('div');"
            "normalizeTarget.appendChild(document.createTextNode('a'));"
            "normalizeTarget.appendChild(document.createTextNode(''));"
            "normalizeTarget.appendChild(document.createTextNode('b'));"
            "normalizeTarget.normalize();"
            "if(normalizeTarget.childNodes.length!==1||"
            "normalizeTarget.textContent!=='ab')"
            "throw new Error('Node.normalize failed');"
            "const advanced=document.createElement('p');"
            "advanced.id='advanced';advanced.textContent='DOM_ADVANCED_PASS';"
            "document.body.appendChild(advanced);"
            "console.log('QJS_SCRIPT_RUNTIME_OK');"
            "</script><script type=\"module\">"
            "document.getElementById('advanced').textContent+='|MODULE_PASS';"
            "</script></head><body><p id=\"out\" "
            "data-label=\"A &amp; B\">old &amp; text</p>"
            "<section id=\"siblings\"><i data-kind=\"alpha beta\" "
            "lang=\"en-US\">first</i> text <span class=\"pick\" "
            "data-kind=\"alphabet\">picked</span><b>later</b></section>"
            "</body></html>";
        (void)sdfs_delete("/qjs-ring3.html");
        if (sdfs_create_file_as("/qjs-ring3.html", 0, 0, 0644) != 0 ||
            sdfs_write_file("/qjs-ring3.html", (uint8_t *)page,
                            sizeof(page) - 1) != sizeof(page) - 1)
            FAIL("ring3_processes", "could not create QuickJS HTML fixture");
        uint64_t irq_flags;
        asm volatile("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
        int qjs_pid = launch_initrd_program_argv("qjs_pipe_probe.lxe", NULL);
        if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
        if (qjs_pid < 0)
            FAIL("ring3_processes", "could not launch QuickJS pipe/DOM probe");
        int qjs_status = -1;
        if (sys_waitpid(qjs_pid, &qjs_status, 0) != qjs_pid || qjs_status != 0)
            FAIL("ring3_processes", "QuickJS script/DOM pipe probe failed");
        if (sdfs_delete("/qjs-ring3.html") != 0)
            FAIL("ring3_processes", "could not remove QuickJS HTML fixture");
        serial_print("[RING3 RESULT] QuickJS inline script, entity DOM, escaped mutations and pipe handoff PASS\n");
    }

    /* This ELF is compiled from a generated project in /tmp using only an
     * isolated sdk-install prefix; its source never includes kernel headers. */
    {
        char *external_argv[] = { "external-test.lxe", "demo", NULL };
        uint64_t irq_flags;
        asm volatile("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
        int external_pid = launch_initrd_program_argv("external-test.lxe",
                                                       external_argv);
        if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
        if (external_pid < 0)
            FAIL("ring3_processes", "external SDK ELF did not load");
        int external_status = -1;
        if (sys_waitpid(external_pid, &external_status, 0) != external_pid ||
            external_status != 23)
            FAIL("ring3_processes", "external SDK ELF output/exit status failed");
        serial_print("[RING3 RESULT] external SDK application output and exit 23 PASS\n");

    int permission_pid = launch_initrd_program_argv("permission_probe", NULL);
    if (permission_pid < 0)
        FAIL("ring3_processes", "native permission probe ELF did not launch");
    int permission_status = -1;
    if (sys_waitpid(permission_pid, &permission_status, 0) != permission_pid ||
        permission_status != 0)
        FAIL("ring3_processes", "native VFS permission probe failed");
    serial_print("[RING3 RESULT] native SDFS permissions, syscall rights, unlink lifetime and exec checks PASS\n");
    }

    if (vfs_live_owned_nodes() != nodes_before)
        FAIL("ring3_processes", "VFS lookup nodes leaked across native/Linux SDK probes");
    if (sys_io_live_file_descriptions() != files_before)
        FAIL("ring3_processes", "open file descriptions leaked across native/Linux SDK probes");
    if (sys_io_live_pipe_objects() != pipes_before)
        FAIL("ring3_processes", "pipe objects leaked across native/Linux SDK probes");

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

