#!/usr/bin/env python3
"""Compara el tamano del vocabulario de nuestro tokenizador contra variantes de CountVectorizer.

Sirve para encontrar por que el programa en C++ da un vocabulario distinto al de la libreta
del curso. Calcula el tamano del vocabulario (union de los k libros) con varias reglas y marca
la que coincide con el numero objetivo.

Uso:
    python3 tests/comparar_vocabulario.py data/cache/urls_locales.txt --objetivo 15170

El archivo de entrada es el mismo que usa el programa: una ruta local por renglon (las lineas
vacias y las que empiezan con # se ignoran). No descarga nada.
"""
import argparse
import re
import sys

from sklearn.feature_extraction.text import CountVectorizer


def leer_rutas(archivo):
    rutas = []
    for linea in open(archivo, encoding="utf-8"):
        linea = linea.strip()
        if linea and not linea.startswith("#"):
            rutas.append(linea)
    return rutas


def cuerpo(texto):
    """Misma regla que quitar_encabezado_y_pie de src/comun.hpp."""
    ini, fin = 0, len(texto)
    s = texto.find("*** START OF")
    if s < 0:
        s = texto.find("***START OF")
    if s >= 0:
        salto = texto.find("\n", s)
        ini = len(texto) if salto < 0 else salto + 1
    e = texto.rfind("*** END OF")
    if e < 0:
        e = texto.rfind("***END OF")
    if e >= 0 and e >= ini:
        fin = e
    return texto[ini:fin]


def nuestro(texto):
    """Tokenizador del C++: bytes ASCII [a-z0-9], minusculas ASCII, minimo 2 caracteres."""
    datos = texto.encode("utf-8")
    return [t.decode("ascii") for t in re.findall(rb"[a-z0-9]+", datos.lower()) if len(t) >= 2]


def con_sklearn(**kw):
    analizador = CountVectorizer(**kw).build_analyzer()
    return analizador


def solo_letras(texto):
    return [t for t in re.findall(r"[a-z]+", texto.lower()) if len(t) >= 2]


def vocabulario(textos, analizador):
    v = set()
    por_libro = []
    for t in textos:
        s = set(analizador(t))
        por_libro.append(len(s))
        v |= s
    return v, por_libro


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archivo_rutas")
    ap.add_argument("--objetivo", type=int, default=None, help="tamano de vocabulario a reproducir")
    args = ap.parse_args()

    rutas = leer_rutas(args.archivo_rutas)
    completos = [open(r, encoding="utf-8", errors="replace").read() for r in rutas]
    cuerpos = [cuerpo(t) for t in completos]

    variantes = [
        ("1. NUESTRO (C++): cuerpo, [a-z0-9], min 2", cuerpos, nuestro),
        ("2. sklearn por defecto, cuerpo", cuerpos, con_sklearn()),
        ("3. sklearn por defecto, texto completo (con encabezado y pie)", completos, con_sklearn()),
        ("4. sklearn + stop_words='english', cuerpo", cuerpos, con_sklearn(stop_words="english")),
        ("5. sklearn + stop_words='english', texto completo", completos, con_sklearn(stop_words="english")),
        ("6. sklearn + strip_accents='unicode', cuerpo", cuerpos, con_sklearn(strip_accents="unicode")),
        ("7. sklearn con token_pattern [a-z]{2,} (sin digitos ni _), cuerpo", cuerpos, con_sklearn(token_pattern=r"(?u)\b[^\W\d_]{2,}\b")),
        ("8. solo letras a-z, cuerpo", cuerpos, solo_letras),
        ("9. solo letras a-z, texto completo", completos, solo_letras),
        ("10. NUESTRO tokenizador pero con texto completo", completos, nuestro),
        ("11. sklearn por defecto, cuerpo, min_df=2", cuerpos, None),
    ]

    resultados = {}
    print(f"Libros leidos: {len(rutas)}")
    print(f"{'variante':<70} {'vocab':>8}  por libro")
    for nombre, textos, f in variantes:
        if f is None:  # min_df=2 se calcula con conteos de documento
            cv = CountVectorizer(min_df=2)
            cv.fit(textos)
            v = set(cv.vocabulary_)
            por = ["-"] * len(textos)
        else:
            v, por = vocabulario(textos, f)
        resultados[nombre] = v
        marca = ""
        if args.objetivo is not None:
            d = len(v) - args.objetivo
            marca = "  <== COINCIDE" if d == 0 else f"  (diferencia {d:+d})"
        print(f"{nombre:<70} {len(v):>8}  {por}{marca}")

    nuestro_v = resultados[variantes[0][0]]
    sk_v = resultados[variantes[1][0]]
    print("\nNUESTRO contra sklearn por defecto (ambos sobre el cuerpo):")
    solo_n = sorted(nuestro_v - sk_v)
    solo_s = sorted(sk_v - nuestro_v)
    print(f"  solo en nuestro:  {len(solo_n):>6}   ejemplos: {solo_n[:25]}")
    print(f"  solo en sklearn:  {len(solo_s):>6}   ejemplos: {solo_s[:25]}")
    con_guion = [p for p in solo_s if "_" in p]
    con_acento = [p for p in solo_s if any(ord(c) > 127 for c in p)]
    print(f"  de las de sklearn, con guion bajo: {len(con_guion)}, con caracteres no ASCII: {len(con_acento)}")

    if args.objetivo is not None and not any(len(v) == args.objetivo for v in resultados.values()):
        print("\nNinguna variante coincide con el objetivo. Revisa que la libreta use los mismos libros y las mismas "
              "URLs (distintas ediciones del texto cambian el vocabulario) y mira por libro con las columnas de arriba.")


if __name__ == "__main__":
    sys.exit(main())
