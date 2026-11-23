#!/usr/bin/env python3
"""Servidor HTTP local que simula una descarga lenta (para probar la ruta de curl sin internet).

Uso: python3 tests/servidor_lento.py <directorio> [retardo_seg=1.0] [puerto=8765]

Sirve los archivos de <directorio> en http://127.0.0.1:<puerto>/<archivo>,
esperando <retardo_seg> antes de responder cada peticion.
"""
import functools
import http.server
import sys
import time


class Lento(http.server.SimpleHTTPRequestHandler):
    retardo = 1.0

    def do_GET(self):
        time.sleep(self.retardo)
        super().do_GET()

    def log_message(self, *args):
        pass


def main():
    directorio = sys.argv[1]
    Lento.retardo = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    puerto = int(sys.argv[3]) if len(sys.argv) > 3 else 8765
    manejador = functools.partial(Lento, directory=directorio)
    servidor = http.server.ThreadingHTTPServer(("127.0.0.1", puerto), manejador)
    print(f"Sirviendo {directorio} en http://127.0.0.1:{puerto} (retardo {Lento.retardo}s)", flush=True)
    servidor.serve_forever()


if __name__ == "__main__":
    main()
