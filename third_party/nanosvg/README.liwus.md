# NanoSVG

The two headers are unmodified upstream sources from
https://github.com/memononen/nanosvg, commit
`239e102ec2c691f2902e20ace2ed36ee4a35cfe6`. Copyright and license notices
are included in each header.

These files are not yet built into LIWUS. A kernel integration needs bounded
allocation, floating-point/math support, safe lifetime management for LVGL
image buffers, and CSS `currentColor` handling. A host-only probe successfully
rasterized a 16×16 GitHub directory Octicon; this does not validate kernel
execution or display in LiwusWeb.
