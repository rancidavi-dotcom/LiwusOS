# LiwusWeb compatibility

LiwusWeb is a native LIWUS browser with an in-kernel HTML/CSS renderer. It is
currently aimed at readable, mostly static pages. It is not a Linux browser or
a Chromium/WebKit port.

## Current renderer

- Parses HTML text, links, headings, lists, tables, common form fields and
  buttons; handles UTF-8 and common HTML entities.
- Loads up to 24 unique external stylesheets and parses up to 3 MiB of CSS per
  page. Repeated absolute stylesheet URLs do not consume the stylesheet count.
- Renders PNG/JPEG images, with a per-page decoded image budget of 8 MiB and
  at most 24 images.
- Loads eligible images synchronously while parsing the document, including
  elements marked `loading="lazy"`; page rendering can take longer, but the
  supported images are decoded before the page is presented as complete.
- Supports a bounded set of common CSS selectors (including adjacent `+` and
  bounded general-sibling `~` matching, `:nth-child()`, `:nth-last-child()`,
  `:nth-of-type()` and `:nth-last-of-type()` with
  integer, `odd`, `even`, and `an+b` forms, simple `:where()`/`:is()` and
  child/type-position selectors, `:empty`, and attribute selectors `[attr]`,
  `[attr=val]`, `[attr~=val]`, `[attr|=val]`, `[attr^=val]`, `[attr$=val]`,
  `[attr*=val]`, and case-insensitive `i` matching), colors, text styles, box layout,
  flex/grid approximations, tables, floats and width media queries. CSS custom
  properties inherit from the parent element and resolve in the current
  element's scope, including aliases and the bounded `!important` cascade.
  Each computed element retains at most 512 scoped variables; additional
  variables are ignored. The rule
  vector grows dynamically up to 24,576 rules per page. Selector matching uses
  an index on the rightmost ID/class/tag and retains at most 256 matching rules
  per element. Unsupported selectors are skipped.
- Limits one page to 8,192 LVGL objects. The remote-resource cache is capped at
  6 MiB.

### GitHub desktop-width validation (2026-11-14)

- The boot framebuffer request now prefers 1920x1080 when the bootloader and
  display support it; the request is optional so unsupported hardware can
  choose another mode. In QEMU, the browser received a 1920x1080 framebuffer.
- The desktop task has a dedicated 512 KiB guarded kernel stack. Rendering the
  GitHub repository at this viewport previously hit the 128 KiB stack guard.
  Other ordinary kernel tasks keep their 128 KiB default stack.
- The browser now draws the fetched server HTML/CSS before it fetches and
  starts the page's external JavaScript graph. In QEMU this exposed the file
  tree while scripts were still loading instead of leaving the previous page
  visible throughout the download.
- The QEMU capture still differs substantially from the reference: responsive
  header controls overlap, the repository columns extend incorrectly, and
  server-rendered skeleton cells remain until JavaScript hydration. One run
  later hit kernel-heap OOM during a DOM-driven re-render. Address-sorted heap
  free-list coalescing was added, but the repeat remained in a long-running
  render and did not reach that same re-render, so the OOM correction is not
  yet verified. The complete page hydration/render did not finish within the
  validation window. Do not treat GitHub as accurate or fully working yet.

### JavaScript & DOM Runtime (QuickJS Ring-3 Sandboxed Engine)

Page JavaScript runs in a separate ring-3 QuickJS process (`qjs.lxe`). LiwusWeb
renders the fetched HTML, then applies DOM snapshots published by the runtime;
script evaluation does not block the desktop event loop. External-script
expansion, CSS fetching, and image fetching can still delay navigation because
those bounded resource operations are synchronous in the current path. The
runtime provides
selected browser globals and compatibility shims to let some page scripts
execute; their presence does not imply standards-complete browser behavior:

- **Resource Limits & Capacity**:
  - Script source budget: up to 8 MiB total (external scripts: up to 32 files, 4 MiB combined, 2 MiB per script).
  - QuickJS memory limit: 64 MiB; stack limit: 384 KiB. LIWUS maps a 512 KiB user stack for each new ELF process, leaving native call-stack headroom above QuickJS's own limit.
  - Event listeners: up to 1,024 active registrations. Timers: up to 256 active timers.
  - Fetch queue: up to 32 pending requests. The kernel services them through
    one browser worker; response bodies over 1 MiB are currently rejected by
    that bridge (QuickJS's separate file-read bound is 4 MiB).
- **Global Environment & Aliases**:
  - `window`, `self`, `top`, `parent`, `globalThis`.
  - Comprehensive `console` (`log`, `info`, `warn`, `error`, `debug`, `trace`, `dir`, `table`).
  - Full `EventTarget` interface on `window` and DOM elements (`addEventListener`, `removeEventListener`, `dispatchEvent`).
  - Standard event constructors: `Event`, `CustomEvent`, `MouseEvent`, `KeyboardEvent`, `FocusEvent`, `UIEvent`.
- **DOM & Element APIs**:
  - Element prototypes: `Node`, `Element`, `HTMLElement`, `HTMLDivElement`, `HTMLSpanElement`, `HTMLInputElement`, `HTMLButtonElement`, `HTMLAnchorElement`, `HTMLImageElement`, `HTMLScriptElement`, `HTMLStyleElement`, `DocumentFragment`.
  - `Element.prototype.style`: Dynamic `CSSStyleDeclaration` proxy mapping camelCase and kebab-case style properties and `cssText` directly to and from element inline style attributes.
  - `Element.prototype.dataset`: `DOMStringMap` proxy synchronizing camelCase properties with `data-*` attributes.
  - Geometry & Interaction: `getBoundingClientRect()`, `getClientRects()`, `scrollIntoView()`, `focus()`, `blur()`, `click()`.
  - DOM Tree Mutation: `insertAdjacentHTML()`, `insertAdjacentElement()`, `append()`, `prepend()`.
  - Document & View: `window.getComputedStyle()`, `window.matchMedia()`, `document.cookie`, `document.defaultView`, `document.readyState`, `document.compatMode`, `document.doctype`, `document.scripts`, `document.getElementsByName()`, `document.createRange()`, `document.createElementNS()`.
  - Viewport & History: `window.screen`, `innerWidth`, `innerHeight`, `outerWidth`, `outerHeight`, `devicePixelRatio`, `scrollX`, `scrollY`, `scrollTo`, `scrollBy`, `history` (`pushState`, `replaceState`, `back`, `forward`, `go`, `state`, `length`).
  - Animation: `requestAnimationFrame`, `cancelAnimationFrame`.
- **Web API shims & networking**:
  - `fetch(input, init)` returning real Promises, with support for `Headers`, `Request`, and `Response`.
  - `XMLHttpRequest`: partial multi-state polyfill (`UNSENT`, `OPENED`, `HEADERS_RECEIVED`, `LOADING`, `DONE`) backed by `fetch`.
  - Storage: `window.localStorage` and `window.sessionStorage` with standard Web Storage API and dynamic property access.
  - Web Crypto: `crypto.getRandomValues(typedArray)`, `crypto.randomUUID()`, and `crypto.subtle.digest()`.
  - Encoding & Codecs: `btoa()`, `atob()`, `TextEncoder`, `TextDecoder`.
  - URLs: Standards-compliant `URL` and `URLSearchParams` parser and query serializer.
  - Observers: `MutationObserver`, `ResizeObserver`, and `IntersectionObserver` shims for modern library detection.
  - Performance: `performance.now()`, `timeOrigin`, `mark()`, `measure()`.
- Supports inline scripts and bounded external classic/module scripts. External
  module scripts retain their absolute URL and can resolve a bounded static
  import graph over the browser worker bridge. Import maps support the first
  map's top-level string entries under `imports`, with exact and longest-prefix
  matching (up to 32 entries and 64 KiB of JSON). `scopes`, later-map merging,
  integrity metadata, and browser-complete URL resolution are not implemented.
  Module fetching is synchronous while page scripts run. `async`, `defer`, and
  SRI processing do not yet match browser scheduling and integrity semantics.

The address bar opens explicit URLs directly. Bare hostnames (including IPv4
hosts and hosts with paths) default to HTTPS; text that does not
look like a URL or hostname is URL-encoded and sent to Bing Search
(`https://www.bing.com/search`); the initial results are delivered as HTML and
can be rendered without executing page JavaScript. DuckDuckGo's HTML and Lite
endpoints returned an anti-bot image challenge during LIWUS validation, and
the current browser cannot complete that interactive challenge. The existing
`wiki:` and `w:` search shortcuts remain available. Bing result links use a
tracking redirect; LiwusWeb decodes Bing's `u=a1` destination before opening
the target so relative links and resources use the destination site as their
base URL. Browser navigation retains URLs up to 2 KiB, and HTTP requests accept
URLs up to 1 KiB.

The Native SDK now contains a bounded, LVGL-free DOM parser in
`include/liwus/webdom.h` and `lib/liwus/webdom.c`. It can be linked into a ring-3
program, limits input to 4 MiB, caps nodes/attributes/depth, preserves
`script`/`style` bodies as inert text, and provides basic tree/attribute
mutations. `webtext URL [SAVE_PATH]` is the first ring-3 consumer: it downloads
an HTML page, parses it with this DOM, and prints its title and readable text.
The fetched HTML is limited to 4 MiB; scripts, styles, templates, and SVG are
omitted from its text output. The SDK decoder contains all 2,125 semicolon-
terminated HTML5 named references, including two-codepoint replacements, plus
numeric references. The checked-in table is regenerated with
`scripts/gen_webdom_entities.py`. Legacy semicolonless named references are not
decoded.

The desktop renderer and network fetch remain in the kernel; only page script
execution and DOM mutation run in the unprivileged QuickJS child. The desktop
hands downloaded HTML to that process through a temporary VFS file, then
renders the serialized result. `webtext` remains a separate ring-3
HTML-processing path. Transport and TLS have not moved to ring 3. Plain
`webtext URL` does not execute scripts; only the explicit `webtext --js` path
runs bounded inline classic scripts in QuickJS.

## GitHub public repository reference (2026-11-13)

The current source snapshot was fetched from
`https://github.com/rancidavi-dotcom/LiwusOS` with a desktop Chromium user
agent. GitHub returned HTTP 200 and 1,262,151 bytes of HTML. The response
contained 13 script elements, including six external module scripts, and 65
`modulepreload` links. The live page is a compatibility workload; this is not a
claim that LIWUS rendered it pixel-for-pixel or executed GitHub's full app.

The response referenced 19 unique external stylesheets through `href`, totaling
2,533,408 bytes. Parsing those exact CSS files with the current LIWUS CSS engine
produced 17,345 rules and 24,340 selector compounds, used a 1,251,661-byte
declaration arena, and did not hit the 24,576-rule cap. The rule vector is
983,040 bytes (`css_rule_t` is 40 bytes and the vector capacity is 24,576).
Selector compounds live in a separate offset-addressed arena with a measured
capacity of 32,768 compounds (10 MiB), plus a 12 KiB selector-index table and
1,182 bytes of stored attribute-selector literals. The renderer prefetches the
page's stylesheets, estimates the compound count from their combined byte size,
and reserves that arena before parsing to avoid expensive large kernel-heap
reallocations. These are host allocation measurements, not a target-memory
profile.
Style computation now visits rules indexed by the element's rightmost `id`,
class tokens, and tag, then runs the existing full matcher on those candidates;
unkeyed selectors are always checked. This avoids a full stylesheet scan for
most elements. Hash collisions only add candidates and do not bypass the full
match check. Cascade order uses the full rule position, including beyond 16,383.
The page's CSS contains class names as
long as 98 characters; selector IDs/classes are now stored as 128-bit hashes
instead of truncated fixed-size strings. This measurement ran on the host against the
captured network responses, not inside the browser process. The page's static
CSS set fits the 24-file/3-MiB stylesheet cap; JavaScript mutations, dynamic
stylesheets, navigation and final visual layout were not validated by this
source-counting measurement. A host regression with 16,502 rules visits only
the two `.growth` candidates for that node, and verifies that the final rule
still wins the cascade. This is a candidate-count regression, not a page-load
timing benchmark.

The page also shows why stylesheet count alone is not a compatibility measure:
it uses repeated CSS URLs, nested `:where()` selectors, state selectors and
sibling combinators. LIWUS now deduplicates repeated stylesheet URLs, parses
simple `:where()`/`:is()` compounds, matches adjacent siblings, and searches a
bounded history of up to four preceding elements for general-sibling rules. The
HTML prepass also records per-tag child positions for `:first-of-type`,
`:last-of-type`, `:only-of-type`, and `:nth-of-type()` when those selectors
appear. It also records whether an element has text or element children for
`:empty`; comments do not count as content, and whitespace does. Structural
metadata is limited to the prepass's first 32,768 start tags. LIWUS also matches
`:hover`, focus, active and form-state selectors against explicit node-state
bits. The renderer derives
`:disabled`, `:checked` and `:enabled` from markup. Hover, focus and active are
not yet connected to pointer/keyboard events. `:where()`/`:is()` with comma-separated
alternatives or nested combinators, `:has()`, and pseudo-element layout remain
outside the implemented subset. Attribute operator comparisons retain only
64-byte prefix/suffix windows for each captured value; substring/token matching
can miss a match located wholly inside the middle of a longer attribute value.
Each rendered element snapshots at most 12 attributes, prioritizing names that
appear in the parsed stylesheet; selectors for additional referenced attributes
on a single element may still be missed.
General-sibling matching only sees the four immediately
preceding element snapshots, by design, and will miss a matching sibling farther
back. After 32,768 start tags, later elements have unknown child/type counts, so
structural selectors can fail closed.

Verification for this checkpoint: `make test` passed 51 unique kernel cases and
23 ring-3 cases under QEMU; `make all` produced `liwusos.iso`. Host regressions
cover CSS adjacent/general sibling matching, forward/reverse child and
type-position selectors, `:empty`, all five attribute operators, case-sensitive
class/value matching, 98-character class selectors, `:not()` exclusions,
sibling-map counts across nested parents, late CSS-referenced attributes,
pseudo-state selectors, candidate indexing, selector-part reservation,
external classic/module script type preservation, standalone QuickJS ES module
evaluation, and cascade ordering beyond 16,384 rules. A pixel-level
comparison against a reference browser remains pending, so this page is not
certified to render correctly.

### Live GitHub check in QEMU (2026-11-14)

The browser was launched from the LIWUS desktop in QEMU (`q35`, 2 GiB guest
memory, e1000 user-mode networking), and `github.com` was entered in its address
bar. A bare hostname was normalized to `https://github.com`. The kernel fetched
the home-page HTML (525,965 bytes, HTTP 200), passed the serialized document to
the ring-3 QuickJS path (`document ready`, 1,163,099 bytes), downloaded CSS and
image resources (HTTP 200), and logged `Pagina renderizada:
https://github.com`. The final run did not report a kernel panic. This validates
one live navigation and its current fetch/render path, not general GitHub app
compatibility.

The first run with the enlarged live stylesheet set failed reproducibly in the
kernel heap while `parse_selector_chain` grew the selector-compound buffer: it
requested another 10 MiB when the 128 MiB heap had about 6 MiB available. The
renderer now prefetches and measures stylesheet input, reserves selector-part
storage before parsing, and reuses cached CSS within the existing per-resource
limit. A host regression checks that explicit reservation is honored, smaller
follow-up reservations do not shrink it, and a chained selector still matches
after parsing. `make test-unit` and `make test` passed after this change.

The first captured QEMU screen from the pre-fix run is visibly incomplete:
GitHub's page is sparse, with large blank regions and only some text, images,
and layout visible. That run logged `unsupported keyword: export`; inspection found that
the external-script inliner discarded `type="module"` and sent module code to
QuickJS as classic JavaScript. The inliner preserves module type and absolute
source URL, with a host regression for both module and classic external scripts.
A bounded URL-aware loader now resolves HTTP(S) relative, root-relative, and
scheme-relative static imports over the browser worker bridge. Import-map
resolution now handles exact specifiers and longest-prefix entries for a page's
first `type="importmap"` block. The live GitHub response contains a top-level
`imports` map, so this covers the form observed there; scopes and import-map
merging are still unsupported. The loader allows up to 64 imported files and
4 MiB of imported source, caps one response at 1 MiB, and shares one 20-second
wait budget across the page's module graph. Other in-flight `fetch()` responses
are queued while imports load and delivered after the module loader returns.
Dynamic `import()`, modulepreload, CORS enforcement, and non-HTTP(S) modules
remain unsupported. Import fetching blocks script execution and delays page
presentation until a response or the shared timeout. A later QEMU run completed
the same GitHub navigation without a kernel panic, TLS ring overflow,
partial-body acceptance, or JavaScript exceptions. Its current capture still
has a blank hero area and incomplete layout, so this is a successful partial
render, not a working GitHub application or desktop-browser parity.

The browser worker retries a failed imported-module request once when the
connection fails before receiving an HTTP status. It does not retry HTTP error
responses, and the retry count is fixed at two attempts total. This bounded
retry can recover a transient DNS/TLS failure without replaying ordinary
resource requests.

The follow-up visual diagnosis confirmed the hero `<img>` remained in the
QuickJS-produced DOM, its 15,850-byte PNG downloaded intact and decoded to
156×96, but three inline/flex ancestors had zero width and clipped it. The
renderer now resolves zero-width content boxes at element close using bounded
intrinsic descendant measurements and the available content width as a
fallback. A fresh QEMU screenshot now shows the duck banner image and caption.
The rest of the page still has incorrect alignment and clipped controls; this
fix does not establish general flex/grid layout fidelity.

The renderer now also maps integer `flex-grow` weights and the grow component
of the common `flex` shorthand to LVGL flex items. Form inputs honor supported
CSS width, percentage width, max-width, and grow values in addition to their
fallback dimensions. Percentage and zero bases in the shorthand (including GitHub's `flex:100%`, `flex:1 1 75%`, and `flex:1 1 0`) now override the declared width for row items; zero-basis items keep their CSS `min-width` and can grow into the remaining space. Host regressions cover the percentage, zero-basis, single-number, and cascade-reset forms. A repeated QEMU fetch of the public repository completed with `[WEB] Pagina renderizada` and no panic. Its final viewport still has horizontal overflow and incomplete content, so this change fixes a concrete sizing input but does not resolve the page layout. The QEMU captures are `docs/screenshots/liwusweb-github-flex-basis-final.png` and `docs/screenshots/liwusweb-github-flex-basis-scroll-diagnostic.png`; the latter was captured with a temporary scroll diagnostic and is not the default initial viewport. CSS flex shrink, fractional grow weights, nonzero pixel bases, grid tracks, and
full browser intrinsic sizing remain unsupported. Row-item percentage and zero
bases are mapped to explicit widths in this renderer; this is a targeted layout
improvement, not a complete flexbox implementation.

Grid tracks now evaluate the bounded `minmax(..., calc(percent - px - px))`
form used by GitHub's desktop repository shell. The percentage is calculated
from the available grid width and fixed sidebar/gutter terms remain fixed; this
lets the main column and sidebar retain their intended proportions. It remains
a constrained track-size implementation: intrinsic sizing, arbitrary `calc()`
expressions, and full CSS Grid placement are not implemented.

During that run, LIWUS received fewer bytes than the CDN's `Content-Length`
for several JavaScript and CSS resources. The TLS socket had acknowledged bytes
as soon as it copied them into its bounded 256 KiB ring, allowing the TCP window
to reopen before BearSSL consumed them. LIWUS now returns receive-window space
as the TLS reader drains the ring, and rejects TLS bodies whose byte count does
not match `Content-Length`. Repeating the run fetched the large CSS resources
completely and produced no partial-body or ring-overflow diagnostics.

Complete CSS then exposed two separate memory costs: `krealloc()` always copied
a growing allocation, and the `:nth-of-type()` metadata table reserved 2 MiB
for every page that needed it. `krealloc()` now extends the final bump
allocation in place when adjacent heap space is available. The sibling metadata
table is sized to twice the parsed element count, up to its existing 65,536
entry ceiling. A host regression checks the capacity policy and existing
sibling-index behavior. In QEMU, GitHub now reaches `[WEB] Pagina renderizada`
without exhausting the 128 MiB kernel heap. Its visible content remains
incomplete; CSS/layout fidelity and placement of other images still need work.

### Next compatibility work, ordered by dependency

1. Fix the live-page initial viewport: after `Pagina renderizada`, the public
   repository page has a large blank body and clipped navigation, while an
   800-pixel vertical diagnostic scroll reveals file rows. Trace the box
   positions and scroll extents, then lock the finding with a small HTML/CSS
   renderer fixture. Replace the synchronous import wait with a task-driven
   loader if the UI needs to stay responsive during module fetches.
2. Improve layout from the current flex-based grid approximation. Add focused
   host fixtures for GitHub-style grid tracks, intrinsic sizing, and positioned
   overlays before changing the live-site result.
3. Define and test script timing and DOM update checkpoints for classic,
   `async`, and `defer` scripts, then cover user interaction and mutation-driven
   re-rendering.
4. Keep a repeatable public-site smoke run separate from deterministic CI;
   external page changes, network failures, and anti-bot challenges must not
   make the normal kernel test suite flaky.

## HTTPS baseline

HTTPS uses BearSSL with a pinned, generic CA bundle compiled into the kernel.
The TLS client verifies the certificate chain, validity dates and requested
hostname; there is no API switch to skip certificate validation. The trust
bundle source is `src/net/trust/ca-certificates.pem`, and
`scripts/gen_bearssl_trust.py` emits the BearSSL anchors in
`src/net/trust/ca_trust_anchors.c`.

Certificate date checks use the CMOS RTC until NTP synchronizes the clock. If
the RTC date is invalid and NTP has not synchronized, certificate validation
fails closed. The bundled root set is a snapshot and must be refreshed as a
separate maintenance task. TLS currently negotiates through TLS 1.2; TLS 1.3,
browser cookies and WebSockets are not implemented by this change. QuickJS is
also used for bounded page-script execution in LiwusWeb.

The kernel does not yet provide a cryptographically seeded system RNG for TLS
on machines without RDRAND. Certificate verification is now strict, but this
increment alone is not a security sign-off for logins or sensitive sessions.

## Incremental direction

1. Keep static-page limits explicit and bounded; add regression fixtures for
   each compatibility change.
2. Keep transport security generic and standards-based, then improve the most common missing HTML/CSS features while preserving a safe
   fallback when a property or element is unsupported.
3. QuickJS 2026-06-04 is now vendored under `third_party/quickjs`; the port
   disables its pthread-backed `Atomics` path. OpenLibm is vendored under
   `third_party/openlibm` to supply the math implementation without host
   glibc. The Native SDK also gained the `localtime_r`/UTC offset surface used
   by the engine, and its x86_64 `stdint.h` now exposes the pointer-width
   limits needed to select QuickJS's 64-bit-safe `JSValue` representation.
4. `qjs SCRIPT.js` is the first ring-3 consumer. It exposes `console.log` and
   no filesystem, network, or module-loader APIs to scripts. The graphical
   persistent-page mode additionally exposes a bounded `fetch(url)` GET bridge
   described below. The optional
   `qjs --html PAGE.html SCRIPT.js` parses a bounded local HTML file and runs
   the separate script against its SDK DOM. `qjs --page PAGE.html` instead
   runs inline classic JavaScript blocks in document order. The bridge exposes
   `document.body`, `getElementById`, `querySelector`/`querySelectorAll`,
   `document.documentElement`, `document.head`, `document.body`,
   `createElement`, `createTextNode`, `createComment`,
   `createDocumentFragment`, `textContent`/`innerText`/`innerHTML`/`outerHTML`,
   `nodeType`, `nodeName`, `tagName`, `nodeValue`, `ownerDocument`,
   `parentNode`/`parentElement`, child and sibling navigation, `children`,
   `childNodes`, element-child navigation, `childElementCount`, `isConnected`,
   `contains`, `cloneNode`, stable JavaScript object identity for each node,
   `appendChild`, `insertBefore`, `removeChild`, `replaceChild`, and `remove`.
   Elements expose `id`, `className`, `classList.add/remove/contains/toggle`,
   `value`, `href`, `getAttribute`, `setAttribute`, `hasAttribute`,
   `toggleAttribute`, and `document.title`. Node methods also include
   `hasChildNodes`, `hasAttributes`, `getAttributeNames`, `isSameNode`,
   `isEqualNode`, `compareDocumentPosition`, `getRootNode`, and `normalize`.
   `children`, `childNodes`, `querySelectorAll`, and `getElementsBy*` return
   bounded arrays (snapshots), not live `HTMLCollection`/`NodeList` objects.
   `DocumentFragment` insertion moves its children into the target.
   Fragment parsing and DOM storage stay bounded by the SDK's node, depth,
   attribute, and source limits.
   Nodes also support synchronous `addEventListener`, `removeEventListener`,
   and `dispatchEvent`; event listeners support capture and `once`, and the
   `Event` object exposes its phase, bubbling, cancellation, `preventDefault`,
   `stopPropagation`, and `stopImmediatePropagation`. The runner caps registrations at
   256 listeners and event names at 63 bytes. The native renderer now forwards
   `click` for identified buttons, clickable containers with inline handlers,
   linked text, and `input`/`change` for identified text fields. Input values
   reach the live page context without rebuilding the LVGL tree on every key;
   DOM snapshots are rendered after a change/click or timer update when focus
   has left the field. Native pointer/keyboard coverage is limited to controls
   represented by the current HTML renderer. `preventDefault()` does not yet
   cancel native link/form behavior.
   After each script, the runner performs a bounded microtask checkpoint for
   Promise reactions and `queueMicrotask()` callbacks, with a maximum of 256
   jobs per checkpoint. It exposes `setTimeout`, `setInterval`, and their clear
   functions. The graphical browser keeps one QuickJS process alive per page,
   services timers and microtasks between IPC polls, and publishes updated DOM
   snapshots to the kernel renderer. The first snapshot is delivered before
   the page is rendered; later snapshots are acknowledged by the desktop before
   the worker publishes another one. Timers are capped at 64 active entries.
   Timer callback arguments are not yet supported.
   This is a practical DOM subset, not full WHATWG DOM conformance. Selectors
   support tag, `*`, `#id`, `.class`, attribute presence/equality/token/dash,
   prefix/suffix/substring operators and the `i` value modifier, compound
   selectors, groups, and
   descendant/child/adjacent/general-sibling combinators. Supported pseudo
   classes are `:root`, `:empty`, first/last/only-child, first/last/only-of-type,
   the corresponding `nth-*` forms, `:checked`, `:disabled`, `:enabled`,
   `:required`, `:optional`, `:link`, `:any-link`, `:not()`, `:is()`, and
   `:where()`. Selector escapes, namespaces, the complete CSS pseudo-class
   set, live collection objects, and many less-common DOM interfaces remain
   unsupported. Form-state matching models the attributes currently parsed by
   LIWUS; it does not implement every HTML constraint-validation or inherited
   disabled-state rule. The HTML parser is bounded and tolerant, but it is not
   the full HTML5 tree-construction algorithm. `isEqualNode()` compares the
   SDK's stored DOM representation, so source entity spellings that decode to
   the same character can compare differently. `getRootNode()` has no composed
   tree option because Shadow DOM is not implemented.
   External scripts are not loaded by `qjs --page`; static/dynamic imports
   remain unsupported because there is no module loader. The runner caps script
   input at 1 MiB, HTML at 4 MiB, QuickJS
   runtime allocations at 8 MiB, stack at 256 KiB, and interrupts execution
   after 500 QuickJS interrupt callbacks. Its static ELF64 `.lxe` builds and is
   included in the normal native-app/initrd build. QEMU runs verified a
   standalone local script (`QJS_RUNTIME_FINAL_OK`), `qjs --page` with DOM
   lookup, `textContent`, event target, bubbling and dispatch assertions
   (`QJS_DOM_EVENTS_OK`), and `qjs --html` with an external script mutating
   the parsed DOM (`QJS_HTML_SCRIPT_OK`). Hardware execution has not been
   verified.
   `qjs --dump-html PAGE.html` runs the page's inline classic scripts and
   inline modules without imports, then emits
   the mutated DOM as escaped HTML on stdout; `console.log` output goes to
   stderr in this mode so it cannot corrupt the document stream. The shared
   DOM serializer is bounded by the DOM's existing node, depth, and arena
   limits, omits comments, and rejects malformed element/attribute names.
   Host regression coverage verifies mutation, escaping, source-entity
   preservation, and parse/serialize round trips. A QEMU ring-3 regression runs
   an inline page through QuickJS and checks sibling/attribute/structural
   selectors, node identity/equality/position, fragment insertion, `normalize`,
   decoded text and attributes, escaped output, and serialized HTML through a
   native pipe (`QJS_PIPE_DOM_PASS`).
   `webtext --js URL [SAVE_PATH]` is a fetched-page consumer: it saves
   the downloaded source, starts `qjs --dump-html` as an unprivileged child,
   reads transformed HTML through a pipe capped at 4 MiB, and displays its text
   in the terminal. External scripts and imported modules remain unsupported.
   A QEMU serial
   integration test now boots with E1000 user networking, waits for DHCP,
   downloads a local HTTP fixture from the QEMU host gateway, verifies that
   inline JavaScript sees decoded source entities and changes the DOM, checks
   the rendered text, and reads back the unchanged saved source. Run
   `make all external-sdk-build test-unit` followed by
   `python3 tests/harness/run_shell_integration.py --webtext-js-only`; the test
   uses a temporary ISO and disk. The broad shell integration also ran through
   this scenario successfully, then stopped later because `demo` ran the libc
   demo while that test expected native hello output; the alias mismatch is
   outside this browser test and remains to be investigated. The graphical
   browser uses the same bounded QuickJS DOM runner, but does not provide a
   full web platform.
5. Keep the engine and DOM bridge as ring-3 SDK components, separate from the
   native LIWUS ABI and kernel. The graphical page context now stays alive per
   tab and receives native `click`, `input`, and `change` events over bounded
   IPCFS channels. The event loop polls cooperatively, performs bounded
   microtask checkpoints, runs timers, and waits for the desktop to acknowledge
   each DOM snapshot before replacing it.
6. The top-level HTML GET now runs in a dedicated kernel task, so a slow server
   does not stop the desktop input/render loop. The network stack still needs
   `net_poll()` from the desktop loop. Script, CSS, and image resource fetches
   during initial page processing/rendering remain synchronous. Persistent
   QuickJS pages can issue `fetch(url)` GETs; the kernel worker resolves
   absolute, root-relative, and directory-relative URLs and returns a response
   with `status`, `ok`, `text()`, and `json()`. At most eight requests may be
   pending, responses are limited to 1 MiB, and a page-generation ID discards
   late replies after navigation. This is not the full Fetch API: request
   options, POST, headers, streaming, XHR, CORS, cookies, abort signals, and
   service workers are absent. Script, CSS, and image resource fetches during
   initial processing/rendering still run synchronously. The browser also lacks
   event-cancellation parity, full keyboard event objects, and input-focus
   restoration after a page snapshot. This milestone is a runtime foundation,
   not full browser compatibility.
7. Add SVG and additional image formats after choosing a decoder with acceptable
   memory use on LIWUS.

The bounded script integration executes inline and selected external classic
scripts from fetched pages, then applies their DOM changes before rendering. A
working engine alone will not make sites such as GitHub or ChatGPT fully usable:
they also need browser APIs, real input events, broader CSS, modern TLS/network
behavior, and substantial page-specific JavaScript support.

## Browser runtime milestone status (2026-11-14)

The persistent page runtime, IPC snapshot acknowledgement, bounded timers and
microtask checkpoints, native `click`/`input`/`change` forwarding, asynchronous
top-level HTML GET, and the limited asynchronous JavaScript `fetch(url)` GET
bridge are implemented in this workspace. `make all` completed with
`-Werror`; `make test-all` passed the host suite and serial QEMU validation in
test, ring-3, and SMAP configurations (51 unique kernel cases and 23 ring-3
cases per configuration). A separate live QEMU navigation fetched and
partially rendered `https://github.com/`; this checks one current site response
and does not establish broad site compatibility. The new browser paths have
not been validated on physical hardware.

## GitHub live-page check and memory findings (2026-11-14)

A QEMU run of the graphical browser exposed a reproducible parser fault while
QuickJS consumed a 431,609-byte inline script from GitHub's current home page.
`liw_webdom_parse()` stored its input pointer in the same reallocating arena
used for DOM strings; arena growth invalidated the parser's source pointer.
The parser now reads from the caller-owned input buffer, and a host regression
parses a script of that exact length after forcing arena growth and checks its
beginning, middle, and end. The regression passes under ASan/UBSan.

The same live-mode boot exposed a separate kernel-heap limit: desktop startup
uses about 94.7 MiB before page parsing (including the 48 MiB RAM disk, copied
initrd, LVGL pool, and framebuffer). The 128 MiB target left too little room for
the parsed stylesheets and page state. The requested contiguous heap target is
now 192 MiB; PMM continues to fall back to the largest available region down to
the existing 8 MiB minimum. On the tested QEMU machine with 2 GiB RAM, the heap
was 192 MiB and CSS parsing reached about 137.6 MiB without panic.

The screenshot confirms visible content from `https://github.com/` rendered in
the real LIWUS desktop. It is only a partial page render: sections below the
first visible content are absent, and the search field was not validated.
This does not prove repository navigation, sign-in, full JavaScript behavior,
physical-hardware stability, or parity with a mainstream browser. After these
changes, `make test-all` passed with 51 unique kernel cases and 23 ring-3 cases
in each of the test, ring-3, and SMAP serial configurations.

## Long-page layout follow-up (2026-11-14)

The public repository page uses 7,548 LVGL objects, so the former 2,048-object
limit truncated it before the file list. The renderer cap is now 8,192. LVGL
coordinates are configured as 32-bit so long README and page layouts do not
wrap at the previous 16-bit boundary. After rendering, the browser measures
the laid-out subtree and adds a non-layout scroll extent marker; content-driven
wrappers are expanded bottom-up in bounded passes before the scroll range is
finalized. HTML container boxes allow visible overflow while their measured
ancestors reserve space.

CSS Grid now has a bounded track-width subset: up to four fixed-pixel,
percentage, or `fr` columns; `minmax()` uses its maximum bound when that bound
is one of those supported forms. This gives simple two-column repository
layouts usable widths. It does not implement grid auto-placement, implicit
tracks, named lines, repeat expansion, full `calc()` sizing, or intrinsic
track sizing.

The document viewport now enables horizontal as well as vertical scrolling.
The post-render extent marker includes the maximum right and bottom coordinates,
so wide content can be reached instead of being silently clipped. This is a
fallback for the renderer's remaining width/layout errors; it does not reflow a
desktop page into a narrow viewport.

QEMU validation fetched the live `https://github.com/rancidavi-dotcom/LiwusOS`
page (HTTP 200), ran the current QuickJS/page processing path, completed
resource requests, and logged `Pagina renderizada`. A screenshot after a
diagnostic 800-pixel scroll shows repository file rows that were previously
unreachable. This is still a partial render: at the top, large blank gaps
remain, the right-side navigation is clipped at the initial horizontal offset,
and the layout is not equivalent to GitHub in Chromium. The horizontal scrollbar
can reach overflow but does not correct the layout. The diagnostic URL and scroll were
injected only for this QEMU check; they are not compiled into the normal
desktop. The final-source QEMU capture after scrolling to repository files,
including the horizontal scrollbar, is saved at
[`docs/screenshots/liwusweb-github-qemu-partial.png`](screenshots/liwusweb-github-qemu-partial.png).
Physical hardware has not been tested.
`make test-unit` and `make test-all` pass after the grid and long-page changes;
the latter reports 51 kernel cases and 23 ring-3 cases passing in test, ring-3,
and SMAP configurations. QEMU proves that this page reaches and displays the
repository content, while the remaining visual defects above mean it is still
not certified for GitHub app use or browser parity.

### CSS numeric input bounds

The host CSS parser also now uses bounded 64-bit intermediates when reading
remote numeric lengths, integer prefixes, and color alpha values, then clamps
lengths to the style representation. This prevents extremely long CSS numbers
from overflowing signed arithmetic before the renderer applies its normal
limits. A regression covers oversized pixel lengths and RGBA alpha under
ASan/UBSan. It is input-hardening only; it does not change the supported CSS
syntax or resolve the layout defects visible in the QEMU capture.

The desktop now also passes the browser content panel's actual height into CSS
evaluation for `vh` lengths. Callers that provide only a width retain the
previous bounded aspect-ratio estimate. A host regression verifies that
`100vh` resolves to the supplied 768-pixel viewport height. This improves
viewport-sized sections but does not implement dynamic viewport units or
browser zoom semantics. `@media` now checks bounded `min-height` and
`max-height` pixel conditions alongside its existing width conditions; host
cases cover both a matching 768-pixel viewport and a rejected 600-pixel one.
It also recognizes range-style `width`/`height` comparisons (`>=`, `<=`, `>`
and `<`) with `px` and `rem` lengths, which covers common modern breakpoint
syntax such as GitHub's `(width>=34rem)` form. This remains a small media-query
subset: compound boolean logic, orientation, resolution, hover, and preference
features are not evaluated faithfully. Flex/grid gaps now keep separate row and
column values, and the `gap: row column` shorthand is parsed with subsequent
longhand overrides following declaration order. CSS `min-height` now resolves
through the same bounded length parser, including `vh`, and establishes a
minimum box height while preserving content-driven growth; an ASan/UBSan host
regression checks cascade interaction with an explicit `height`. Other
shorthands such as `margin`/`padding` still have their own bounded parser
behavior.

### Large page DOM bounds

A fresh QEMU fetch of the public LIWUS GitHub repository showed the ring-3 DOM
rejecting the page before scripts ran. The runtime input had grown to 2,722,283
bytes after external modules were expanded; its DOM arena duplicated source
text into the same bounded string store and stopped at byte 1,579,504. The
arena budget now allows two copies of the existing 4 MiB input cap plus bounded
node/attribute metadata. DOM capacity is 32,768 nodes and depth 256; the
renderer remains capped at 8,192 LVGL objects. Host ASan/UBSan regressions cover
a 2.5 MiB raw script, a 20,000-element page, and 200 nested elements. The
captured public repository HTML parses to 19,089 DOM nodes in the host probe.
End-to-end QEMU confirmation now succeeds: after this arena change, the page
completed module and image requests and logged `Pagina renderizada` without a
DOM parse error or kernel panic. The new screenshot is a real QEMU browser
window capture at
[`docs/screenshots/liwusweb-github-qemu-dom-arena.png`](screenshots/liwusweb-github-qemu-dom-arena.png).
The final frame still shows substantial blank space and clipped/misaligned
repository navigation; a transient loading frame briefly showed README text,
but that content was not retained in the final render. A separate diagnostic
capture after an 800-pixel vertical scroll shows file rows at
[`docs/screenshots/liwusweb-github-qemu-scroll-diagnostic.png`](screenshots/liwusweb-github-qemu-scroll-diagnostic.png).
This demonstrates that repository content exists below the initial viewport,
although the row layout remains visibly incomplete. After adding exact two-term `calc()` evaluation for the page root's inline
`min-height:calc(100vh - 64px)`, a fresh QEMU capture still reaches
`Pagina renderizada` but remains sparse, so that CSS issue was real but was not
the root cause of the layout failure. This confirms the DOM limit was removed
for this page, not that GitHub's layout or application works correctly. After
the final CSS changes,
`make test-unit`, `make test-all`, and `make all` passed; a normal `make run-log`
boot reached `LIWUS_BOOT_READY` without the diagnostic navigation hook. Physical
hardware has not been tested.

### CSS variable cascade and first QuickJS snapshot (2026-11-14)

Host ASan/UBSan coverage now exercises element-scoped custom properties,
inheritance, alias resolution, sibling isolation, inline values, and
`!important` precedence. This caught and fixed a cascade-order bug: the browser
had computed ordinary declarations before applying important custom properties,
so a declaration such as `width:var(--pane)` could use the stale value. Custom
properties are now finalized before ordinary declarations are evaluated.

The live QEMU run also exposed an ordering error in the persistent QuickJS
bridge. It notified the desktop that a DOM snapshot was ready before writing
the initial post-script HTML. The bridge now serializes and writes that first
snapshot before sending the notification. A fresh `q35`/EHCI/e1000 run fetched
the public LIWUS repository page, including its external resources, and
completed without a kernel panic. The actual browser-window capture is
[`docs/screenshots/liwusweb-github-qemu-ring3-snapshot-partial.png`](screenshots/liwusweb-github-qemu-ring3-snapshot-partial.png).
It still shows large blank regions, horizontal overflow, and clipped
repository navigation. The change ensures the transformed DOM can reach the
renderer; it does not fix the remaining GitHub layout defects or demonstrate
pixel parity. No physical-hardware validation was done.

Verification after these changes: `make all` and `make test-all` passed;
the latter reports 51 kernel cases and 23 ring-3 cases in each of the standard,
ring-3, and SMAP QEMU runs. The QEMU live-page run made 78 successful HTTP/TLS
resource responses and reached `Pagina renderizada`.

### GitHub PageLayout custom-property capacity (2026-11-14)

Inspection of GitHub's live PageLayout CSS and a host-side run of the LIWUS CSS
cascade found that the root component fills the previous 24-entry scoped CSS
variable limit. Increasing it to 64 and then 128 was insufficient: repeatable
probes over the saved stylesheet set still showed a full inherited-variable
array and dropped the pane's inline `--pane-width-size`. The scoped limit now
matches the stylesheet's 512-variable bound. A host-side probe over 19 saved
GitHub stylesheets now retains 218 variables for the PageLayout pane, resolves
its width to 256 px, and computes content/pane orders 2/3 at a 984 px viewport.
QEMU booted with 2 GiB, fetched the live page and reached `Pagina renderizada`
without an allocation failure. This adds roughly 2.5 MiB to the parser's
bounded per-depth style storage compared with the former 128-entry array.

The live page also uses `@media (width<=calc(48rem - .02px))` to select its
narrow layout. The media evaluator now supports bounded two-term `calc()` pixel
and `rem` lengths and rejects conditions whose lengths it cannot evaluate,
instead of incorrectly applying an unrecognized breakpoint at desktop widths.
For wrapping flex rows, a CSS `flex: 100%` item is treated as a full-track item;
zero-basis grow items give descendants the containing line width during the
streaming parse so nested `width:100%` boxes are not permanently created at the
60 px minimum. In the QEMU layout probe, the repository content region measured
668 px and its sidebar measured 256 px at a 938 px inner width; the sidebar was
placed at x=674. The live QEMU run reached `Pagina renderizada` after fetching
the page resources without an allocation failure.

The real-window capture is
[`docs/screenshots/liwusweb-github-page-layout-qa.png`](screenshots/liwusweb-github-page-layout-qa.png).
It shows repository rows and the two-column region, but still has blank areas,
clipped navigation, horizontal overflow, and missing page detail. This is a
visible layout improvement, not parity with Chromium or support for GitHub's
full application. A previous whole-subtree reflow prototype kept QEMU at
“Renderizando pagina...” for over four minutes and was removed. Physical
hardware was not tested.

### Flex item sizing and latest GitHub capture (2026-11-14)

The reference screenshot's repository navigation is a single horizontal row.
QEMU instrumentation traced LIWUS's wrapping to auto-sized `li` flex items
measuring 938 px each: a zero-height, unsupported SVG placeholder was being
counted as visible content. The renderer now measures auto-sized row flex items
from visible descendants, skips zero-height placeholders, accounts for
horizontal padding/gaps, and stretches auto-width children of column flex
containers by default. The CSS `align-items` default is now `stretch`, while
explicit `start`, `center`, and `end` values remain distinct. A QEMU first-pass
capture shows the repository tabs on one horizontal line and file rows below:
[`docs/screenshots/liwusweb-github-flex-nav-initial.png`](screenshots/liwusweb-github-flex-nav-initial.png).

The post-JavaScript DOM snapshot still does not retain that navigation row; the
final frame shows the repository title and file list, with the page's large
blank areas and missing sidebar still present:
[`docs/screenshots/liwusweb-github-flex-nav-postjs.png`](screenshots/liwusweb-github-flex-nav-postjs.png).
This narrows the remaining navigation issue to the post-script DOM/render pass;
the initial HTML/CSS layout now measures the tabs correctly.

The scoped-variable count is now 16-bit, so the configured 512-entry limit is
represented without wrapping at 255. Selectors may contain three attribute
conditions. An experiment raising the per-style variable capacity to 2,048
caused a desktop task stack overflow in QEMU. Per-element environments
therefore stay at 512; the stylesheet-wide token table is handled separately.
At that point the captured page had not achieved dark-theme parity, even though
`matchMedia` and CSS `prefers-color-scheme` recognized the dark preference.

The final QEMU run used 2 GiB, q35, EHCI keyboard/mouse, and e1000 networking.
It fetched 78 HTTP/TLS resources and reached `Pagina renderizada` without a
kernel panic. `make all` completed without compiler warnings. This is QEMU
evidence only; physical hardware and complete GitHub behavior remain unverified.

### Grid fraction track sizing (2026-11-14)

The bounded CSS Grid renderer now resolves each supported `fr` track to an
explicit pixel width before adding its item to the LVGL row. Fixed and
percentage tracks are subtracted first; supported `auto` tracks retain the
existing bounded sidebar estimate, and the remaining space is divided by the
tracks' `fr` weights. This avoids relying on LVGL `flex-grow` to emulate Grid's
track sizing. The live GitHub repository page still has blank regions and a
missing sidebar in the QEMU capture; its main PageLayout uses Flexbox, so this
Grid change does not fix that page. `make all` succeeds. This remains a bounded
track subset, not full CSS Grid sizing or auto-placement.

### Root theme preference and CSS token capacity (2026-11-14)

The captured GitHub HTML sets `data-color-mode="auto"` and
`data-light-theme="light"`, reflecting the preference attached to the request
that produced it. LIWUS already evaluates `prefers-color-scheme: dark` and
JavaScript `matchMedia` as dark, but CSS selector matching still saw GitHub's
light-theme attribute. The HTML renderer now maps the root element's
`data-light-theme` value to `dark` when the LIWUS dark preference is active. It
keeps `data-color-mode="auto"` so GitHub's own dark token rules are selected.

A host-side diagnostic against the saved live GitHub stylesheets found that
root theme tokens occur after more than 1,000 distinct variables. The
stylesheet-wide token table is now bounded at 2,048 entries, while the
per-element inherited environment stays bounded at 512 entries. With the root
attribute override and matched-root-variable collection, the diagnostic
resolved GitHub's root background to `#0d1117` and foreground to `#f0f6fc`.
`make all` passes without compiler warnings and produces `liwusos.iso`.

QEMU then loaded the repository after DHCP, fetched 77 successful HTTP 200
responses, and logged `Pagina renderizada`. The real browser-window capture is
[`docs/screenshots/liwusweb-github-dark-theme-qemu.png`](screenshots/liwusweb-github-dark-theme-qemu.png).
It confirms the dark background and light text are now applied after page
rendering. The screenshot still has large blank regions, no repository sidebar,
missing file metadata, and clipped repository details; it is not Chromium
parity. This was a QEMU run only.
