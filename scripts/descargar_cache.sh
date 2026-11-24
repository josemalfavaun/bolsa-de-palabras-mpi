#!/usr/bin/env bash
# Descarga cada libro UNA sola vez a una carpeta local y escribe la lista de rutas locales.
#
# Uso:    scripts/descargar_cache.sh <archivo_urls> [directorio=data/cache]
# Ej.:    scripts/descargar_cache.sh data/urls_6_libros.txt
#
# Luego puedes validar y medir el computo SIN volver a tocar Project Gutenberg:
#   scripts/validar.sh data/cache/urls_locales.txt 1 2 4 7
#   scripts/medir.sh   data/cache/urls_locales.txt 5 1 2 3 6
#
# Project Gutenberg es un sitio para personas: bloquea IPs que parecen automaticas.
# Por eso se hace una pausa entre libros (PAUSA, 3 s por defecto) y se omiten los
# libros que ya estan en la carpeta. Si un libro falla, abre la URL en tu navegador,
# guarda el texto con el nombre que indica el mensaje y vuelve a correr este script.
set -u

URLS=${1:?Uso: scripts/descargar_cache.sh <archivo_urls> [directorio]}
DIR=${2:-data/cache}
PAUSA=${PAUSA:-3}
UA=${BOLSA_USER_AGENT:-"bolsa-de-palabras-mpi/1.0 (+https://github.com/josemalfavaun/bolsa-de-palabras-mpi; proyecto escolar ITAM)"}

mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)
: > "$DIR/urls_locales.txt"
fallos=0

while read -r url; do
    destino="$DIR/$(basename "$url")"
    if [ -s "$destino" ]; then
        echo "Ya existe: $destino"
    else
        echo "Descargando $url"
        if curl --fail --silent --show-error --location --retry 2 --retry-delay 3 \
                -A "$UA" -o "$destino" "$url" && [ "$(wc -c < "$destino")" -gt 1000 ]; then
            sleep "$PAUSA"
        else
            rm -f "$destino"
            echo "  FALLO con $url"
            echo "  -> Abrelo en tu navegador y guarda el texto como: $destino"
            fallos=$((fallos + 1))
            continue
        fi
    fi
    echo "$destino" >> "$DIR/urls_locales.txt"
done < <(grep -vE '^[[:space:]]*(#|$)' "$URLS")

n=$(wc -l < "$DIR/urls_locales.txt" | tr -d ' ')
echo "Libros listos: $n  ->  $DIR/urls_locales.txt"
[ "$fallos" -eq 0 ] || { echo "$fallos libro(s) faltan; el orden de la lista cambia si usas una lista incompleta."; exit 1; }
