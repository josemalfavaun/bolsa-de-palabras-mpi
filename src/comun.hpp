// comun.hpp
//
// Codigo compartido por la version serial y la version MPI.
//
// Todo lo que determina el RESULTADO (descarga, quitar encabezado/pie de
// Gutenberg, tokenizacion, conteo, vocabulario global y matriz) vive aqui, de
// modo que ambas versiones usan exactamente las mismas funciones y sus CSV
// tienen que coincidir byte por byte.
#pragma once

#include <sys/wait.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bolsa {

// Conteo de palabras de UN libro. std::map mantiene las palabras ordenadas.
using Conteo = std::map<std::string, int>;

// Resultado final: matriz k x V (renglon = libro, columna = palabra).
struct Bolsa {
    std::vector<std::string> vocabulario;       // V palabras, orden alfabetico
    std::vector<std::vector<int>> matriz;       // k renglones de V enteros
    bool operator==(const Bolsa& otra) const {
        return vocabulario == otra.vocabulario && matriz == otra.matriz;
    }
};

// Desglose de tiempos (en segundos) de la version serial.
struct TiemposSerial {
    double descarga = 0, conteo = 0, matriz = 0, escritura = 0, total = 0;
};

inline double ahora() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Argumentos de linea de comandos (los dos programas aceptan los mismos).
//   <archivo_urls> [--salida CSV] [--salida-serial CSV] [--tiempo-serial SEG]
// ---------------------------------------------------------------------------
struct Argumentos {
    std::string archivo_urls;
    std::string salida;
    std::string salida_serial = "salida_serial.csv";
    double tiempo_serial = -1;  // <= 0: el programa MPI lo mide el mismo
    bool explicar = false;      // --explicar: mensajes para presentar el proyecto
};

inline Argumentos leer_argumentos(int argc, char** argv, const std::string& salida_defecto) {
    Argumentos a;
    a.salida = salida_defecto;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto valor = [&](const char* nombre) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::string("Falta el valor de ") + nombre);
            return argv[++i];
        };
        if (s == "--salida") a.salida = valor("--salida");
        else if (s == "--salida-serial") a.salida_serial = valor("--salida-serial");
        else if (s == "--tiempo-serial") a.tiempo_serial = std::stod(valor("--tiempo-serial"));
        else if (s == "--explicar") a.explicar = true;
        else if (!s.empty() && s[0] == '-') throw std::runtime_error("Opcion desconocida: " + s);
        else if (a.archivo_urls.empty()) a.archivo_urls = s;
        else throw std::runtime_error("Argumento de sobra: " + s);
    }
    if (a.archivo_urls.empty()) throw std::runtime_error("Falta el archivo con la lista de URLs");
    return a;
}

// Lee un archivo con una URL por renglon. Ignora renglones vacios y los que
// empiezan con '#'.
inline std::vector<std::string> leer_urls(const std::string& ruta) {
    std::ifstream f(ruta);
    if (!f) throw std::runtime_error("No pude abrir el archivo de URLs: " + ruta);
    std::vector<std::string> urls;
    std::string linea;
    const char* blancos = " \t\r\n";
    while (std::getline(f, linea)) {
        size_t a = linea.find_first_not_of(blancos);
        if (a == std::string::npos) continue;
        size_t b = linea.find_last_not_of(blancos);
        linea = linea.substr(a, b - a + 1);
        if (linea[0] == '#') continue;
        urls.push_back(linea);
    }
    if (urls.empty()) throw std::runtime_error("El archivo de URLs no tiene ninguna URL: " + ruta);
    return urls;
}

// ---------------------------------------------------------------------------
// Descarga
// ---------------------------------------------------------------------------

// La URL se pasa a curl dentro de comillas simples, asi que solo aceptamos
// caracteres "seguros" (nada de comillas, espacios, ';', '$', '`', etc.).
inline bool url_segura(const std::string& url) {
    const std::string_view permitidos = "-._~:/?=&%+@,#";
    for (unsigned char c : url) {
        bool letra_o_digito = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!letra_o_digito && permitidos.find(static_cast<char>(c)) == std::string_view::npos) return false;
    }
    return true;
}

inline std::string leer_archivo(const std::string& ruta) {
    std::ifstream f(ruta, std::ios::binary);
    if (!f) throw std::runtime_error("No pude abrir el archivo: " + ruta);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// User-Agent que identifica al programa (Project Gutenberg pide que los clientes
// automaticos se identifiquen). Se puede cambiar con la variable de entorno
// BOLSA_USER_AGENT; no puede contener comillas simples.
inline std::string agente_usuario() {
    const char* env = std::getenv("BOLSA_USER_AGENT");
    std::string ua = env ? env : "";
    if (ua.empty() || ua.find('\'') != std::string::npos)
        ua = "bolsa-de-palabras-mpi/1.0 (+https://github.com/josemalfavaun/bolsa-de-palabras-mpi; proyecto escolar ITAM)";
    return ua;
}

// Devuelve el texto de "fuente". Si empieza con http:// o https:// lo descarga
// con curl; si no, lo trata como ruta de un archivo local (util para probar sin
// red). Lanza std::runtime_error si algo sale mal.
inline std::string descargar(const std::string& fuente) {
    const bool es_url = fuente.rfind("http://", 0) == 0 || fuente.rfind("https://", 0) == 0;
    std::string texto;
    if (!es_url) {
        texto = leer_archivo(fuente);
    } else {
        if (!url_segura(fuente)) throw std::runtime_error("La URL tiene caracteres no permitidos: " + fuente);
        const std::string comando =
            "curl --fail --silent --show-error --location --retry 3 --retry-delay 1 "
            "--connect-timeout 20 --max-time 300 "
            "-A '" + agente_usuario() + "' '" + fuente + "'";
        FILE* tuberia = popen(comando.c_str(), "r");
        if (!tuberia) throw std::runtime_error("No pude ejecutar curl (esta instalado?)");
        char buffer[65536];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, tuberia)) > 0) texto.append(buffer, n);
        const int estado = pclose(tuberia);
        if (estado == -1 || !WIFEXITED(estado) || WEXITSTATUS(estado) != 0)
            throw std::runtime_error("curl fallo al descargar " + fuente);
    }
    if (texto.size() < 1000) throw std::runtime_error("Contenido vacio o demasiado corto: " + fuente);
    if (texto.compare(0, 9, "<!DOCTYPE") == 0 || texto.compare(0, 5, "<html") == 0)
        throw std::runtime_error("Se recibio una pagina HTML, no texto plano: " + fuente);
    return texto;
}

// ---------------------------------------------------------------------------
// Tokenizacion unica
// ---------------------------------------------------------------------------

// Se queda solo con el cuerpo del libro: lo que esta entre la linea
// "*** START OF ..." y la linea "*** END OF ..." de Project Gutenberg. Si no
// encuentra las marcas, devuelve el texto completo.
inline std::string_view quitar_encabezado_y_pie(std::string_view t) {
    size_t inicio = 0, fin = t.size();
    size_t s = t.find("*** START OF");
    if (s == std::string_view::npos) s = t.find("***START OF");
    if (s != std::string_view::npos) {
        size_t salto = t.find('\n', s);
        inicio = (salto == std::string_view::npos) ? t.size() : salto + 1;
    }
    size_t e = t.rfind("*** END OF");
    if (e == std::string_view::npos) e = t.rfind("***END OF");
    if (e != std::string_view::npos && e >= inicio) fin = e;
    return t.substr(inicio, fin - inicio);
}

// Reglas (iguales para serial y MPI; equivalen a CountVectorizer de sklearn con
// sus valores por defecto, salvo por los acentos):
//   - se pasa a minusculas (solo ASCII);
//   - una palabra es una racha de [a-z0-9]; todo lo demas (puntuacion, espacios,
//     guion bajo y cualquier byte no ASCII) separa palabras;
//   - se descartan palabras de 1 solo caracter.
inline Conteo contar_palabras(const std::string& texto_completo) {
    const std::string_view cuerpo = quitar_encabezado_y_pie(texto_completo);
    Conteo conteo;
    std::string palabra;
    auto cerrar = [&]() {
        if (palabra.size() >= 2) ++conteo[palabra];
        palabra.clear();
    };
    for (unsigned char c : cuerpo) {
        if (c >= 'A' && c <= 'Z') palabra.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) palabra.push_back(static_cast<char>(c));
        else cerrar();
    }
    cerrar();
    return conteo;
}

// ---------------------------------------------------------------------------
// Matriz y CSV
// ---------------------------------------------------------------------------

// Une los conteos de los k libros: vocabulario global ordenado + matriz k x V.
inline Bolsa construir_bolsa(const std::vector<Conteo>& por_libro) {
    std::map<std::string, int> indice;  // palabra -> columna
    for (const auto& conteo : por_libro)
        for (const auto& par : conteo) indice.emplace(par.first, 0);

    Bolsa bolsa;
    bolsa.vocabulario.reserve(indice.size());
    int columna = 0;
    for (auto& par : indice) {
        par.second = columna++;
        bolsa.vocabulario.push_back(par.first);
    }
    bolsa.matriz.assign(por_libro.size(), std::vector<int>(indice.size(), 0));
    for (size_t libro = 0; libro < por_libro.size(); ++libro)
        for (const auto& par : por_libro[libro]) bolsa.matriz[libro][indice.at(par.first)] = par.second;
    return bolsa;
}

// Formato: primer renglon = "libro,<palabra1>,<palabra2>,...";
// luego un renglon por libro: "<indice del libro>,<conteo1>,<conteo2>,...".
// El indice es la posicion de la URL en el archivo de entrada (empieza en 0).
inline void escribir_csv(const std::string& ruta, const Bolsa& bolsa) {
    std::ofstream f(ruta, std::ios::binary);
    if (!f) throw std::runtime_error("No pude crear el archivo de salida: " + ruta);
    f << "libro";
    for (const auto& palabra : bolsa.vocabulario) f << ',' << palabra;
    f << '\n';
    for (size_t libro = 0; libro < bolsa.matriz.size(); ++libro) {
        f << libro;
        for (int cuenta : bolsa.matriz[libro]) f << ',' << cuenta;
        f << '\n';
    }
    f.flush();
    if (!f) throw std::runtime_error("Error al escribir el archivo de salida: " + ruta);
}

// ---------------------------------------------------------------------------
// Version serial completa (descarga + conteo + matriz + CSV)
// ---------------------------------------------------------------------------
inline Bolsa bolsa_serial(const std::vector<std::string>& urls, const std::string& salida, TiemposSerial& t) {
    const double inicio = ahora();
    std::vector<Conteo> por_libro;
    por_libro.reserve(urls.size());
    for (const auto& url : urls) {
        const double a = ahora();
        const std::string texto = descargar(url);
        const double b = ahora();
        por_libro.push_back(contar_palabras(texto));
        const double c = ahora();
        t.descarga += b - a;
        t.conteo += c - b;
    }
    const double d = ahora();
    Bolsa bolsa = construir_bolsa(por_libro);
    const double e = ahora();
    escribir_csv(salida, bolsa);
    const double f = ahora();
    t.matriz = e - d;
    t.escritura = f - e;
    t.total = f - inicio;
    return bolsa;
}

}  // namespace bolsa
