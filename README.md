# Bolsa de palabras con MPI

Bolsa de palabras para k libros de Project Gutenberg en C++: versión serial y versión paralela con MPI, con cálculo de speed-up.

Proyecto de Clausura de *Cómputo Paralelo y en la Nube* (ITAM, 2026b, Dr. Octavio Gutiérrez).

El programa recibe el número de procesos y una lista de URLs de Project Gutenberg, descarga los libros dinámicamente, construye la matriz de bolsa de palabras (un renglón por libro, una columna por palabra del vocabulario global) y la guarda en un CSV. Funciona para cualquier k libros y cualquier q procesos (incluso q > k), e imprime el speed-up de la versión paralela contra la serial.

## Estructura

```
src/comun.hpp          Código compartido: descarga, tokenizador, conteo, matriz, CSV y versión serial
src/serial.cpp         Programa serial
src/paralelo_mpi.cpp   Programa paralelo con MPI
scripts/validar.sh     Compara el CSV serial contra el MPI para varios k y q
scripts/medir.sh       Mide tiempos y speed-up (medianas) y guarda los datos crudos
tests/                 Pruebas sin internet (libros falsos, servidor lento, implementación de referencia en Python)
data/                  Listas de URLs de ejemplo
resultados/            Mediciones
```

## Requisitos

- Compilador con C++17 y MPI (Open MPI o MPICH). En macOS: `brew install open-mpi`.
- `curl` (viene en macOS y en la mayoría de Linux).
- Para los scripts de pruebas: Python 3.

## Compilar y ejecutar

```bash
make                                   # genera bin/serial y bin/bolsa_mpi

# Serial
./bin/serial data/urls_6_libros.txt --salida salida_serial.csv

# Paralelo con q = 4 procesos (corre antes la versión serial para medir T_serial)
mpirun -np 4 ./bin/bolsa_mpi data/urls_6_libros.txt --salida salida_paralela.csv
```

Opciones del programa MPI:

| Opción | Significado |
|---|---|
| `--salida CSV` | Archivo de salida de la versión paralela (por defecto `salida_paralela.csv`). |
| `--salida-serial CSV` | Archivo de salida de la corrida serial interna (por defecto `salida_serial.csv`). |
| `--tiempo-serial SEG` | Usa este T_serial en lugar de correr la versión serial dentro del programa. |

Si se pide más procesos que núcleos, Open MPI exige `mpirun --oversubscribe -np q ...`.

La salida incluye el desglose de tiempos, el speed-up, la eficiencia, si cumple el requisito de speed-up > 1.2x y si el resultado es idéntico al serial.

### Formato de la lista de URLs

Un URL por renglón. Los renglones vacíos y los que empiezan con `#` se ignoran. Una entrada que no empiece con `http://` o `https://` se trata como ruta de un archivo local (sirve para probar sin internet).

### Formato del CSV de salida

Primer renglón: `libro,<palabra1>,<palabra2>,...` (vocabulario global en orden alfabético). Después, un renglón por libro: `<índice del libro>,<conteo1>,<conteo2>,...`. El índice es la posición de la URL en el archivo de entrada, empezando en 0. Se lee directo con `pandas.read_csv(..., index_col="libro")`.

## Cómo funciona

### Tokenización única

Serial y MPI usan exactamente las mismas funciones de `src/comun.hpp`, así que sus CSV coinciden byte por byte. Las reglas:

1. Se conserva solo el cuerpo del libro, entre las líneas `*** START OF ...` y `*** END OF ...` de Gutenberg (se descartan el encabezado y el pie legal).
2. Se pasa a minúsculas (ASCII).
3. Una palabra es una racha de letras o dígitos ASCII (`[a-z0-9]`). La puntuación, los espacios, el guion bajo y cualquier carácter no ASCII separan palabras.
4. Se descartan las palabras de un solo carácter.

Son las reglas por defecto de `CountVectorizer` de scikit-learn (el que usa la libreta de ejemplo del curso), salvo por los caracteres acentuados y el guion bajo (aquí separa palabras; en `CountVectorizer` se queda dentro de ellas).

### Versión serial

Para cada URL: descarga, cuenta las palabras en un `std::map<std::string,int>`; al final une el vocabulario, arma la matriz y escribe el CSV.

### Versión MPI

1. El proceso 0 lee la lista de URLs y la reparte con `MPI_Bcast`.
2. **Reparto cíclico:** el libro `i` lo atiende el proceso `i % q`. **Cada proceso descarga y cuenta sus propios libros**, de modo que la descarga (que domina el tiempo) también se paraleliza.
3. Cada proceso serializa sus conteos en un buffer de caracteres (`palabra\0cuenta\0...`, porque MPI no puede enviar un `std::map`) y los junta en el proceso 0 con `MPI_Gather` + `MPI_Gatherv`.
4. El proceso 0 reconstruye los conteos, une el vocabulario, arma la matriz y escribe el CSV.
5. Los tiempos se miden con `MPI_Wtime()`; speed-up = T_serial / T_paralelo.

Si algún libro falla al descargarse, todos los procesos terminan con un mensaje de error (no se queda ninguno esperando).

### Qué se cronometra

T_serial y T_paralelo incluyen todo: descarga, conteo, unión, matriz y escritura del CSV. El desglose por fase se imprime aparte. El tiempo de descarga depende de la red, por eso `scripts/medir.sh` repite cada corrida y reporta la mediana.

## Validación y mediciones

```bash
# ¿El CSV paralelo es idéntico al serial para distintos k y q?
scripts/validar.sh data/urls_6_libros.txt 1 2 4 7

# Tiempos y speed-up (5 repeticiones, q = 1 2 3 6)
scripts/medir.sh data/urls_6_libros.txt 5 1 2 3 6
scripts/medir.sh data/urls_grandes.txt 5 1 2 4 8
```

`mediciones.csv` queda en `resultados/`. Usa `MPIRUN="mpirun --oversubscribe"` si necesitas más procesos que núcleos.

### Pruebas sin internet

```bash
python3 tests/generar_libros_falsos.py /tmp/libros 7 300000     # crea libros y urls_locales.txt
python3 tests/referencia.py /tmp/libros/urls_locales.txt /tmp/ref.csv   # resultado "de referencia" en Python
./bin/serial /tmp/libros/urls_locales.txt --salida /tmp/serial.csv
cmp /tmp/ref.csv /tmp/serial.csv                                 # el C++ cuenta igual que Python

# Probar la descarga real con curl contra un servidor local con retardo de 1.5 s por libro
python3 tests/servidor_lento.py /tmp/libros 1.5 8765 &
for i in 0 1 2 3 4 5 6; do echo "http://127.0.0.1:8765/libro_$i.txt"; done > /tmp/urls_http.txt
mpirun -np 4 ./bin/bolsa_mpi /tmp/urls_http.txt
```

## Resultados

Pendiente: correr `scripts/medir.sh` en el equipo de pruebas y pegar aquí la tabla (equipo, sistema operativo, versión de MPI, número de núcleos, T_serial, T_mpi y speed-up por q).

## Uso de IA generativa

Este proyecto se desarrolló con la asistencia de Claude (Anthropic), que ayudó a escribir el código, los scripts y este documento. Los autores son responsables de su contenido.
