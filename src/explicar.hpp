// explicar.hpp - Mensajes "para humanos" que se activan con la opcion --explicar.
//
// Sirven para presentar el proyecto: en vez de una lista de numeros, el programa
// cuenta que hizo cada proceso, dibuja barras de tiempo y compara serial contra
// paralelo. La salida normal (sin --explicar) NO cambia, porque los scripts de
// validacion y medicion dependen de ella.
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace bolsa {

// "https://.../pg46.txt" -> "A Christmas Carol". Si no conoce el archivo, devuelve su nombre.
inline std::string nombre_libro(const std::string& url) {
    const size_t pos = url.find_last_of('/');
    const std::string archivo = (pos == std::string::npos) ? url : url.substr(pos + 1);
    static const struct { const char* archivo; const char* titulo; } conocidos[] = {
        {"pg46.txt", "A Christmas Carol"},    {"pg98.txt", "A Tale of Two Cities"},
        {"pg1524.txt", "Hamlet"},             {"pg1513.txt", "Romeo and Juliet"},
        {"pg730.txt", "Oliver Twist"},        {"pg1515.txt", "The Merchant of Venice"},
    };
    for (const auto& c : conocidos)
        if (archivo == c.archivo) return c.titulo;
    return archivo;
}

// Barra de texto: fraccion 0..1 -> [#####.....]
inline std::string barra(double fraccion, int ancho = 30) {
    if (fraccion < 0) fraccion = 0;
    if (fraccion > 1) fraccion = 1;
    const int llenos = static_cast<int>(fraccion * ancho + 0.5);
    return "[" + std::string(static_cast<size_t>(llenos), '#') +
           std::string(static_cast<size_t>(ancho - llenos), '.') + "]";
}

inline void linea_doble() {
    std::printf("==============================================================================\n");
}

// ---------------------------- version serial ----------------------------

inline void explicar_serial(const std::vector<std::string>& urls, size_t columnas, double total,
                            double descarga, double conteo, double matriz, double escritura,
                            const std::string& salida) {
    linea_doble();
    std::printf(" VERSION SERIAL: un solo proceso hace todo, libro por libro\n");
    linea_doble();
    std::printf("Que hace el programa: lee %zu libros y arma una tabla (la \"bolsa de palabras\"):\n",
                urls.size());
    std::printf("una fila por libro, una columna por palabra distinta, y en cada celda cuantas\n");
    std::printf("veces aparece esa palabra en ese libro.\n\n");
    std::printf("Libros, en el orden en que se procesan (uno despues de otro):\n");
    for (size_t i = 0; i < urls.size(); ++i)
        std::printf("  libro %zu: %s\n", i, nombre_libro(urls[i]).c_str());
    std::printf("\nResultado: una tabla de %zu filas x %zu columnas (%zu palabras distintas).\n\n",
                urls.size(), columnas, columnas);
    std::printf("En que se fue el tiempo (total %.4f s):\n", total);
    const struct { const char* nombre; double t; } partes[] = {
        {"descargar los libros", descarga}, {"contar palabras", conteo},
        {"armar la matriz", matriz},        {"escribir el CSV", escritura}};
    for (const auto& p : partes)
        std::printf("  %-22s %s %5.1f%%  %.4f s\n", p.nombre, barra(p.t / total).c_str(),
                    100.0 * p.t / total, p.t);
    std::printf("\nAnalogia: un solo cocinero prepara los %zu platos, uno tras otro.\n", urls.size());
    std::printf("Este tiempo es nuestra REFERENCIA: la version paralela tiene que ganarle.\n");
    std::printf("CSV guardado en: %s\n", salida.c_str());
}

// ---------------------------- version MPI ----------------------------

struct DatosMPI {
    int q = 1;
    std::vector<std::string> urls;
    std::vector<double> t_ocupado;  // por proceso: tiempo de descarga + conteo
    std::vector<double> bytes;      // por proceso: bytes de texto que descargo
    double t_serial = 0, t_total = 0, t_fase1 = 0, t_union = 0, t_matriz = 0, t_escritura = 0;
    size_t columnas = 0, bytes_enviados = 0;
    bool serial_medido_aqui = false, coincide = false;
    std::string salida;
};

inline void explicar_mpi(const DatosMPI& d) {
    const int k = static_cast<int>(d.urls.size());
    const double speedup = d.t_serial / d.t_total;
    linea_doble();
    std::printf(" VERSION PARALELA (MPI): %d procesos se reparten el trabajo\n", d.q);
    linea_doble();
    std::printf("Idea: en vez de un solo cocinero hay %d. Cada proceso es una copia del mismo\n", d.q);
    std::printf("programa; MPI les da un numero (0, 1, 2...) y les permite mandarse mensajes.\n\n");

    std::printf("PASO 1. Reparto ciclico: el libro i le toca al proceso (i mod %d).\n", d.q);
    for (int r = 0; r < d.q; ++r) {
        std::printf("  proceso %d ->", r);
        bool alguno = false;
        for (int i = r; i < k; i += d.q) {
            std::printf("%s libro %d (%s)", alguno ? "," : "", i,
                        nombre_libro(d.urls[static_cast<size_t>(i)]).c_str());
            alguno = true;
        }
        if (!alguno) std::printf(" (sin libros: hay mas procesos que libros)");
        std::printf("\n");
    }
    std::printf("Cada proceso descarga y cuenta SUS libros al mismo tiempo que los demas.\n\n");

    const double mas_lento = *std::max_element(d.t_ocupado.begin(), d.t_ocupado.end());
    const int quien = static_cast<int>(
        std::max_element(d.t_ocupado.begin(), d.t_ocupado.end()) - d.t_ocupado.begin());
    std::printf("Cuanto trabajo hizo cada proceso (barra = su tiempo ocupado / el del mas lento):\n");
    for (int r = 0; r < d.q; ++r) {
        int libros = 0;
        for (int i = r; i < k; i += d.q) ++libros;
        const double t = d.t_ocupado[static_cast<size_t>(r)];
        std::printf("  proceso %d %s %.4f s  %6.2f MB  %d libro(s)\n", r,
                    barra(mas_lento > 0 ? t / mas_lento : 0).c_str(), t,
                    d.bytes[static_cast<size_t>(r)] / 1e6, libros);
    }
    std::printf("El proceso mas lento fue el %d. Los demas terminan antes y ESPERAN: la fase\n", quien);
    std::printf("dura lo que dura el mas lento (%.4f s). Por eso importa repartir parejo.\n\n", d.t_fase1);

    std::printf("PASO 2. Juntar resultados (%.4f s).\n", d.t_union);
    std::printf("Cada proceso empaqueta sus conteos en un texto (palabra, cuenta, palabra...)\n");
    std::printf("y lo manda al proceso 0 con MPI_Gather (tamanos) y MPI_Gatherv (datos).\n");
    std::printf("Viajaron %.2f MB en total.\n\n", static_cast<double>(d.bytes_enviados) / 1e6);

    std::printf("PASO 3. Solo el proceso 0: armar la matriz (%.4f s) y escribir el CSV (%.4f s).\n",
                d.t_matriz, d.t_escritura);
    std::printf("Esta parte NO se reparte: es la fraccion serial que limita el speed-up\n");
    std::printf("(la ley de Amdahl).\n\n");

    linea_doble();
    std::printf(" COMPARACION\n");
    linea_doble();
    const double mayor = std::max(d.t_serial, d.t_total);
    std::printf("  serial     %s %.4f s   (1 proceso)\n", barra(d.t_serial / mayor).c_str(), d.t_serial);
    std::printf("  paralelo   %s %.4f s   (%d procesos)\n\n", barra(d.t_total / mayor).c_str(), d.t_total, d.q);
    std::printf("  Speed-up   = T_serial / T_paralelo = %.4f / %.4f = %.2fx\n", d.t_serial, d.t_total, speedup);
    std::printf("               (el paralelo es %.2f veces mas rapido)\n", speedup);
    std::printf("  Eficiencia = speed-up / procesos = %.2f / %d = %.2f\n", speedup, d.q, speedup / d.q);
    std::printf("               (1.00 seria perfecto; baja por el paso 3 y por esperar al mas lento)\n");
    std::printf("  Requisito  : speed-up > 1.2x ... %s\n", speedup > 1.2 ? "SI se cumple" : "NO se cumple");
    if (d.serial_medido_aqui)
        std::printf("  Resultado  : identico al serial ... %s\n", d.coincide ? "SI" : "NO (!!)");
    else
        std::printf("  Resultado  : no verificado aqui (compara los CSV con cmp)\n");
    std::printf("CSV guardado en: %s\n", d.salida.c_str());
}

}  // namespace bolsa
