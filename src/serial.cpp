// serial.cpp - Bolsa de palabras, version serial.
//
// Uso: ./bin/serial <archivo_urls> [--salida salida_serial.csv]

#include <cstdio>
#include <exception>

#include "comun.hpp"

int main(int argc, char** argv) {
    try {
        const bolsa::Argumentos args = bolsa::leer_argumentos(argc, argv, "salida_serial.csv");
        const std::vector<std::string> urls = bolsa::leer_urls(args.archivo_urls);

        bolsa::TiemposSerial t;
        const bolsa::Bolsa resultado = bolsa::bolsa_serial(urls, args.salida, t);

        std::printf("=== Bolsa de palabras (version SERIAL) ===\n");
        std::printf("Libros (k):              %zu\n", urls.size());
        std::printf("Vocabulario (columnas):  %zu\n", resultado.vocabulario.size());
        std::printf("Tiempo total:            %.6f s\n", t.total);
        std::printf("   descarga:             %.6f s\n", t.descarga);
        std::printf("   conteo de palabras:   %.6f s\n", t.conteo);
        std::printf("   matriz:               %.6f s\n", t.matriz);
        std::printf("   escritura del CSV:    %.6f s\n", t.escritura);
        std::printf("Salida: %s\n", args.salida.c_str());
        std::printf("#TIEMPO %.6f\n", t.total);  // linea para los scripts
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        std::fprintf(stderr, "Uso: serial <archivo_urls> [--salida salida_serial.csv]\n");
        return 1;
    }
}
