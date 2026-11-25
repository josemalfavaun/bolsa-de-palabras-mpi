#!/usr/bin/env bash
# Demostracion narrada para presentar el proyecto.
#
# Uso:  scripts/demo.sh [archivo_urls] [procesos]
# Ej.:  scripts/demo.sh data/cache/urls_locales.txt 4
#       scripts/demo.sh data/urls_6_libros_espejo.txt 4     (descarga real: 12 peticiones)
#
# Hace: compilar, correr la version serial, correr la MPI con el T_serial recien medido
# (asi cada libro se baja solo 2 veces), comparar los CSV con cmp y mostrar un pedazo
# de la tabla. Variable opcional: SIN_PAUSAS=1 para no esperar Enter entre pasos.
set -u

RAIZ=$(cd "$(dirname "$0")/.." && pwd)
URLS=${1:-$RAIZ/data/cache/urls_locales.txt}
Q=${2:-4}
MPIRUN=${MPIRUN:-mpirun}
OUT=$(mktemp -d /tmp/demo_bolsa.XXXXXX)

pausa() { [ "${SIN_PAUSAS:-0}" = 1 ] || read -r -p "-- Enter para continuar -- " _; echo; }
titulo() { echo; echo "######## $1"; echo; }

[ -f "$URLS" ] || { echo "No existe la lista de libros: $URLS"; exit 1; }

titulo "0. Compilar (make)"
make -C "$RAIZ" || exit 1
pausa

titulo "1. Version SERIAL: un solo proceso"
"$RAIZ/bin/serial" "$URLS" --salida "$OUT/serial.csv" --explicar | tee "$OUT/serial.txt"
T=$(awk '/^#TIEMPO/ {print $2}' "$OUT/serial.txt")
pausa

titulo "2. Version PARALELA con MPI: $Q procesos"
$MPIRUN -np "$Q" "$RAIZ/bin/bolsa_mpi" "$URLS" --salida "$OUT/paralelo.csv" --tiempo-serial "$T" --explicar \
    || { echo "mpirun fallo (si pediste mas procesos que nucleos, usa MPIRUN=\"mpirun --oversubscribe\")"; exit 1; }
pausa

titulo "3. Es el MISMO resultado? (cmp compara byte por byte; si no imprime nada, son identicos)"
if cmp "$OUT/serial.csv" "$OUT/paralelo.csv"; then
    echo "Los dos CSV son identicos."
else
    echo "OJO: los CSV son distintos."
fi
pausa

titulo "4. Un vistazo a la tabla: cuantas veces aparece cada palabra en cada libro"
awk -F, -v palabras="love death king london ghost christmas" '
NR == 1 {
    n = split(palabras, p, " ")
    for (i = 2; i <= NF; i++) for (j = 1; j <= n; j++) if ($i == p[j]) col[j] = i
    ncol = NF - 1
    printf "%-8s", "libro"
    for (j = 1; j <= n; j++) printf "%-10s", p[j]
    printf "\n"
    next
}
{
    printf "%-8s", $1
    for (j = 1; j <= n; j++) printf "%-10s", ((j in col) ? $(col[j]) : "-")
    printf "\n"
    nf++
}
END { printf "\n%d filas (libros) x %d columnas (palabras distintas)\n", nf, ncol }' "$OUT/paralelo.csv"
echo
echo "(Los CSV completos quedaron en $OUT)"
