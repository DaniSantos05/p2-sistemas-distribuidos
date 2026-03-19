#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "claves.h"

// -----------------------------------------------------------------------------
// CODIGOS DE OPERACION DEL PROTOCOLO
// -----------------------------------------------------------------------------
// Cada funcion de la API de claves.h se convierte en una operacion remota.
// Estos numeros viajaran por la red para que el servidor sepa
// que accion le esta pidiendo el cliente.
#define CODIGO_DESTROY       1
#define CODIGO_SET_VALUE     2
#define CODIGO_GET_VALUE     3
#define CODIGO_MODIFY_VALUE  4
#define CODIGO_DELETE_KEY    5
#define CODIGO_EXIST         6

// -----------------------------------------------------------------------------
// TAMANOS DE CABECERA DEL PROTOCOLO
// -----------------------------------------------------------------------------
// La peticion del cliente empieza con:
//   - 1 byte: codigo de operacion
//   - 4 bytes: longitud del cuerpo
//
// La respuesta del servidor empieza con:
//   - 1 byte: codigo de operacion
//   - 4 bytes: estado devuelto por la operacion
//   - 4 bytes: longitud del cuerpo de respuesta
#define TAM_CABECERA_PETICION 5
#define TAM_CABECERA_RESPUESTA 9

// -----------------------------------------------------------------------------
// FUNCION: enviar_todo
// -----------------------------------------------------------------------------
// Objetivo:
//   Enviar exactamente 'longitud' bytes por el socket.
//
// Por que hace falta:
//   La funcion send() NO garantiza que envie todos los bytes de golpe.
//   A veces envia solo una parte.
//   Por eso repetimos en bucle hasta completar el envio.
static int enviar_todo(int descriptor_socket, const void *buffer, size_t longitud) {
    const unsigned char *puntero = (const unsigned char *)buffer;
    size_t bytes_enviados = 0;

    // Seguimos mientras aun queden bytes por enviar.
    while (bytes_enviados < longitud) {
        ssize_t resultado = send(descriptor_socket,
                                 puntero + bytes_enviados,
                                 longitud - bytes_enviados,
                                 0);

        // Si send() devuelve 0 o negativo, consideramos que ha habido error.
        if (resultado <= 0) {
            return -1;
        }

        // Avanzamos el numero de bytes ya enviados.
        bytes_enviados += (size_t)resultado;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// FUNCION: recibir_todo
// -----------------------------------------------------------------------------
// Objetivo:
//   Recibir exactamente 'longitud' bytes por el socket.
//
// Por que hace falta:
//   Igual que con send(), recv() puede devolver menos bytes de los pedidos.
//   Asi que tambien repetimos hasta recibirlo todo.
static int recibir_todo(int descriptor_socket, void *buffer, size_t longitud) {
    unsigned char *puntero = (unsigned char *)buffer;
    size_t bytes_recibidos = 0;

    // Seguimos mientras aun falten bytes por recibir.
    while (bytes_recibidos < longitud) {
        ssize_t resultado = recv(descriptor_socket,
                                 puntero + bytes_recibidos,
                                 longitud - bytes_recibidos,
                                 0);

        // Si recv() devuelve 0 o negativo, hubo error o cierre inesperado.
        if (resultado <= 0) {
            return -1;
        }

        // Sumamos lo recibido en esta iteracion.
        bytes_recibidos += (size_t)resultado;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// FUNCION: escribir_entero32
// -----------------------------------------------------------------------------
// Objetivo:
//   Guardar un entero de 32 bits dentro de un buffer de bytes.
//
// Detalle importante:
//   Lo convertimos antes a "orden de red".
//   Esto se hace para que la comunicacion no dependa de como guarda
//   los enteros cada maquina internamente.
static void escribir_entero32(unsigned char *buffer, size_t *desplazamiento, int32_t valor) {
    uint32_t valor_red = htonl((uint32_t)valor);

    // Copiamos el entero convertido al buffer en la posicion actual.
    memcpy(buffer + *desplazamiento, &valor_red, sizeof(valor_red));

    // Movemos el desplazamiento para apuntar al siguiente hueco libre.
    *desplazamiento += sizeof(valor_red);
}

// -----------------------------------------------------------------------------
// FUNCION: leer_entero32
// -----------------------------------------------------------------------------
// Objetivo:
//   Leer un entero de 32 bits desde un buffer.
//
// Detalle importante:
//   El entero viene en orden de red, asi que lo convertimos a orden local.
static int32_t leer_entero32(const unsigned char *buffer, size_t *desplazamiento) {
    uint32_t valor_red;

    // Copiamos los 4 bytes del entero desde el buffer.
    memcpy(&valor_red, buffer + *desplazamiento, sizeof(valor_red));

    // Avanzamos el desplazamiento para futuras lecturas.
    *desplazamiento += sizeof(valor_red);

    // Convertimos de orden de red a orden local.
    return (int32_t)ntohl(valor_red);
}

// -----------------------------------------------------------------------------
// FUNCION: enviar_respuesta
// -----------------------------------------------------------------------------
// Objetivo:
//   Construir y enviar una respuesta completa al cliente.
//
// Que envia:
//   - codigo de operacion
//   - estado devuelto por la operacion
//   - longitud del cuerpo
//   - cuerpo (si existe)
//
// En este primer commit el cuerpo normalmente ira vacio.
static int enviar_respuesta(int descriptor_cliente,
                            uint8_t codigo_operacion,
                            int estado,
                            const unsigned char *cuerpo,
                            uint32_t longitud_cuerpo) {
    unsigned char cabecera[TAM_CABECERA_RESPUESTA];
    size_t desplazamiento = 0;

    // Primer byte: codigo de operacion.
    cabecera[desplazamiento++] = codigo_operacion;

    // Siguientes 4 bytes: estado de la operacion.
    escribir_entero32(cabecera, &desplazamiento, (int32_t)estado);

    // Siguientes 4 bytes: longitud del cuerpo de respuesta.
    escribir_entero32(cabecera, &desplazamiento, (int32_t)longitud_cuerpo);

    // Enviamos la cabecera.
    if (enviar_todo(descriptor_cliente, cabecera, sizeof(cabecera)) == -1) {
        return -1;
    }

    // Si hay cuerpo, lo enviamos tambien.
    if (longitud_cuerpo > 0 && enviar_todo(descriptor_cliente, cuerpo, longitud_cuerpo) == -1) {
        return -1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// FUNCION: atender_cliente
// -----------------------------------------------------------------------------
// Objetivo:
//   Atender una conexion entrante de un cliente.
//
// En este primer commit aun NO resolvemos de verdad las operaciones.
// Solo hacemos la parte de red:
//   1. leer la cabecera
//   2. leer el cuerpo
//   3. contestar con error generico
//
// Esto nos deja una base solida para luego añadir la logica real.
static void atender_cliente(int descriptor_cliente) {
    unsigned char cabecera[TAM_CABECERA_PETICION];
    unsigned char *cuerpo = NULL;
    size_t desplazamiento = 1;
    int32_t longitud_cuerpo32;
    uint8_t codigo_operacion;

    // Leemos la cabecera completa de la peticion.
    if (recibir_todo(descriptor_cliente, cabecera, sizeof(cabecera)) == -1) {
        close(descriptor_cliente);
        return;
    }

    // El primer byte indica que operacion pide el cliente.
    codigo_operacion = cabecera[0];

    // Los siguientes 4 bytes indican cuantos bytes ocupa el cuerpo.
    longitud_cuerpo32 = leer_entero32(cabecera, &desplazamiento);

    // Si la longitud es negativa, la peticion es invalida.
    if (longitud_cuerpo32 < 0) {
        enviar_respuesta(descriptor_cliente, codigo_operacion, -1, NULL, 0);
        close(descriptor_cliente);
        return;
    }

    // Si existe cuerpo, reservamos memoria para guardarlo.
    if (longitud_cuerpo32 > 0) {
        cuerpo = (unsigned char *)malloc((size_t)longitud_cuerpo32);

        // Si no hay memoria disponible, respondemos con error.
        if (cuerpo == NULL) {
            enviar_respuesta(descriptor_cliente, codigo_operacion, -1, NULL, 0);
            close(descriptor_cliente);
            return;
        }

        // Leemos el cuerpo completo de la peticion.
        if (recibir_todo(descriptor_cliente, cuerpo, (size_t)longitud_cuerpo32) == -1) {
            free(cuerpo);
            close(descriptor_cliente);
            return;
        }
    }

    // En este primer commit todavia no interpretamos el contenido real.
    // Solo devolvemos error generico para comprobar que:
    //   - el servidor escucha
    //   - acepta conexiones
    //   - recibe peticiones
    //   - y puede responder
    enviar_respuesta(descriptor_cliente, codigo_operacion, -1, NULL, 0);

    // Liberamos memoria si se reservo.
    free(cuerpo);

    // Cerramos la conexion con este cliente.
    close(descriptor_cliente);
}

// -----------------------------------------------------------------------------
// FUNCION PRINCIPAL
// -----------------------------------------------------------------------------
// Objetivo:
//   Arrancar el servidor TCP, escuchar en un puerto y aceptar clientes.
int main(int argc, char *argv[]) {
    struct addrinfo pistas;
    struct addrinfo *resultado = NULL;
    struct addrinfo *recorrido;
    int descriptor_servidor = -1;
    int error_getaddrinfo;
    int reutilizar = 1;

    // El servidor se ejecuta asi:
    // ./servidor <PUERTO>
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <PUERTO>\n", argv[0]);
        return 1;
    }

    // Dejamos la estructura de pistas a cero para no tener basura.
    memset(&pistas, 0, sizeof(pistas));

    // AF_UNSPEC permite que funcione con IPv4 o IPv6.
    pistas.ai_family = AF_UNSPEC;

    // SOCK_STREAM significa TCP.
    pistas.ai_socktype = SOCK_STREAM;

    // AI_PASSIVE indica que queremos una direccion local para hacer bind.
    pistas.ai_flags = AI_PASSIVE;

    // Pedimos al sistema las direcciones posibles para escuchar en ese puerto.
    error_getaddrinfo = getaddrinfo(NULL, argv[1], &pistas, &resultado);
    if (error_getaddrinfo != 0) {
        fprintf(stderr, "Error en getaddrinfo: %s\n", gai_strerror(error_getaddrinfo));
        return 1;
    }

    // Recorremos las direcciones devueltas hasta encontrar una valida.
    for (recorrido = resultado; recorrido != NULL; recorrido = recorrido->ai_next) {
        // Intentamos crear un socket con esa direccion.
        descriptor_servidor = socket(recorrido->ai_family,
                                     recorrido->ai_socktype,
                                     recorrido->ai_protocol);

        if (descriptor_servidor == -1) {
            continue;
        }

        // SO_REUSEADDR permite reutilizar antes el puerto
        // si el programa se reinicia.
        if (setsockopt(descriptor_servidor,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &reutilizar,
                       sizeof(reutilizar)) == -1) {
            close(descriptor_servidor);
            descriptor_servidor = -1;
            continue;
        }

        // Intentamos asociar el socket a la direccion y puerto elegidos.
        if (bind(descriptor_servidor,
                 recorrido->ai_addr,
                 recorrido->ai_addrlen) == 0) {
            break;
        }

        // Si falla, cerramos y probamos la siguiente direccion.
        close(descriptor_servidor);
        descriptor_servidor = -1;
    }

    // Ya no necesitamos la lista de direcciones.
    freeaddrinfo(resultado);

    // Si sigue valiendo -1, ninguna direccion nos sirvio.
    if (descriptor_servidor == -1) {
        fprintf(stderr, "No se pudo crear ni asociar el socket del servidor.\n");
        return 1;
    }

    // Ponemos el socket en modo escucha.
    if (listen(descriptor_servidor, SOMAXCONN) == -1) {
        perror("listen");
        close(descriptor_servidor);
        return 1;
    }

    printf("Servidor TCP escuchando en el puerto %s...\n", argv[1]);

    // Bucle infinito del servidor:
    // acepta un cliente, lo atiende y luego vuelve a esperar otro.
    while (1) {
        int descriptor_cliente = accept(descriptor_servidor, NULL, NULL);

        // Si accept falla, seguimos esperando otro cliente.
        if (descriptor_cliente == -1) {
            continue;
        }

        atender_cliente(descriptor_cliente);
    }

    close(descriptor_servidor);
    return 0;
}