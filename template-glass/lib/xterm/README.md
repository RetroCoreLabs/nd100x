# xterm.js (vendored)

The exact files the page used to load from cdn.jsdelivr.net, copied here so
the Glass UI has no third-party script origin (Chrome warned about the
document.write cross-site loads, Edge logged "Tracking Prevention" for
every jsdelivr request). Same bytes, same versions:

| File | Package | Version | Source |
|------|---------|---------|--------|
| xterm.min.js, xterm.min.css | xterm | 5.1.0 | https://cdn.jsdelivr.net/npm/xterm@5.1.0/ |
| xterm-addon-fit.min.js | xterm-addon-fit | 0.7.0 | https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.7.0/ |
| xterm-addon-canvas.min.js | xterm-addon-canvas | 0.3.0 | https://cdn.jsdelivr.net/npm/xterm-addon-canvas@0.3.0/ |

License: MIT (xterm.js authors, SourceLair, Christopher Jeffrey) - see
https://github.com/xtermjs/xterm.js/blob/master/LICENSE

Loaded by template-glass/index.html and template-glass/terminal-popout.html
with plain <link>/<script> tags; the Makefile adds the ?v= cache-bust token.
