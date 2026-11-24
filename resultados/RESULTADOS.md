# Resultados

Mediciones hechas el 24 de noviembre de 2026 con `scripts/medir.sh` (mediana de 3 repeticiones por configuración).

**Equipo:** MacBook Air (Apple Silicon, arm64) — chip: Apple M4, RAM: 16 GB, macOS: 26.5.1.
**Software:** Open MPI 5.0.11, `mpicxx -O2 -std=c++17`, `curl`.
**Libros:** los 6 del ejemplo del curso (`data/urls_6_libros.txt`), 2,468,390 bytes en total.
**Origen de la descarga:** ese día el sitio principal (https://www.gutenberg.org) estaba en mantenimiento, así que se usó el espejo oficial https://gutenberg.pglaf.org (`data/urls_6_libros_espejo.txt`), que sirve los mismos archivos `cache/epub/N/pgN.txt`.

## Validación

`scripts/validar.sh data/cache/urls_locales.txt 1 2 4 7` → los CSV serial y paralelo son **idénticos byte por byte** para k = 1, 3 y 6 libros y q = 1, 2, 4 y 7 procesos (incluye q > k).

## 1. Con descarga real (la medición que importa)

T_serial y T_mpi incluyen descarga, conteo, unión de vocabularios, matriz y escritura del CSV. Se hicieron dos corridas completas de `scripts/medir.sh data/urls_6_libros_espejo.txt 3 1 2 3 6`.

**Corrida A** — T_serial = 24.40 s

| q | T_mpi (s) | speed-up | eficiencia |
|---|-----------|----------|------------|
| 1 | 15.40 | 1.584x | 1.584 |
| 2 | 9.98 | 2.446x | 1.223 |
| 3 | 13.85 | 1.762x | 0.587 |
| 6 | 7.31 | 3.336x | 0.556 |

**Corrida B** — T_serial = 18.25 s

| q | T_mpi (s) | speed-up | eficiencia |
|---|-----------|----------|------------|
| 1 | 17.93 | 1.018x | 1.018 |
| 2 | 10.19 | 1.792x | 0.896 |
| 3 | 12.17 | 1.500x | 0.500 |
| 6 | 11.79 | 1.549x | 0.258 |

**El speed-up supera 1.2x para q = 2, 3 y 6 en las dos corridas.**

### Cómo leer estos números

- **El ruido de la red es grande.** Con q = 1 el programa MPI hace prácticamente lo mismo que el serial, así que su speed-up debería ser ≈ 1. En la corrida B lo es (1.018x); en la A salió 1.584x porque ese T_serial resultó mucho más lento (24.4 s contra 18.3 s) por variación de la red, no por paralelismo. Por eso los speed-ups de la corrida A están inflados y la corrida B es la referencia más limpia.
- **Con q = 3 el speed-up baja respecto a q = 2 (en ambas corridas).** No es un error: es desbalanceo de carga del reparto cíclico (libro `i` → proceso `i % q`), porque los libros pesan muy distinto:

| libro | archivo | bytes |
|---|---|---|
| A Christmas Carol | pg46 | 182,250 |
| A Tale of Two Cities | pg98 | 807,197 |
| Hamlet | pg1524 | 206,895 |
| Romeo and Juliet | pg1513 | 169,504 |
| Oliver Twist | pg730 | 955,858 |
| The Merchant of Venice | pg1515 | 146,686 |

  | q | bytes por proceso | proceso más cargado | cota de speed-up si el tiempo fuera proporcional a los bytes |
  |---|---|---|---|
  | 2 | 1,345,003 / 1,123,387 | 54.5% del total | ≈ 1.84x |
  | 3 | 351,754 / **1,763,055** / 353,581 | **71.4% del total** | ≈ 1.40x |
  | 6 | un libro cada uno | 38.7% (Oliver Twist) | ≈ 2.58x |

  Con q = 3, el proceso 1 recibe los dos libros más grandes (Tale of Two Cities y Oliver Twist) y los otros dos procesos terminan pronto y esperan. Con q = 6 el tiempo lo fija el libro más grande (Oliver Twist), así que más procesos no ayudan más allá de ese punto. La cota es solo un modelo aproximado (ignora latencias fijas), pero explica el orden observado q = 2 > q = 3.

## 2. Solo cómputo (libros ya descargados, sin red)

`scripts/medir.sh data/cache/urls_locales.txt 3 1 2 3 6` — T_serial = 0.1045 s

| q | T_mpi (s) | speed-up | eficiencia |
|---|-----------|----------|------------|
| 1 | 0.1119 | 0.934x | 0.934 |
| 2 | 0.0736 | 1.421x | 0.710 |
| 3 | 0.0887 | 1.178x | 0.393 |
| 6 | 0.0606 | 1.726x | 0.288 |

Con solo ~0.1 s de trabajo, los costos fijos de MPI (barreras, serialización, `Gatherv`) pesan mucho y la variación entre corridas es grande. Esta tabla muestra que el cómputo por sí solo es poco; la ganancia real del proyecto viene de **paralelizar la descarga**, que es lo que domina el tiempo (~18-24 s contra ~0.1 s de cómputo).

## Conclusiones y limitaciones

- La versión MPI produce exactamente el mismo resultado que la serial y cumple el requisito de speed-up > 1.2x con descarga real.
- La ganancia depende de la red y varía entre corridas; por eso se reportan medianas y dos corridas completas.
- El reparto cíclico es estático y no conoce el tamaño de los libros, lo que provoca el desbalanceo visto con q = 3. Una mejora posible es un reparto dinámico (cada proceso toma el siguiente libro pendiente al terminar el suyo), que balancearía mejor la carga cuando los libros son muy desiguales.
