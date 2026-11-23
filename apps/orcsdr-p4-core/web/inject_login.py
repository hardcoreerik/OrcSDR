#!/usr/bin/env python3
"""inject_login.py <m5.html.gz in> <m5.html.gz out>: adds <script src="/m5/orclogin.js"></script> before the page's first script.

The bundled page is not modified in its source repository; the copy embedded in the firmware gets this one extra line so the login script
(web/orclogin.js) runs before the page's own scripts and can wrap fetch(). Fails (non-zero) if the page has no script tag to anchor on.
"""
import gzip
import sys

src, dst = sys.argv[1], sys.argv[2]
html = gzip.decompress(open(src, "rb").read()).decode("utf-8")
tag = '<script src="/m5/orclogin.js"></script>\n'
if tag in html:
    out = html
else:
    i = html.find("<script")
    if i < 0:
        sys.exit("no <script> in " + src)
    out = html[:i] + tag + html[i:]
open(dst, "wb").write(gzip.compress(out.encode("utf-8"), 9, mtime=0))
