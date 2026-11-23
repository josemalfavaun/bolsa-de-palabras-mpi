// paralelo_mpi.cpp - Bolsa de palabras, version paralela con MPI.
//
// Estrategia:
//   1. El proceso 0 lee la lista de URLs y la reparte con MPI_Bcast.
//   2. Reparto ciclico: el libro i lo atiende el proceso (i % q). CADA proceso
//      descarga (con curl) y cuenta las palabras de SUS libros, asi la descarga
//      tambien se paraleliza.
//   3. Cada proceso serializa sus conteos (palabra '\0' cuenta '\0' ...) en un
//      buffer de caracteres; MPI_Gather + MPI_Gatherv los llevan al proceso 0.
//   4. El proceso 0 reconstruye los conteos, une el vocabulario, arma la matriz y
//      escribe el CSV con las MISMAS funciones que usa la version serial.
//   5. Se imprime el speed-up = T_serial / T_paralelo.
//
// Uso: mpirun -np <q> ./bin/bolsa_mpi <archivo_urls>
//          [--salida salida_paralela.csv]
//          [--salida-serial salida_serial.csv]
//          [--tiempo-serial SEG]   (si se omite, el proceso 0 corre primero la
//                                   version serial para medir T_serial)

#include <mpi.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

#include "comun.hpp"

using namespace bolsa;

namespace {

// ---------- serializacion de conteos (MPI no puede enviar un std::map) ----------
// Formato por libro:  <indice>\0 <num_pares>\0 { <palabra>\0 <cuenta>\0 }*

void agregar_campo(std::string& buffer, const std::string& campo) {
    buffer += campo;
    buffer.push_back('\0');
}

std::string serializar(const std::vector<int>& indices, const std::vector<Conteo>& conteos) {
    std::string buffer;
    for (size_t j = 0; j < indices.size(); ++j) {
        agregar_campo(buffer, std::to_string(indices[j]));
        agregar_campo(buffer, std::to_string(conteos[j].size()));
        for (const auto& par : conteos[j]) {
            agregar_campo(buffer, par.first);
            agregar_campo(buffer, std::to_string(par.second));
        }
    }
    return buffer;
}

std::string_view siguiente_campo(const char*& p, const char* fin) {
    const char* nulo = static_cast<const char*>(std::memchr(p, '\0', static_cast<size_t>(fin - p)));
    if (!nulo) throw std::runtime_error("Buffer MPI mal formado");
    std::string_view campo(p, static_cast<size_t>(nulo - p));
    p = nulo + 1;
    return campo;
}

long a_numero(std::string_view s) { return std::stol(std::string(s)); }

void deserializar(const char* p, const char* fin, std::vector<Conteo>& por_libro) {
    while (p < fin) {
        const long libro = a_numero(siguiente_campo(p, fin));
        const long pares = a_numero(siguiente_campo(p, fin));
        if (libro < 0 || libro >= static_cast<long>(por_libro.size()))
            throw std::runtime_error("Indice de libro fuera de rango en el buffer MPI");
        Conteo& conteo = por_libro[static_cast<size_t>(libro)];
        for (long i = 0; i < pares; ++i) {
            const std::string_view palabra = siguiente_campo(p, fin);
            const int cuenta = static_cast<int>(a_numero(siguiente_campo(p, fin)));
            // Las palabras de un libro llegan ya ordenadas: insercion al final, O(1).
            conteo.emplace_hint(conteo.end(), std::string(palabra), cuenta);
        }
    }
}

std::vector<std::string> separar_renglones(const std::string& texto) {
    std::vector<std::string> renglones;
    size_t inicio = 0;
    while (inicio < texto.size()) {
        size_t salto = texto.find('\n', inicio);
        if (salto == std::string::npos) salto = texto.size();
        renglones.push_back(texto.substr(inicio, salto - inicio));
        inicio = salto + 1;
    }
    return renglones;
}

// Ejecuta todo el programa. Devuelve el codigo de salida; TODOS los procesos
// devuelven el mismo valor para que nadie se quede esperando en una colectiva.
int ejecutar(int rank, int q, int argc, char** argv) {
    const MPI_Comm comunicador = MPI_COMM_WORLD;
    const int MAESTRO = 0;

    // --- argumentos (todos los procesos reciben los mismos) ---
    Argumentos args;
    try {
        args = leer_argumentos(argc, argv, "salida_paralela.csv");
    } catch (const std::exception& e) {
        if (rank == MAESTRO) {
            std::cerr << "ERROR: " << e.what() << "\n"
                      << "Uso: mpirun -np <q> bolsa_mpi <archivo_urls> [--salida CSV] "
                         "[--salida-serial CSV] [--tiempo-serial SEG]\n";
        }
        return 1;
    }

    // --- el maestro lee las URLs y las reparte ---
    std::string lista;
    int ok = 1;
    if (rank == MAESTRO) {
        try {
            for (const auto& url : leer_urls(args.archivo_urls)) lista += url + "\n";
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            ok = 0;
        }
    }
    MPI_Bcast(&ok, 1, MPI_INT, MAESTRO, comunicador);
    if (!ok) return 1;
    int longitud = static_cast<int>(lista.size());
    MPI_Bcast(&longitud, 1, MPI_INT, MAESTRO, comunicador);
    lista.resize(static_cast<size_t>(longitud));
    MPI_Bcast(&lista[0], longitud, MPI_CHAR, MAESTRO, comunicador);
    const std::vector<std::string> urls = separar_renglones(lista);
    const int k = static_cast<int>(urls.size());

    // --- tiempo serial de referencia ---
    double t_serial = args.tiempo_serial;
    const bool serial_medido_aqui = (t_serial <= 0);
    Bolsa referencia;
    if (serial_medido_aqui) {
        ok = 1;
        if (rank == MAESTRO) {
            try {
                TiemposSerial ts;
                referencia = bolsa_serial(urls, args.salida_serial, ts);
                t_serial = ts.total;
            } catch (const std::exception& e) {
                std::cerr << "ERROR en la version serial: " << e.what() << "\n";
                ok = 0;
            }
        }
        MPI_Bcast(&ok, 1, MPI_INT, MAESTRO, comunicador);
        if (!ok) return 1;
        MPI_Bcast(&t_serial, 1, MPI_DOUBLE, MAESTRO, comunicador);
    }

    // =====================  PARTE PARALELA (se cronometra)  =====================
    MPI_Barrier(comunicador);
    const double t0 = MPI_Wtime();

    // Fase 1: cada proceso descarga y cuenta sus libros (reparto ciclico).
    std::vector<int> mis_indices;
    std::vector<Conteo> mis_conteos;
    double t_descarga = 0, t_conteo = 0;
    int fallos = 0;
    for (int i = rank; i < k; i += q) {
        try {
            const double a = MPI_Wtime();
            const std::string texto = descargar(urls[static_cast<size_t>(i)]);
            const double b = MPI_Wtime();
            Conteo conteo = contar_palabras(texto);
            const double c = MPI_Wtime();
            t_descarga += b - a;
            t_conteo += c - b;
            mis_indices.push_back(i);
            mis_conteos.push_back(std::move(conteo));
        } catch (const std::exception& e) {
            std::cerr << "[proceso " << rank << "] ERROR con el libro " << i << " ("
                      << urls[static_cast<size_t>(i)] << "): " << e.what() << std::endl;
            ++fallos;
        }
    }
    int fallos_totales = 0;
    MPI_Allreduce(&fallos, &fallos_totales, 1, MPI_INT, MPI_SUM, comunicador);
    if (fallos_totales > 0) {
        if (rank == MAESTRO) std::cerr << "ERROR: fallaron " << fallos_totales << " libro(s); abortando.\n";
        return 1;
    }
    const double t_fase1 = MPI_Wtime() - t0;  // ya sincronizados: es el maximo entre procesos

    // Fase 2: serializar y juntar en el maestro.
    const std::string mi_buffer = serializar(mis_indices, mis_conteos);
    if (mi_buffer.size() > static_cast<size_t>(INT_MAX)) {
        std::cerr << "[proceso " << rank << "] ERROR: buffer demasiado grande para MPI\n";
        MPI_Abort(comunicador, 2);
    }
    const int mi_tam = static_cast<int>(mi_buffer.size());
    std::vector<int> tamanos(static_cast<size_t>(q), 0), desplazamientos(static_cast<size_t>(q), 0);
    MPI_Gather(&mi_tam, 1, MPI_INT, tamanos.data(), 1, MPI_INT, MAESTRO, comunicador);

    std::vector<char> recibido;
    if (rank == MAESTRO) {
        long long total = 0;
        for (int r = 0; r < q; ++r) {
            desplazamientos[static_cast<size_t>(r)] = static_cast<int>(total);
            total += tamanos[static_cast<size_t>(r)];
        }
        if (total > INT_MAX) {
            std::cerr << "ERROR: los datos juntos exceden el limite de MPI_Gatherv\n";
            MPI_Abort(comunicador, 2);
        }
        recibido.resize(static_cast<size_t>(total));
    }
    MPI_Gatherv(mi_buffer.data(), mi_tam, MPI_CHAR, recibido.data(), tamanos.data(),
                desplazamientos.data(), MPI_CHAR, MAESTRO, comunicador);

    // Fase 3 (solo maestro): unir, armar la matriz y escribir el CSV.
    double t_union = 0, t_matriz = 0, t_escritura = 0, t_total = 0;
    bool coincide = false;
    size_t columnas = 0;
    if (rank == MAESTRO) {
        try {
            const double a = MPI_Wtime();
            std::vector<Conteo> por_libro(static_cast<size_t>(k));
            for (int r = 0; r < q; ++r) {
                const char* inicio = recibido.data() + desplazamientos[static_cast<size_t>(r)];
                deserializar(inicio, inicio + tamanos[static_cast<size_t>(r)], por_libro);
            }
            const double b = MPI_Wtime();
            const Bolsa resultado = construir_bolsa(por_libro);
            const double c = MPI_Wtime();
            escribir_csv(args.salida, resultado);
            const double d = MPI_Wtime();
            t_union = b - a;
            t_matriz = c - b;
            t_escritura = d - c;
            t_total = d - t0;
            columnas = resultado.vocabulario.size();
            if (serial_medido_aqui) coincide = (resultado == referencia);
        } catch (const std::exception& e) {
            std::cerr << "ERROR en el proceso maestro: " << e.what() << std::endl;
            MPI_Abort(comunicador, 1);
        }
    }

    // Tiempos maximos entre procesos (el mas lento define la fase).
    double max_descarga = 0, max_conteo = 0;
    MPI_Reduce(&t_descarga, &max_descarga, 1, MPI_DOUBLE, MPI_MAX, MAESTRO, comunicador);
    MPI_Reduce(&t_conteo, &max_conteo, 1, MPI_DOUBLE, MPI_MAX, MAESTRO, comunicador);

    if (rank == MAESTRO) {
        const double speedup = t_serial / t_total;
        std::printf("=== Bolsa de palabras (version MPI) ===\n");
        std::printf("Libros (k):                     %d\n", k);
        std::printf("Procesos (q):                   %d\n", q);
        std::printf("Vocabulario (columnas):         %zu\n", columnas);
        std::printf("Tiempo serial:                  %.6f s  (%s)\n", t_serial,
                    serial_medido_aqui ? "medido en esta corrida" : "dato recibido con --tiempo-serial");
        std::printf("Tiempo paralelo:                %.6f s\n", t_total);
        std::printf("   fase 1 (descarga + conteo):  %.6f s   [descarga max: %.6f, conteo max: %.6f]\n",
                    t_fase1, max_descarga, max_conteo);
        std::printf("   fase 2 (juntar en maestro):  %.6f s\n", t_union);
        std::printf("   matriz:                      %.6f s\n", t_matriz);
        std::printf("   escritura del CSV:           %.6f s\n", t_escritura);
        std::printf("Speed-up:                       %.3fx\n", speedup);
        std::printf("Eficiencia (speed-up / q):      %.3f\n", speedup / q);
        std::printf("Speed-up > 1.2x (requisito):    %s\n", speedup > 1.2 ? "SI" : "NO");
        if (serial_medido_aqui)
            std::printf("Resultado identico al serial:   %s\n", coincide ? "SI" : "NO (!!)");
        else
            std::printf("Resultado identico al serial:   no verificado aqui (usa scripts/validar.sh)\n");
        std::printf("Salida: %s\n", args.salida.c_str());
        std::printf("#TIEMPO %.6f\n", t_total);  // linea para los scripts
        std::fflush(stdout);
        if (serial_medido_aqui && !coincide) return 3;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, q = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &q);
    const int codigo = ejecutar(rank, q, argc, argv);
    MPI_Finalize();
    return codigo;
}
