"""Host user-supplied nx_backend handlers over the _nxipc memory transport."""
import io
import threading
import traceback

class HttpMemorySocket:
    def __init__(self, request):
        self.input = io.BytesIO(request)
        self.output = bytearray()
    def makefile(self, mode, *args, **kwargs):
        if 'r' not in mode:
            raise ValueError('Use sendall for HTTP output')
        return self.input
    def sendall(self, data):
        self.output.extend(data)

class GameMemorySocket:
    def __init__(self, native, channel):
        self.native, self.channel = native, channel
    def settimeout(self, timeout):
        self.native.settimeout(self.channel, timeout)
    def recv(self, size):
        return self.native.recv(self.channel, size)
    def sendall(self, data):
        self.native.sendall(self.channel, data)

def serve(native, runtime, data):
    from nx_backend import configure
    game_handler, http_handler, verified = configure(runtime, data)
    threading.stack_size(1024 * 1024)
    def game(channel):
        try:
            if hasattr(native, 'schedule_thread'):
                native.schedule_thread()
            game_handler(GameMemorySocket(native, channel), ('127.0.0.1', 0), None)
        except Exception:
            traceback.print_exc()
        finally:
            native.server_close(channel)
    native.ready(verified)
    while True:
        kind, token, payload = native.next_job()
        if kind == 'stop':
            return
        if kind == 'connect':
            threading.Thread(target=game, args=(token,), daemon=True).start()
        elif kind == 'http':
            try:
                request = HttpMemorySocket(payload)
                http_handler(request, ('127.0.0.1', 0), None)
                native.http_done(token, bytes(request.output))
            except Exception:
                traceback.print_exc()
                native.http_done(token, b'HTTP/1.0 500 Error\r\nContent-Length: 0\r\n\r\n')
        else:
            raise ValueError('Unknown native job: ' + str(kind))
        del token, payload
