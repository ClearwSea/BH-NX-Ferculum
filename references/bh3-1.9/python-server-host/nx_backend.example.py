"""Non-game HTTP demonstration. Rename to nx_backend.py for host smoke checks.

This example does not implement the game's wire protocol or business logic.
"""
from http.server import BaseHTTPRequestHandler

class DemoHttp(BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'Ferculum host adapter is ready. Game backend is not implemented.\n'
        self.send_response(501)
        self.send_header('Content-Type','text/plain; charset=utf-8')
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)

def game_handler(socket, address, server):
    raise NotImplementedError('Provide your game protocol handler')

def configure(runtime, data):
    return game_handler, DemoHttp, 0
