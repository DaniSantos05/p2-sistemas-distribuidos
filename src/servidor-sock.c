#include <arpa/inet.h>   // Para htons, htonl, ntohl
#include <errno.h>       // Para errno
#include <netinet/in.h>  // Para sockaddr_in, INADDR_ANY
#include <pthread.h>     // Para pthread_create y pthread_detach
#include <signal.h>      // Para signal
#include <stdint.h>      // Para uint8_t, uint32_t, int32_t
#include <stdio.h>       // Para printf, fprintf y perror
#include <stdlib.h>      // Para atoi, malloc, free, EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>      // Para memset, memcpy, strlen, strerror
#include <sys/socket.h>  // Para socket, bind, listen, accept, send, recv, setsockopt
#include <unistd.h>      // Para close

#include "claves.h"      // Para usar la API del servicio de claves

// Tamaño máximo de key y value1.
// Son 255 caracteres útiles + '\0'.
#define MAX_TEXTO 256

// Tamaño máximo del vector de floats.
#define MAX_VALUE2 32

// Tamaño de la cabecera de petición.
// 1 byte de operación + 4 bytes de tamaño del cuerpo.
#define TAM_CABECERA_PETICION 5

// Tamaño de la cabecera de respuesta.
// 1 byte de operación + 4 bytes de estado + 4 bytes de tamaño del cuerpo.
#define TAM_CABECERA_RESPUESTA 9

// Límite máximo del cuerpo.
// Sirve para evitar reservar memoria absurda si llega una petición rota.
#define MAX_CUERPO 4096

// Códigos de operación.
// Tienen que coincidir con los del proxy.
enum codigo_operacion {
    OP_DESTROY = 1,      // destroy
    OP_SET_VALUE = 2,    // set_value
    OP_GET_VALUE = 3,    // get_value
    OP_MODIFY_VALUE = 4, // modify_value
    OP_DELETE_KEY = 5,   // delete_key
    OP_EXIST = 6         // exist
};

// Aquí guardamos una petición ya leída y ya entendida.
// Esta estructura es interna del servidor.
// No se manda por red.
struct datos_peticion {
    uint8_t operacion;               // Operación pedida
    char clave[MAX_TEXTO];           // Clave
    char valor1[MAX_TEXTO];          // value1
    int n_value2;                    // Número real de floats
    float vector_value2[MAX_VALUE2]; // Vector de floats
    struct Paquete valor3;           // Estructura con x, y, z
};

// Aquí guardamos el resultado de ejecutar una operación.
// Para get_value también guarda los datos que luego habrá que mandar.
struct datos_respuesta {
    int estado;                      // Valor devuelto por la operación
    char valor1[MAX_TEXTO];          // value1 devuelto
    int n_value2;                    // N_value2 devuelto
    float vector_value2[MAX_VALUE2]; // Vector devuelto
    struct Paquete valor3;           // value3 devuelto
};

// Esta bandera vale 1 mientras el servidor debe seguir activo.
static volatile sig_atomic_t seguir_funcionando = 1;

// Aquí guardamos el socket del servidor para poder cerrarlo al recibir Ctrl+C.
static int descriptor_servidor = -1;

// Esta función se ejecuta cuando llega una señal para parar el servidor.
static void detener_servidor(int senal) {
    (void)senal;                     // No usamos el parámetro, solo evitamos warning
    seguir_funcionando = 0;          // Marcamos que el servidor debe terminar

    // Si el socket del servidor sigue abierto, lo cerramos.
    // Así accept() deja de bloquearse y el programa puede salir.
    if (descriptor_servidor != -1) {
        close(descriptor_servidor);
        descriptor_servidor = -1;
    }
}

// Esta función envía exactamente n bytes.
// send() puede enviar menos bytes de golpe, así que repetimos hasta terminar.
static int enviar_todo(int descriptor, const void *buffer, size_t cantidad_bytes) {
    const unsigned char *puntero = buffer; // Puntero a los bytes que queremos mandar
    size_t bytes_enviados = 0;             // Número de bytes ya enviados

    // Seguimos mientras queden bytes por enviar.
    while (bytes_enviados < cantidad_bytes) {
        // Intentamos enviar los bytes que faltan.
        ssize_t resultado = send(descriptor,
                                 puntero + bytes_enviados,
                                 cantidad_bytes - bytes_enviados,
                                 0);

        // Si send falla...
        if (resultado < 0) {
            // ...y fue por interrupción de señal, reintentamos.
            if (errno == EINTR) {
                continue;
            }

            // En otro caso, devolvemos error.
            return -1;
        }

        // Si devuelve 0, tratamos la conexión como fallida.
        if (resultado == 0) {
            return -1;
        }

        // Sumamos los bytes enviados en esta vuelta.
        bytes_enviados += (size_t)resultado;
    }

    // Todo salió bien.
    return 0;
}

// Esta función recibe exactamente n bytes.
// recv() también puede devolver menos bytes de los pedidos.
static int recibir_todo(int descriptor, void *buffer, size_t cantidad_bytes) {
    unsigned char *puntero = buffer; // Puntero al buffer donde guardamos lo recibido
    size_t bytes_recibidos = 0;      // Número de bytes ya recibidos

    // Seguimos mientras queden bytes por recibir.
    while (bytes_recibidos < cantidad_bytes) {
        // Intentamos recibir lo que falta.
        ssize_t resultado = recv(descriptor,
                                 puntero + bytes_recibidos,
                                 cantidad_bytes - bytes_recibidos,
                                 0);

        // Si recv falla...
        if (resultado < 0) {
            // ...y fue por interrupción de señal, reintentamos.
            if (errno == EINTR) {
                continue;
            }

            // En otro caso, devolvemos error.
            return -1;
        }

        // Si devuelve 0, el cliente cerró la conexión antes de tiempo.
        if (resultado == 0) {
            return -1;
        }

        // Sumamos los bytes recibidos.
        bytes_recibidos += (size_t)resultado;
    }

    // Todo salió bien.
    return 0;
}

// Esta función escribe un entero de 32 bits en el buffer.
// Primero lo pasa a orden de red.
static void escribir_entero_32(unsigned char *buffer, size_t *posicion, int32_t valor) {
    uint32_t valor_red = htonl((uint32_t)valor); // Convertimos a orden de red

    // Copiamos el entero al buffer en la posición actual.
    memcpy(buffer + *posicion, &valor_red, sizeof(valor_red));

    // Avanzamos 4 bytes.
    *posicion += sizeof(valor_red);
}

// Esta función lee un entero de 32 bits desde el buffer.
// Después lo convierte de orden de red a orden local.
static int32_t leer_entero_32(const unsigned char *buffer, size_t *posicion) {
    uint32_t valor_red = 0; // Variable temporal

    // Leemos 4 bytes desde el buffer.
    memcpy(&valor_red, buffer + *posicion, sizeof(valor_red));

    // Avanzamos 4 bytes.
    *posicion += sizeof(valor_red);

    // Convertimos a orden local y devolvemos el valor.
    return (int32_t)ntohl(valor_red);
}

// Esta función escribe un float en el buffer.
// Lo hacemos usando sus 4 bytes como si fuera un entero de 32 bits.
static void escribir_float_32(unsigned char *buffer, size_t *posicion, float valor) {
    uint32_t bits = 0; // Aquí guardamos temporalmente los 4 bytes del float

    // Copiamos los 4 bytes del float al entero temporal.
    memcpy(&bits, &valor, sizeof(bits));

    // Convertimos esos 4 bytes a orden de red.
    bits = htonl(bits);

    // Guardamos esos 4 bytes en el buffer.
    memcpy(buffer + *posicion, &bits, sizeof(bits));

    // Avanzamos 4 bytes.
    *posicion += sizeof(bits);
}

// Esta función lee un float desde el buffer.
// Hace el proceso contrario al de escribir_float_32.
static float leer_float_32(const unsigned char *buffer, size_t *posicion) {
    uint32_t bits = 0; // Aquí leemos primero los 4 bytes
    float valor = 0.0f; // Aquí reconstruimos el float final

    // Leemos 4 bytes desde el buffer.
    memcpy(&bits, buffer + *posicion, sizeof(bits));

    // Avanzamos 4 bytes.
    *posicion += sizeof(bits);

    // Pasamos esos 4 bytes de orden de red a orden local.
    bits = ntohl(bits);

    // Copiamos esos 4 bytes dentro del float.
    memcpy(&valor, &bits, sizeof(valor));

    // Devolvemos el float reconstruido.
    return valor;
}

// Esta función comprueba si aún quedan suficientes bytes en el buffer.
// Sirve para no leer fuera de la memoria.
static int quedan_bytes(size_t posicion, size_t necesarios, size_t total) {
    return posicion <= total && necesarios <= (total - posicion);
}

// Esta función lee una cadena del buffer.
// El formato es:
// - 4 bytes con la longitud
// - luego los bytes de la cadena
static int leer_cadena(const unsigned char *buffer,
                       size_t total,
                       size_t *posicion,
                       char destino[MAX_TEXTO]) {
    int32_t longitud_32; // Longitud leída como entero con signo
    size_t longitud;     // Longitud convertida a size_t

    // Deben quedar al menos 4 bytes para leer la longitud.
    if (!quedan_bytes(*posicion, sizeof(uint32_t), total)) {
        return -1;
    }

    // Leemos la longitud.
    longitud_32 = leer_entero_32(buffer, posicion);

    // Si es negativa, el mensaje es incorrecto.
    if (longitud_32 < 0) {
        return -1;
    }

    // Convertimos la longitud.
    longitud = (size_t)longitud_32;

    // La cadena no puede medir 256 o más, porque no cabría con el '\0'.
    if (longitud >= MAX_TEXTO) {
        return -1;
    }

    // Comprobamos que queden realmente esos bytes en el buffer.
    if (!quedan_bytes(*posicion, longitud, total)) {
        return -1;
    }

    // Copiamos la cadena.
    memcpy(destino, buffer + *posicion, longitud);

    // Añadimos el terminador nulo.
    destino[longitud] = '\0';

    // Avanzamos el desplazamiento.
    *posicion += longitud;

    // Todo salió bien.
    return 0;
}

// Esta función construye el cuerpo de respuesta de get_value cuando sale bien.
// En ese cuerpo mandamos:
// - value1
// - N_value2
// - los floats
// - x, y, z
static int construir_cuerpo_get_value(unsigned char **cuerpo,
                                      uint32_t *longitud_cuerpo,
                                      const struct datos_respuesta *respuesta) {
    size_t longitud_valor1 = strlen(respuesta->valor1); // Longitud real de value1
    size_t total;                                       // Tamaño total del cuerpo
    unsigned char *buffer;                              // Buffer donde construiremos el cuerpo
    size_t posicion = 0;                                // Posición actual dentro del buffer
    int i;                                              // Variable para recorrer los floats

    // Validamos value1 y n_value2.
    if (longitud_valor1 >= MAX_TEXTO ||
        respuesta->n_value2 < 1 ||
        respuesta->n_value2 > MAX_VALUE2) {
        return -1;
    }

    // Calculamos el tamaño total del cuerpo.
    total =
        sizeof(uint32_t) +                                  // Longitud de value1
        longitud_valor1 +                                   // Bytes de value1
        sizeof(uint32_t) +                                  // N_value2
        (size_t)respuesta->n_value2 * sizeof(uint32_t) +    // Floats
        3 * sizeof(uint32_t);                               // x, y, z

    // Reservamos memoria.
    buffer = malloc(total);

    // Si falla malloc, devolvemos error.
    if (buffer == NULL) {
        return -1;
    }

    // Guardamos la longitud de value1.
    escribir_entero_32(buffer, &posicion, (int32_t)longitud_valor1);

    // Guardamos los bytes de value1.
    memcpy(buffer + posicion, respuesta->valor1, longitud_valor1);

    // Avanzamos el desplazamiento.
    posicion += longitud_valor1;

    // Guardamos N_value2.
    escribir_entero_32(buffer, &posicion, respuesta->n_value2);

    // Guardamos todos los floats.
    for (i = 0; i < respuesta->n_value2; i++) {
        escribir_float_32(buffer, &posicion, respuesta->vector_value2[i]);
    }

    // Guardamos x.
    escribir_entero_32(buffer, &posicion, respuesta->valor3.x);

    // Guardamos y.
    escribir_entero_32(buffer, &posicion, respuesta->valor3.y);

    // Guardamos z.
    escribir_entero_32(buffer, &posicion, respuesta->valor3.z);

    // Devolvemos el buffer construido.
    *cuerpo = buffer;

    // Devolvemos su tamaño real.
    *longitud_cuerpo = (uint32_t)posicion;

    // Todo salió bien.
    return 0;
}

// Esta función convierte el cuerpo recibido en una petición interna.
// Según la operación, el cuerpo tiene un formato distinto.
static int interpretar_peticion(uint8_t operacion,
                                const unsigned char *cuerpo,
                                uint32_t longitud_cuerpo,
                                struct datos_peticion *peticion) {
    size_t posicion = 0; // Posición actual dentro del cuerpo
    int i;               // Variable para recorrer floats

    // Dejamos la estructura limpia.
    memset(peticion, 0, sizeof(*peticion));

    // Guardamos la operación.
    peticion->operacion = operacion;

    // Elegimos cómo interpretar el cuerpo según la operación.
    switch (operacion) {
        case OP_DESTROY:
            // destroy no debe llevar cuerpo.
            return longitud_cuerpo == 0 ? 0 : -1;

        case OP_DELETE_KEY:
        case OP_EXIST:
        case OP_GET_VALUE:
            // Estas tres operaciones solo necesitan la key.
            if (leer_cadena(cuerpo, longitud_cuerpo, &posicion, peticion->clave) == -1) {
                return -1;
            }

            // No deben sobrar bytes.
            return posicion == longitud_cuerpo ? 0 : -1;

        case OP_SET_VALUE:
        case OP_MODIFY_VALUE:
            // Leemos key.
            if (leer_cadena(cuerpo, longitud_cuerpo, &posicion, peticion->clave) == -1) {
                return -1;
            }

            // Leemos value1.
            if (leer_cadena(cuerpo, longitud_cuerpo, &posicion, peticion->valor1) == -1) {
                return -1;
            }

            // Deben quedar al menos 4 bytes para N_value2.
            if (!quedan_bytes(posicion, sizeof(uint32_t), longitud_cuerpo)) {
                return -1;
            }

            // Leemos N_value2.
            peticion->n_value2 = leer_entero_32(cuerpo, &posicion);

            // Validamos el rango.
            if (peticion->n_value2 < 1 || peticion->n_value2 > MAX_VALUE2) {
                return -1;
            }

            // Deben quedar bytes para los floats y para x, y, z.
            if (!quedan_bytes(posicion,
                              (size_t)peticion->n_value2 * sizeof(uint32_t) + 3 * sizeof(uint32_t),
                              longitud_cuerpo)) {
                return -1;
            }

            // Leemos todos los floats.
            for (i = 0; i < peticion->n_value2; i++) {
                peticion->vector_value2[i] = leer_float_32(cuerpo, &posicion);
            }

            // Leemos x.
            peticion->valor3.x = leer_entero_32(cuerpo, &posicion);

            // Leemos y.
            peticion->valor3.y = leer_entero_32(cuerpo, &posicion);

            // Leemos z.
            peticion->valor3.z = leer_entero_32(cuerpo, &posicion);

            // No deben sobrar bytes.
            return posicion == longitud_cuerpo ? 0 : -1;

        default:
            // Si la operación no existe, devolvemos error.
            return -1;
    }
}

// Esta función llama a las funciones reales del servicio.
// Aquí conectamos la parte de red con claves.c.
static void ejecutar_peticion(const struct datos_peticion *peticion,
                              struct datos_respuesta *respuesta) {
    // Dejamos la estructura de respuesta limpia.
    memset(respuesta, 0, sizeof(*respuesta));

    // Elegimos qué función llamar según la operación.
    switch (peticion->operacion) {
        case OP_DESTROY:
            respuesta->estado = destroy();
            break;

        case OP_SET_VALUE:
            respuesta->estado = set_value(peticion->clave,
                                          peticion->valor1,
                                          peticion->n_value2,
                                          (float *)peticion->vector_value2,
                                          peticion->valor3);
            break;

        case OP_GET_VALUE:
            respuesta->estado = get_value(peticion->clave,
                                          respuesta->valor1,
                                          &respuesta->n_value2,
                                          respuesta->vector_value2,
                                          &respuesta->valor3);
            break;

        case OP_MODIFY_VALUE:
            respuesta->estado = modify_value(peticion->clave,
                                             peticion->valor1,
                                             peticion->n_value2,
                                             (float *)peticion->vector_value2,
                                             peticion->valor3);
            break;

        case OP_DELETE_KEY:
            respuesta->estado = delete_key(peticion->clave);
            break;

        case OP_EXIST:
            respuesta->estado = exist(peticion->clave);
            break;

        default:
            respuesta->estado = -1;
            break;
    }
}

// Esta función manda la respuesta al cliente.
// Primero envía la cabecera y luego el cuerpo, si existe.
static int enviar_respuesta(int descriptor,
                            uint8_t operacion,
                            int estado,
                            const unsigned char *cuerpo,
                            uint32_t longitud_cuerpo) {
    unsigned char cabecera[TAM_CABECERA_RESPUESTA]; // Buffer de cabecera
    size_t posicion = 0;                            // Posición actual dentro de la cabecera

    // Guardamos la operación.
    cabecera[posicion++] = operacion;

    // Guardamos el estado.
    escribir_entero_32(cabecera, &posicion, (int32_t)estado);

    // Guardamos la longitud del cuerpo.
    escribir_entero_32(cabecera, &posicion, (int32_t)longitud_cuerpo);

    // Enviamos la cabecera.
    if (enviar_todo(descriptor, cabecera, sizeof(cabecera)) == -1) {
        return -1;
    }

    // Si hay cuerpo, lo enviamos también.
    if (longitud_cuerpo > 0 && enviar_todo(descriptor, cuerpo, longitud_cuerpo) == -1) {
        return -1;
    }

    // Todo salió bien.
    return 0;
}

// Esta función atiende una conexión completa.
// Lee la petición, la interpreta, ejecuta la operación y manda la respuesta.
static void atender_cliente(int descriptor_cliente) {
    unsigned char cabecera[TAM_CABECERA_PETICION]; // Aquí guardamos la cabecera recibida
    unsigned char *cuerpo = NULL;                  // Aquí guardaremos el cuerpo recibido
    unsigned char *cuerpo_respuesta = NULL;        // Aquí guardaremos el cuerpo de la respuesta
    uint8_t operacion;                             // Operación pedida
    int32_t longitud_cuerpo_32;                    // Tamaño del cuerpo como entero con signo
    uint32_t longitud_cuerpo;                      // Tamaño del cuerpo ya validado
    size_t posicion = 1;                           // Empezamos en 1 porque el byte 0 es la operación
    struct datos_peticion peticion;                // Petición ya interpretada
    struct datos_respuesta respuesta;              // Resultado de la operación
    int estado = -1;                               // Estado final a devolver
    uint32_t longitud_cuerpo_respuesta = 0;        // Tamaño del cuerpo de la respuesta

    // Leemos la cabecera completa.
    if (recibir_todo(descriptor_cliente, cabecera, sizeof(cabecera)) == -1) {
        close(descriptor_cliente);
        return;
    }

    // El primer byte es la operación.
    operacion = cabecera[0];

    // Los 4 bytes siguientes son el tamaño del cuerpo.
    longitud_cuerpo_32 = leer_entero_32(cabecera, &posicion);

    // Validamos ese tamaño.
    if (longitud_cuerpo_32 < 0 || longitud_cuerpo_32 > MAX_CUERPO) {
        (void)enviar_respuesta(descriptor_cliente, operacion, -1, NULL, 0);
        close(descriptor_cliente);
        return;
    }

    // Convertimos el tamaño a uint32_t.
    longitud_cuerpo = (uint32_t)longitud_cuerpo_32;

    // Si hay cuerpo, reservamos memoria para él.
    if (longitud_cuerpo > 0) {
        cuerpo = malloc(longitud_cuerpo);

        // Si falla malloc, respondemos con error.
        if (cuerpo == NULL) {
            (void)enviar_respuesta(descriptor_cliente, operacion, -1, NULL, 0);
            close(descriptor_cliente);
            return;
        }

        // Leemos el cuerpo completo.
        if (recibir_todo(descriptor_cliente, cuerpo, longitud_cuerpo) == -1) {
            free(cuerpo);
            close(descriptor_cliente);
            return;
        }
    }

    // Interpretamos el cuerpo recibido.
    if (interpretar_peticion(operacion, cuerpo, longitud_cuerpo, &peticion) == -1) {
        (void)enviar_respuesta(descriptor_cliente, operacion, -1, NULL, 0);
        free(cuerpo);
        close(descriptor_cliente);
        return;
    }

    // Ejecutamos la operación real.
    ejecutar_peticion(&peticion, &respuesta);

    // Guardamos el estado devuelto.
    estado = respuesta.estado;

    // Si era get_value y salió bien, construimos el cuerpo de respuesta.
    if (operacion == OP_GET_VALUE && estado == 0) {
        if (construir_cuerpo_get_value(&cuerpo_respuesta,
                                       &longitud_cuerpo_respuesta,
                                       &respuesta) == -1) {
            estado = -1;
            longitud_cuerpo_respuesta = 0;
        }
    }

    // Enviamos la respuesta.
    (void)enviar_respuesta(descriptor_cliente,
                           operacion,
                           estado,
                           cuerpo_respuesta,
                           longitud_cuerpo_respuesta);

    // Liberamos el cuerpo de respuesta.
    free(cuerpo_respuesta);

    // Liberamos el cuerpo recibido.
    free(cuerpo);

    // Cerramos la conexión con este cliente.
    close(descriptor_cliente);
}

// Esta es la función que ejecuta cada hilo.
// Cada hilo atiende a un cliente distinto.
static void *hilo_cliente(void *argumento) {
    int descriptor_cliente = *(int *)argumento; // Recuperamos el socket del cliente
    free(argumento);                            // Liberamos la memoria reservada para pasarlo al hilo
    atender_cliente(descriptor_cliente);        // Atendemos al cliente
    return NULL;                                // El hilo termina aquí
}

// Función principal del servidor.
int main(int argc, char *argv[]) {
    struct sockaddr_in direccion; // Dirección del servidor en IPv4
    int puerto;                   // Puerto leído desde argv
    int reutilizar_direccion = 1; // Valor para SO_REUSEADDR

    // Debe haber exactamente un argumento: el puerto.
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <PUERTO>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Convertimos el puerto de texto a entero.
    puerto = atoi(argv[1]);

    // Validamos que sea un puerto correcto.
    if (puerto <= 0 || puerto > 65535) {
        fprintf(stderr, "Puerto no válido.\n");
        return EXIT_FAILURE;
    }

    // Si llega Ctrl+C, llamamos a detener_servidor.
    signal(SIGINT, detener_servidor);

    // Si llega SIGTERM, llamamos a detener_servidor.
    signal(SIGTERM, detener_servidor);

    // Ignoramos SIGPIPE para que un send a un socket roto no mate el proceso.
    signal(SIGPIPE, SIG_IGN);

    // Creamos el socket del servidor.
    descriptor_servidor = socket(AF_INET, SOCK_STREAM, 0);

    // Si falla, mostramos error y terminamos.
    if (descriptor_servidor == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Activamos SO_REUSEADDR para poder reutilizar antes el puerto.
    if (setsockopt(descriptor_servidor,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reutilizar_direccion,
                   sizeof(reutilizar_direccion)) == -1) {
        perror("setsockopt");
        close(descriptor_servidor);
        return EXIT_FAILURE;
    }

    // Dejamos la estructura de dirección a cero.
    memset(&direccion, 0, sizeof(direccion));

    // Indicamos que usamos IPv4.
    direccion.sin_family = AF_INET;

    // Guardamos el puerto en orden de red.
    direccion.sin_port = htons((uint16_t)puerto);

    // Escuchamos en todas las interfaces de red.
    direccion.sin_addr.s_addr = htonl(INADDR_ANY);

    // Asociamos el socket al puerto.
    if (bind(descriptor_servidor, (struct sockaddr *)&direccion, sizeof(direccion)) == -1) {
        perror("bind");
        close(descriptor_servidor);
        return EXIT_FAILURE;
    }

    // Ponemos el socket en modo escucha.
    if (listen(descriptor_servidor, SOMAXCONN) == -1) {
        perror("listen");
        close(descriptor_servidor);
        return EXIT_FAILURE;
    }

    // Mostramos que el servidor ya está listo.
    printf("Servidor TCP escuchando en el puerto %d...\n", puerto);

    // Bucle principal del servidor.
    while (seguir_funcionando) {
        int descriptor_cliente = accept(descriptor_servidor, NULL, NULL); // Esperamos una nueva conexión
        pthread_t identificador_hilo;                                     // ID del nuevo hilo
        int *argumento_hilo;                                              // Memoria para pasar el socket al hilo
        int error_hilo;                                                   // Código de error de pthread_create

        // Si accept falla...
        if (descriptor_cliente == -1) {
            // ...y el servidor ya debe parar, salimos del bucle.
            if (!seguir_funcionando) {
                break;
            }

            // ...si fue por señal, seguimos.
            if (errno == EINTR) {
                continue;
            }

            // En otro caso, mostramos error y seguimos.
            perror("accept");
            continue;
        }

        // Reservamos memoria para pasar el socket al hilo.
        argumento_hilo = malloc(sizeof(*argumento_hilo));

        // Si falla malloc, cerramos el cliente y seguimos.
        if (argumento_hilo == NULL) {
            close(descriptor_cliente);
            continue;
        }

        // Guardamos el socket del cliente.
        *argumento_hilo = descriptor_cliente;

        // Creamos un hilo para atender a este cliente.
        error_hilo = pthread_create(&identificador_hilo, NULL, hilo_cliente, argumento_hilo);

        // Si falla, limpiamos y seguimos.
        if (error_hilo != 0) {
            fprintf(stderr, "Error creando hilo: %s\n", strerror(error_hilo));
            close(descriptor_cliente);
            free(argumento_hilo);
            continue;
        }

        // Dejamos el hilo detached para no tener que hacer pthread_join.
        pthread_detach(identificador_hilo);
    }

    // Si el socket servidor sigue abierto, lo cerramos.
    if (descriptor_servidor != -1) {
        close(descriptor_servidor);
    }

    // Mostramos mensaje final.
    printf("Servidor TCP apagado correctamente.\n");

    // Terminamos bien.
    return EXIT_SUCCESS;
}