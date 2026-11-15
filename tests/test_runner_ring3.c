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
            "document.addEventListener('DOMContentLoaded',function(){output.setAttribute('data-dom-ready','yes');});"
            "addEventListener('load',function(){output.setAttribute('data-window-load','yes');});"
            "if(window!==globalThis||self!==globalThis||"
            "navigator.platform!=='LIWUS'||navigator.onLine!==true||"
            "location.href!=='about:blank'||location.protocol!=='about:'||"
            "location.pathname!=='blank'||document.URL!=='about:blank')"
            "throw new Error('basic web globals/location failed');"
            "output.dataset.extra='dataset-ok';"
            "output.style.backgroundColor='rgb(1, 2, 3)';"
            "if(output.dataset.extra!=='dataset-ok'||"
            "output.getAttribute('data-extra')!=='dataset-ok'||"
            "output.style.backgroundColor!=='rgb(1, 2, 3)'||"
            "output.getAttribute('style')!=='background-color: rgb(1, 2, 3)')"
            "throw new Error('style or dataset bridge failed');"
            "const parsed=new DOMParser().parseFromString('<article data-x=ok>parsed</article>','text/html');"
            "if(!parsed.querySelector('article')||parsed.querySelector('article').dataset.x!=='ok')"
            "throw new Error('DOMParser failed');"
            "if(new XMLSerializer().serializeToString(parsed.querySelector('article'))!=='<article data-x=\"ok\">parsed</article>')"
            "throw new Error('XMLSerializer failed');"
            "const params=new URLSearchParams([['b','2'],['a','1']]);params.sort();"
            "if(params.size!==2||params.toString()!=='a=1&b=2')"
            "throw new Error('URLSearchParams extensions failed');"
            "const rel=new URL('../assets/app.js?x=1#top','https://github.com/org/repo/index.html');"
            "if(rel.href!=='https://github.com/org/assets/app.js?x=1#top'||"
            "new URL('?q=2','https://github.com/a/b').href!=='https://github.com/a/b?q=2'||"
            "new URL('//cdn.example/x','https://github.com/a').origin!=='https://cdn.example')"
            "throw new Error('relative URL resolution failed');"
            "const image=new Image(32,16);if(image.width!==32||image.height!==16||"
            "!document.fonts.check('14px sans-serif'))throw new Error('media/font globals failed');"
            "const video=document.createElement('video');if(typeof video.play!=='function'||video.canPlayType('video/mp4')!=='maybe'||video.paused!==true)throw new Error('media element surface failed');video.currentTime=2;video.play();if(video.paused!==false||video.currentTime!==2)throw new Error('media element state failed');video.pause();"
            "if(new Intl.PluralRules().select(2)!=='other'||new Intl.ListFormat().format(['a','b'])!=='a and b'||"
            "new Intl.Collator().compare('a','b')>=0)throw new Error('Intl globals failed');"
            "const response=new Response('body',{status:201,url:'https://example.test/api',headers:{'x-test':'ok'}});"
            "const request=new Request('/api',{method:'post',headers:{'x-test':'ok'}});"
            "if(!response.ok||response.status!==201||response.headers.get('x-test')!=='ok'||"
            "request.method!=='POST'||request.url!=='/api'||response.url!=='https://example.test/api')"
            "throw new Error('Request/Response failed');"
            "const jsonResponse=Response.json({ok:true});if(jsonResponse.headers.get('content-type')!=='application/json'||"
            "Response.error().type!=='error'||Response.redirect('/next',303).status!==303)"
            "throw new Error('Response static helpers failed');"
            "const xhr=new XMLHttpRequest();if(!(xhr instanceof EventTarget)||xhr.readyState!==XMLHttpRequest.UNSENT)throw new Error('XHR constructor failed');"
            "xhr.open('get','/api');if(xhr.readyState!==XMLHttpRequest.OPENED||xhr._m!=='GET')throw new Error('XHR open failed');"
            "xhr.setRequestHeader('X-Test','ok');xhr.abort();if(xhr.readyState!==XMLHttpRequest.UNSENT)throw new Error('XHR abort failed');"
            "const consumed=new Response('x');consumed.text();if(!consumed.bodyUsed)throw new Error('Response bodyUsed failed');"
            "let cloneRejected=false;try{consumed.clone();}catch(e){cloneRejected=true;}if(!cloneRejected)throw new Error('Response clone guard failed');"
            "const requestBody=new Request('/body',{method:'POST',body:'x'});requestBody.text();"
            "if(!requestBody.bodyUsed)throw new Error('Request bodyUsed failed');"
            "const ws=new WebSocket('wss://example.invalid');const es=new EventSource('/events');"
            "if(ws.readyState!==WebSocket.CLOSED||es.readyState!==EventSource.CLOSED)"
            "throw new Error('network event shims failed');ws.close();es.close();"
            "const bc=new BroadcastChannel('qjs-test');bc.close();"
            "const controller=new AbortController();const combined=AbortSignal.any([controller.signal]);"
            "controller.abort('stop');if(!combined.aborted||combined.reason!=='stop')"
            "throw new Error('AbortSignal.any failed');"
            "if(structuredClone({a:[1,2]}).a[1]!==2)throw new Error('structuredClone failed');"
            "history.pushState({page:2},'', '?page=2');"
            "if(history.state.page!==2||location.search!=='?page=2')throw new Error('history pushState failed');"
            "history.back();if(history.state!==null)throw new Error('history back failed');"
            "if(output.textContent!=='old & text'||"
            "output.getAttribute('data-label')!=='A & B')"
            "throw new Error('HTML entities were not decoded in the DOM');"
            "document.title='Script title';"
            "output.textContent='mutated <&';"
            "if(output.textContent!=='mutated <&')"
            "throw new Error('mutated textContent changed on readback');"
            "const section=document.getElementById('siblings');"
            "let observed=false,textObserved=false,recordTyped=false;const mo=new MutationObserver(function(records){for(const r of records){if(r.type==='childList')observed=true;if(r.type==='characterData')textObserved=true;if(r instanceof MutationRecord)recordTyped=true;}});"
            "mo.observe(section,{childList:true,characterData:true,subtree:true});section.appendChild(document.createElement('small'));"
            "const textTarget=document.createElement('i');section.appendChild(textTarget);textTarget.textContent='first-updated';textTarget.remove();"
            "queueMicrotask(function(){if(!observed||!textObserved||!recordTyped)throw new Error('MutationObserver failed');});"
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
            "if(picked.closest('section')!==section)throw new Error('closest failed');"
            "if(!picked.matches('span.pick'))throw new Error('matches failed');"
            "if(section.parentElement!==document.body)throw new Error('parentElement failed');"
            "if(section.children.length!==4)throw new Error('children failed '+section.children.length);"
            "if(section.firstElementChild!==first)throw new Error('firstElementChild failed');"
            "if(!section.lastElementChild||section.lastElementChild.tagName!=='SMALL')throw new Error('lastElementChild failed');"
            "if(first.nextElementSibling!==picked)throw new Error('nextElementSibling failed');"
            "if(picked.previousElementSibling!==first)throw new Error('previousElementSibling failed');"
            "if(picked.className!=='pick')throw new Error('className failed');"
            "picked.toggleAttribute('hidden');if(!picked.hidden||!picked.hasAttribute('hidden'))"
            "throw new Error('toggleAttribute failed');picked.toggleAttribute('hidden',false);"
            "const replaceTarget=document.createElement('div');replaceTarget.append('a','b');"
            "replaceTarget.replaceChildren('done');if(replaceTarget.textContent!=='done')"
            "throw new Error('replaceChildren failed');"
            "const form=document.createElement('form');let submitted=false;form.addEventListener('submit',function(e){submitted=true;e.preventDefault();});"
            "form.requestSubmit();if(!submitted||!form.checkValidity()||!form.reportValidity())throw new Error('form APIs failed');"
            "const ce=new CustomEvent('custom',{detail:7,bubbles:true});if(ce.detail!==7||ce.type!=='custom'||!(ce instanceof Event))throw new Error('CustomEvent failed');"
            "const me=new MouseEvent('click',{clientX:3,clientY:4,ctrlKey:true,button:1});if(me.clientX!==3||me.clientY!==4||!me.ctrlKey||me.button!==1)throw new Error('MouseEvent failed '+me.clientX+','+me.clientY+','+me.ctrlKey+','+me.button);"
            "const pe=new PointerEvent('pointerdown',{pointerId:2,pointerType:'mouse',isPrimary:true});if(pe.pointerId!==2||pe.pointerType!=='mouse'||!pe.isPrimary)throw new Error('PointerEvent failed');"
            "const ke=new KeyboardEvent('keydown',{key:'Enter',code:'Enter',keyCode:13});if(ke.key!=='Enter'||ke.code!=='Enter'||ke.keyCode!==13||ke.which!==13)throw new Error('KeyboardEvent failed');"
            "const ie=new InputEvent('input',{data:'a',inputType:'insertText'});if(ie.data!=='a'||ie.inputType!=='insertText')throw new Error('InputEvent failed');"
            "const msg=new MessageEvent('message',{data:'ok',origin:'https://example.test'});if(msg.data!=='ok'||msg.origin!=='https://example.test'||!Array.isArray(msg.ports))throw new Error('MessageEvent failed');"
            "const prog=new ProgressEvent('progress',{lengthComputable:true,loaded:4,total:8});if(!prog.lengthComputable||prog.loaded!==4||prog.total!==8)throw new Error('ProgressEvent failed');"
            "const close=new CloseEvent('close',{code:1000,reason:'done',wasClean:true});if(close.code!==1000||close.reason!=='done'||!close.wasClean)throw new Error('CloseEvent failed');"
            "const pop=new PopStateEvent('popstate',{state:{page:1}});if(!pop.state||pop.state.page!==1)throw new Error('PopStateEvent failed');"
            "if(typeof CSS==='undefined'||typeof CSS.supports!=='function'||typeof CSS.escape!=='function')throw new Error('CSS compatibility missing');"
            "if(!CSS.supports('display','grid')||CSS.supports('display','invalid'))throw new Error('CSS supports values failed');"
            "const tpl=document.createElement('template');tpl.innerHTML='<span>template</span>';"
            "if(!tpl.content||!tpl.content.firstChild||tpl.content.firstChild.textContent!=='template')throw new Error('template content failed');"
            "const shadowHost=document.createElement('div');const shadow=shadowHost.attachShadow({mode:'open'});"
            "const shadowChild=document.createElement('b');shadowChild.textContent='shadow';shadow.appendChild(shadowChild);"
            "if(!shadowHost.shadowRoot||shadowHost.shadowRoot.host!==shadowHost||shadow.querySelector('b')!==shadowChild)throw new Error('shadow root failed');"
            "const stream=new ReadableStream({start:function(c){c.enqueue('x');c.close();}});const reader=stream.getReader();"
            "if(!stream.locked||typeof reader.read!=='function'||typeof WritableStream!=='function'||typeof TransformStream!=='function')throw new Error('streams failed');reader.releaseLock();"
            "if(typeof new Blob(['x']).stream!=='function'||typeof new Blob(['x']).bytes!=='function')throw new Error('blob streams failed');"
            "const up=new URLPattern('https://*.example.test/*');if(!up.test('https://www.example.test/repo')||up.test('https://other.test/repo')||!up.exec('https://www.example.test/repo'))throw new Error('URLPattern failed');"
            "const fr=new FileReader();if(fr.readyState!==FileReader.EMPTY||typeof fr.readAsText!=='function')throw new Error('FileReader surface failed');"
            "let frLoaded=false;fr.onload=function(){frLoaded=fr.result==='hello'&&fr.readyState===FileReader.DONE;};fr.readAsText(new Blob(['hello']));queueMicrotask(function(){if(!frLoaded)throw new Error('FileReader async read failed');});"
            "const rect=new DOMRect(1,2,3,4);if(rect.right!==4||rect.bottom!==6||rect.left!==1||rect.top!==2||!(rect instanceof DOMRectReadOnly))throw new Error('DOMRect failed');"
            "const matrix=new DOMMatrix([1,0,0,1,5,6]);const rotated=matrix.rotate(90);if(matrix.e!==5||matrix.f!==6||rotated.is2D!==true||matrix.inverse().e!==-5||matrix.toFloat32Array().length!==6||matrix.toString().indexOf('matrix(')!==0)throw new Error('DOMMatrix failed');"
            "if(!visualViewport||visualViewport.width!==innerWidth||visualViewport.scale!==1)throw new Error('visualViewport failed');"
            "const bodyRect=document.body.getBoundingClientRect();if(!(bodyRect instanceof DOMRect)||bodyRect.width!==800||bodyRect.height!==600)throw new Error('element geometry failed');"
            "const mq=matchMedia('(min-width: 1px)');if(!(mq instanceof EventTarget)||!mq.matches||typeof mq.addListener!=='function'||typeof mq.media!=='string')throw new Error('MediaQueryList failed');"
            "const walker=document.createTreeWalker(document.body,NodeFilter.SHOW_ELEMENT);if(!walker.nextNode()||walker.currentNode.nodeType!==1)throw new Error('TreeWalker failed');"
            "const iter=document.createNodeIterator(document.body,NodeFilter.SHOW_ELEMENT);if(!iter.nextNode())throw new Error('NodeIterator failed');"
            "const mr=new MutationRecord({type:'attributes',target:output,attributeName:'data-x'});if(mr.type!=='attributes'||mr.target!==output)throw new Error('MutationRecord failed');"
            "let resizeSeen=false;new ResizeObserver(function(entries){resizeSeen=entries.length===1&&entries[0] instanceof ResizeObserverEntry;}).observe(document.body);"
            "let intersectSeen=false;new IntersectionObserver(function(entries){intersectSeen=entries.length===1&&entries[0] instanceof IntersectionObserverEntry;}).observe(document.body);"
            "const deferred=Promise.withResolvers();deferred.resolve(7);"
            "deferred.promise.then(function(v){if(v!==7)throw new Error('Promise.withResolvers failed');});"
            "const anim=picked.animate([{opacity:0},{opacity:1}],{duration:1});"
            "if(!anim||!anim.effect||anim.effect.target!==picked)throw new Error('Element.animate failed');"
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
            "advanced.id='advanced';advanced.textContent='DOM_ADVANCED_PASS|WEB_API_PASS';"
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

