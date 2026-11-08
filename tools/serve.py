"""VS Code foreground HTTP task with explicit readiness and same-project reuse."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import sys
from urllib.error import URLError
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]


class ProjectHTTPServer(ThreadingHTTPServer):
    # Windows SO_REUSEADDR can admit two listeners on the same port.
    allow_reuse_address = sys.platform != "win32"

    def server_bind(self) -> None:
        if sys.platform == "win32":
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)

        super().server_bind()


class ProjectRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    # Windows registry MIME overrides can classify .mjs as text/plain.
    # ES module consumers require a JavaScript MIME type regardless of that registry.
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".wasm": "application/wasm",
    }


def serves_current_project(port: int) -> bool:
    for name in ("pbd_model.html", "src/app.js", "wasm/pbd_core.wasm"):
        expected = (ROOT / name).read_bytes()

        try:
            with urlopen(f"http://127.0.0.1:{port}/{name}", timeout=2) as response:
                if response.read(len(expected) + 1) != expected:
                    return False
        except (OSError, URLError):
            return False

    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000)

    args = parser.parse_args()

    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")

    print("PBD_SERVER_STARTING", flush=True)

    handler = partial(ProjectRequestHandler, directory=str(ROOT))

    try:
        server = ProjectHTTPServer(("127.0.0.1", args.port), handler)
    except OSError as error:
        if serves_current_project(args.port):
            print(f"PBD_SERVER_READY http://127.0.0.1:{args.port}/pbd_model.html (existing server)", flush=True)

            return 0

        print(f"PBD_SERVER_ERROR: port {args.port} is unavailable: {error}", file=sys.stderr, flush=True)

        return 1

    with server:
        print(f"PBD_SERVER_READY http://127.0.0.1:{args.port}/pbd_model.html", flush=True)

        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
