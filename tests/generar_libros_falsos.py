#!/usr/bin/env python3
"""Genera "libros" falsos con formato de Project Gutenberg para probar SIN red.

Uso: python3 tests/generar_libros_falsos.py <directorio> <num_libros> <palabras_por_libro>

Crea <directorio>/libro_0.txt ... libro_{k-1}.txt y <directorio>/urls_locales.txt
(una ruta local por renglon, que los programas aceptan igual que una URL).
Los libros llevan encabezado/pie de Gutenberg, mayusculas, puntuacion, digitos,
palabras de una letra, guiones bajos y caracteres no ASCII para ejercitar el
tokenizador.
"""
import os
import random
import sys

SILABAS = ["ka", "lo", "mi", "ra", "ten", "su", "vo", "an", "el", "or", "ith", "um", "pa", "sel", "dor"]
EXTRAS = ["a", "I", "x", "42", "2026", "don't", "café", "_italica_", "TheEnd", "ALL-CAPS", "mother-in-law"]


def palabra(rng):
    return "".join(rng.choice(SILABAS) for _ in range(rng.randint(1, 4)))


def main():
    directorio, k, n_palabras = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    os.makedirs(directorio, exist_ok=True)
    rng = random.Random(2026)
    vocabulario = [palabra(rng) for _ in range(4000)] + EXTRAS
    # Distribucion tipo Zipf: pocas palabras muy frecuentes, muchas raras.
    pesos = [1.0 / (i + 1) for i in range(len(vocabulario))]
    rutas = []
    for i in range(k):
        cuerpo = []
        for j, w in enumerate(rng.choices(vocabulario, weights=pesos, k=n_palabras)):
            if rng.random() < 0.08:
                w = w.capitalize()
            cuerpo.append(w + rng.choice([" ", " ", " ", ", ", ". ", "; ", "\n", " -- "]))
        texto = (
            f"The Project Gutenberg eBook of Libro Falso {i}\n\n"
            "This eBook is for the use of anyone anywhere (aviso legal de prueba).\n\n"
            f"*** START OF THE PROJECT GUTENBERG EBOOK LIBRO FALSO {i} ***\n\n"
            + "".join(cuerpo)
            + f"\n\n*** END OF THE PROJECT GUTENBERG EBOOK LIBRO FALSO {i} ***\n\n"
            "Pie legal con palabras que NO deben contarse: licencia donacion fundacion.\n"
        )
        ruta = os.path.join(directorio, f"libro_{i}.txt")
        with open(ruta, "w", encoding="utf-8") as f:
            f.write(texto)
        rutas.append(os.path.abspath(ruta))
    with open(os.path.join(directorio, "urls_locales.txt"), "w") as f:
        f.write("\n".join(rutas) + "\n")
    print(f"{k} libros de ~{n_palabras} palabras en {directorio}")


if __name__ == "__main__":
    main()
