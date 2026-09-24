#!/usr/bin/env python3
# Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
# Serve the BUNDLED Code-OSS 1.92.2 web workbench (one 31 MB
# workbench.web.main.js) instead of @vscode/test-web's sources mode, which
# hands the browser ~2000 separate AMD modules - on AROS that never finished
# inside a 15 minute run.  index.html is test-web's own generated page with
# /static/sources rewritten to /static/build.
import http.server, os, sys, functools

ROOT = sys.argv[1]
INDEX = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'index.html')

class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        path = path.split('?', 1)[0].split('#', 1)[0]
        if path in ('/', '/index.html'):
            return INDEX
        if path.startswith('/static/build/'):
            return os.path.join(ROOT, path[len('/static/build/'):])
        return os.path.join(ROOT, path.lstrip('/'))

    def end_headers(self):
        self.send_header('Cache-Control', 'public, max-age=3600')
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

http.server.ThreadingHTTPServer.allow_reuse_address = True
http.server.ThreadingHTTPServer(('0.0.0.0', 8085),
    functools.partial(Handler)).serve_forever()
