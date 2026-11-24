#!/usr/bin/env bash
# Valida que la version MPI produzca EXACTAMENTE el mismo CSV que la serial.
#
# Uso:    scripts/validar.sh <archivo_urls> [q1 q2 ...]
# Ej.:    scripts/validar.sh data/urls_6_libros.txt 1 2 4 7
#
# Para varios k (1, la mitad, todos los libros del archivo) y cada q compara con
# `cmp` el CSV serial contra el CSV paralelo. Con URLs reales descarga los libros de nuevo
# en cada corrida (muchas peticiones a Gutenberg): mejor usa la lista de rutas locales que
# genera scripts/descargar_cache.sh (data/cache/urls_locales.txt).
#
# Variables opcionales:
#   MPIRUN="mpirun --oversubscribe"   para usar mas procesos que nucleos
#   KS="1 3 6"                        para elegir los valores de k a probar
set -u

URLS=${1:?Uso: scripts/validar.sh <archivo_urls> [q1 q2 ...]}
shift
QS=${*:-1 2 4 7}
MPIRUN=${MPIRUN:-mpirun}
RAIZ=$(cd "$(dirname "$0")/.." && pwd)

[ -x "$RAIZ/bin/serial" ] && [ -x "$RAIZ/bin/bolsa_mpi" ] || { echo "Primero compila con: make"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
grep -vE '^[[:space:]]*(#|$)' "$URLS" > "$TMP/todas.txt"
N=$(wc -l < "$TMP/todas.txt" | tr -d ' ')
KS=${KS:-$(printf '1\n%s\n%s\n' $((N / 2)) "$N" | awk '$1 >= 1' | sort -nu | tr '\n' ' ')}

fallos=0
for k in $KS; do
    head -n "$k" "$TMP/todas.txt" > "$TMP/urls_k.txt"
    if ! "$RAIZ/bin/serial" "$TMP/urls_k.txt" --salida "$TMP/serial.csv" > /dev/null; then
        echo "k=$k: fallo la version serial"; fallos=$((fallos + 1)); continue
    fi
    for q in $QS; do
        if $MPIRUN -np "$q" "$RAIZ/bin/bolsa_mpi" "$TMP/urls_k.txt" --salida "$TMP/paralelo.csv" \
                --tiempo-serial 1 > /dev/null; then
            if cmp -s "$TMP/serial.csv" "$TMP/paralelo.csv"; then
                echo "k=$k q=$q: OK (CSV identico)"
            else
                echo "k=$k q=$q: DIFERENTE"; fallos=$((fallos + 1))
            fi
        else
            echo "k=$k q=$q: fallo la ejecucion MPI"; fallos=$((fallos + 1))
        fi
    done
done

if [ "$fallos" -eq 0 ]; then echo "Todo coincide."; else echo "$fallos caso(s) con problemas."; exit 1; fi
