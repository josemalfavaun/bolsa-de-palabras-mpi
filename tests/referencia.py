#!/usr/bin/env python3
"""Implementacion de REFERENCIA (independiente del C++) de la bolsa de palabras.

Uso: python3 tests/referencia.py <archivo_urls_locales> <salida.csv>

Aplica las mismas reglas que el tokenizador de C++ pero con Python puro:
  - solo cuerpo entre "*** START OF" y "*** END OF";
  - minusculas ASCII; palabra = racha de [a-z0-9]; se descartan las de 1 caracter.
Sirve para comprobar que el C++ cuenta bien (no solo que serial y MPI coincidan
entre si). Solo acepta rutas locales.
"""
import re
import sys
from collections import Counter


def cuerpo(datos: bytes) -> bytes:
    inicio, fin = 0, len(datos)
    for marca in (b"*** START OF", b"***START OF"):
        s = datos.find(marca)
        if s != -1:
            salto = datos.find(b"\n", s)
            inicio = len(datos) if salto == -1 else salto + 1
            break
    for marca in (b"*** END OF", b"***END OF"):
        e = datos.rfind(marca)
        if e != -1:
            if e >= inicio:
                fin = e
            break
    return datos[inicio:fin]


def main():
    urls = [l.strip() for l in open(sys.argv[1]) if l.strip() and not l.startswith("#")]
    conteos = []
    for ruta in urls:
        datos = open(ruta, "rb").read()
        conteos.append(Counter(re.findall(rb"[a-z0-9]{2,}", cuerpo(datos).lower())))
    vocabulario = sorted(set().union(*[set(c) for c in conteos]))
    with open(sys.argv[2], "w") as f:
        f.write("libro," + ",".join(p.decode() for p in vocabulario) + "\n")
        for i, c in enumerate(conteos):
            f.write(str(i) + "," + ",".join(str(c.get(p, 0)) for p in vocabulario) + "\n")


if __name__ == "__main__":
    main()
