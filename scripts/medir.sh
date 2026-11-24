#!/usr/bin/env bash
# Mide tiempos y speed-up: serial vs MPI para varios numeros de procesos.
#
# Uso:    scripts/medir.sh <archivo_urls> [repeticiones=3] [q1 q2 ...]
# Ej.:    scripts/medir.sh data/urls_6_libros.txt 5 1 2 3 6
#
# T_serial = mediana de las repeticiones de bin/serial.
# T_mpi(q) = mediana de las repeticiones de bin/bolsa_mpi con q procesos.
# speed-up = T_serial / T_mpi(q).   Los datos crudos quedan en resultados/mediciones_<fecha_hora>.csv
#
# OJO: con URLs de Gutenberg cada corrida vuelve a descargar los libros (k x repeticiones x
# (1 + numero de valores de q) peticiones). Gutenberg bloquea IPs que parecen automaticas, asi que
# para medir el computo usa la lista de rutas locales de scripts/descargar_cache.sh, y haz solo
# unas pocas corridas con URLs reales (p. ej. 3 repeticiones) para la medicion final.
#
# Variable opcional:  MPIRUN="mpirun --oversubscribe"
set -u

URLS=${1:?Uso: scripts/medir.sh <archivo_urls> [repeticiones] [q1 q2 ...]}
REPS=${2:-3}
shift
[ $# -gt 0 ] && shift
QS=${*:-1 2 3 4 6}
MPIRUN=${MPIRUN:-mpirun}
RAIZ=$(cd "$(dirname "$0")/.." && pwd)

[ -x "$RAIZ/bin/serial" ] && [ -x "$RAIZ/bin/bolsa_mpi" ] || { echo "Primero compila con: make"; exit 1; }

mediana() { sort -g | awk '{a[NR]=$1} END{ if (NR==0) exit 1; if (NR%2) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'; }
tiempo() { awk '/^#TIEMPO/ {print $2}'; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
K=$(grep -cvE '^[[:space:]]*(#|$)' "$URLS")
mkdir -p "$RAIZ/resultados"
CSV="$RAIZ/resultados/mediciones_$(date +%Y%m%d_%H%M%S).csv"
echo "version,q,repeticion,segundos" > "$CSV"

echo "Libros (k) = $K, repeticiones = $REPS"
echo "Midiendo version serial..."
for r in $(seq 1 "$REPS"); do
    t=$("$RAIZ/bin/serial" "$URLS" --salida "$TMP/s.csv" | tiempo)
    [ -n "$t" ] || { echo "Fallo la version serial"; exit 1; }
    echo "serial,1,$r,$t" >> "$CSV"
    echo "$t" >> "$TMP/serial.txt"
done
T_SERIAL=$(mediana < "$TMP/serial.txt")
printf "T_serial (mediana) = %.4f s\n\n" "$T_SERIAL"

printf "%4s %16s %10s %12s\n" "q" "T_mpi (mediana)" "speed-up" "eficiencia"
for q in $QS; do
    : > "$TMP/q$q.txt"
    for r in $(seq 1 "$REPS"); do
        t=$($MPIRUN -np "$q" "$RAIZ/bin/bolsa_mpi" "$URLS" --salida "$TMP/p.csv" \
                --tiempo-serial "$T_SERIAL" | tiempo)
        [ -n "$t" ] || { echo "Fallo la version MPI con q=$q"; exit 1; }
        echo "mpi,$q,$r,$t" >> "$CSV"
        echo "$t" >> "$TMP/q$q.txt"
    done
    T=$(mediana < "$TMP/q$q.txt")
    awk -v q="$q" -v t="$T" -v s="$T_SERIAL" 'BEGIN { printf "%4d %14.4f s %9.3fx %12.3f\n", q, t, s/t, s/t/q }'
done
echo
echo "Datos crudos en: ${CSV#$RAIZ/}"
