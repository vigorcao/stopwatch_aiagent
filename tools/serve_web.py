"""Serve the web simulator with caching off, so a rebuild shows up on a normal reload.

Usage: python3 tools/serve_web.py [port]   (serves M5StopWatch-UserDemo/web/build, default port 8765)
"""
import functools, http.server, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "M5StopWatch-UserDemo", "web", "build")


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
handler = functools.partial(NoCacheHandler, directory=ROOT)
print(f"http://localhost:{port}/stopwatch.html")
http.server.ThreadingHTTPServer(("", port), handler).serve_forever()
