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
#define MAX_TEXT 256

// Tamaño máximo del vector de floats.
#define MAX_VALUE2 32

// Tamaño de la cabecera de petición.
// 1 byte de operación + 4 bytes de tamaño del cuerpo.
#define PET_HDR 5

// Tamaño de la cabecera de respuesta.
// 1 byte de operación + 4 bytes de estado + 4 bytes de tamaño del cuerpo.
#define RES_HDR 9

// Límite máximo de cuerpo.
// Sirve para evitar reservar memoria absurda si llega una petición rota.
#define MAX_BODY 4096

// Códigos de operación.
// Tienen que coincidir con los del proxy.
enum operation_code {
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
struct request_data {
    uint8_t op;                    // Operación pedida
    char key[MAX_TEXT];            // Clave
    char value1[MAX_TEXT];         // value1
    int n_value2;                  // Número real de floats
    float v_value2[MAX_VALUE2];    // Vector de floats
    struct Paquete value3;         // Estructura con x, y, z
};

// Aquí guardamos el resultado de ejecutar una operación.
// Para get_value también guarda los datos que luego habrá que mandar.
struct response_data {
    int status;                    // Valor devuelto por la operación
    char value1[MAX_TEXT];         // value1 devuelto
    int n_value2;                  // N_value2 devuelto
    float v_value2[MAX_VALUE2];    // Vector devuelto
    struct Paquete value3;         // value3 devuelto
};

// Esta bandera vale 1 mientras el servidor debe seguir activo.
static volatile sig_atomic_t keep_running = 1;

// Aquí guardamos el socket del servidor para poder cerrarlo al recibir Ctrl+C.
static int server_fd = -1;

// Esta función se ejecuta cuando llega una señal para parar el servidor.
static void stop_server(int sig) {
    (void)sig;                     // No usamos el parámetro, solo evitamos warning
    keep_running = 0;              // Marcamos que el servidor debe terminar

    // Si el socket del servidor sigue abierto, lo cerramos.
    // Así accept() deja de bloquearse y el programa puede salir.
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

// Esta función envía exactamente n bytes.
// send() puede enviar menos bytes de golpe, así que repetimos hasta terminar.
static int send_all(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf;  // Puntero a los bytes que queremos mandar
    size_t sent = 0;               // Número de bytes ya enviados

    // Seguimos mientras queden bytes por enviar.
    while (sent < n) {
        // Intentamos enviar los bytes que faltan.
        ssize_t r = send(fd, p + sent, n - sent, 0);

        // Si send falla...
        if (r < 0) {
            // ...y fue por interrupción de señal, reintentamos.
            if (errno == EINTR) {
                continue;
            }

            // En otro caso, devolvemos error.
            return -1;
        }

        // Si devuelve 0, tratamos la conexión como fallida.
        if (r == 0) {
            return -1;
        }

        // Sumamos los bytes enviados en esta vuelta.
        sent += (size_t)r;
    }

    // Todo salió bien.
    return 0;
}

// Esta función recibe exactamente n bytes.
// recv() también puede devolver menos bytes de los pedidos.
static int recv_all(int fd, void *buf, size_t n) {
    unsigned char *p = buf;        // Puntero al buffer donde guardamos lo recibido
    size_t recvd = 0;              // Número de bytes ya recibidos

    // Seguimos mientras queden bytes por recibir.
    while (recvd < n) {
        // Intentamos recibir lo que falta.
        ssize_t r = recv(fd, p + recvd, n - recvd, 0);

        // Si recv falla...
        if (r < 0) {
            // ...y fue por interrupción de señal, reintentamos.
            if (errno == EINTR) {
                continue;
            }

            // En otro caso, devolvemos error.
            return -1;
        }

        // Si devuelve 0, el cliente cerró la conexión antes de tiempo.
        if (r == 0) {
            return -1;
        }

        // Sumamos los bytes recibidos.
        recvd += (size_t)r;
    }

    // Todo salió bien.
    return 0;
}

// Esta función escribe un entero de 32 bits en el buffer.
// Primero lo pasa a orden de red.
static void write_i32(unsigned char *buf, size_t *pos, int32_t v) {
    uint32_t net = htonl((uint32_t)v);     // Convertimos a orden de red

    // Copiamos el entero al buffer en la posición actual.
    memcpy(buf + *pos, &net, sizeof(net));

    // Avanzamos 4 bytes.
    *pos += sizeof(net);
}

// Esta función lee un entero de 32 bits desde el buffer.
// Después lo convierte de orden de red a orden local.
static int32_t read_i32(const unsigned char *buf, size_t *pos) {
    uint32_t net = 0;                      // Variable temporal

    // Leemos 4 bytes desde el buffer.
    memcpy(&net, buf + *pos, sizeof(net));

    // Avanzamos 4 bytes.
    *pos += sizeof(net);

    // Convertimos a orden local y devolvemos el valor.
    return (int32_t)ntohl(net);
}

// Esta función escribe un float en el buffer.
// Lo hacemos usando sus 4 bytes como si fuera un entero de 32 bits.
static void write_f32(unsigned char *buf, size_t *pos, float v) {
    uint32_t bits = 0;                     // Aquí guardamos temporalmente los 4 bytes del float

    // Copiamos los 4 bytes del float al entero temporal.
    memcpy(&bits, &v, sizeof(bits));

    // Convertimos esos 4 bytes a orden de red.
    bits = htonl(bits);

    // Guardamos esos 4 bytes en el buffer.
    memcpy(buf + *pos, &bits, sizeof(bits));

    // Avanzamos 4 bytes.
    *pos += sizeof(bits);
}

// Esta función lee un float desde el buffer.
// Hace el proceso contrario al de write_f32.
static float read_f32(const unsigned char *buf, size_t *pos) {
    uint32_t bits = 0;                     // Aquí leemos primero los 4 bytes
    float v = 0.0f;                        // Aquí reconstruimos el float final

    // Leemos 4 bytes desde el buffer.
    memcpy(&bits, buf + *pos, sizeof(bits));

    // Avanzamos 4 bytes.
    *pos += sizeof(bits);

    // Pasamos esos 4 bytes de orden de red a orden local.
    bits = ntohl(bits);

    // Copiamos esos 4 bytes dentro del float.
    memcpy(&v, &bits, sizeof(v));

    // Devolvemos el float reconstruido.
    return v;
}

// Esta función comprueba si aún quedan suficientes bytes en el buffer.
// Sirve para no leer fuera de la memoria.
static int has_bytes(size_t pos, size_t need, size_t total) {
    return pos <= total && need <= (total - pos);
}

// Esta función lee una cadena del buffer.
// El formato es:
// - 4 bytes con la longitud
// - luego los bytes de la cadena
static int read_string(const unsigned char *buf, size_t total, size_t *pos, char dst[MAX_TEXT]) {
    int32_t len32;                // Longitud leída como entero con signo
    size_t len;                   // Longitud convertida a size_t

    // Deben quedar al menos 4 bytes para leer la longitud.
    if (!has_bytes(*pos, sizeof(uint32_t), total)) {
        return -1;
    }

    // Leemos la longitud.
    len32 = read_i32(buf, pos);

    // Si es negativa, el mensaje es incorrecto.
    if (len32 < 0) {
        return -1;
    }

    // Convertimos la longitud.
    len = (size_t)len32;

    // La cadena no puede medir 256 o más, porque no cabría con el '\0'.
    if (len >= MAX_TEXT) {
        return -1;
    }

    // Comprobamos que queden realmente esos bytes en el buffer.
    if (!has_bytes(*pos, len, total)) {
        return -1;
    }

    // Copiamos la cadena.
    memcpy(dst, buf + *pos, len);

    // Añadimos el terminador nulo.
    dst[len] = '\0';

    // Avanzamos el desplazamiento.
    *pos += len;

    // Todo salió bien.
    return 0;
}

// Esta función construye el cuerpo de respuesta de get_value cuando sale bien.
// En ese cuerpo mandamos:
// - value1
// - N_value2
// - los floats
// - x, y, z
static int build_get_body(unsigned char **body, uint32_t *body_len, const struct response_data *res) {
    size_t len1 = strlen(res->value1);     // Longitud real de value1
    size_t total;                          // Tamaño total del cuerpo
    unsigned char *buf;                    // Buffer donde construiremos el cuerpo
    size_t pos = 0;                        // Posición actual dentro del buffer
    int i;                                 // Variable para recorrer los floats

    // Validamos value1 y n_value2.
    if (len1 >= MAX_TEXT || res->n_value2 < 1 || res->n_value2 > MAX_VALUE2) {
        return -1;
    }

    // Calculamos el tamaño total del cuerpo.
    total =
        sizeof(uint32_t) +                 // Longitud de value1
        len1 +                             // Bytes de value1
        sizeof(uint32_t) +                 // N_value2
        (size_t)res->n_value2 * sizeof(uint32_t) + // Floats
        3 * sizeof(uint32_t);              // x, y, z

    // Reservamos memoria.
    buf = malloc(total);

    // Si falla malloc, devolvemos error.
    if (buf == NULL) {
        return -1;
    }

    // Guardamos la longitud de value1.
    write_i32(buf, &pos, (int32_t)len1);

    // Guardamos los bytes de value1.
    memcpy(buf + pos, res->value1, len1);

    // Avanzamos el desplazamiento.
    pos += len1;

    // Guardamos N_value2.
    write_i32(buf, &pos, res->n_value2);

    // Guardamos todos los floats.
    for (i = 0; i < res->n_value2; i++) {
        write_f32(buf, &pos, res->v_value2[i]);
    }

    // Guardamos x.
    write_i32(buf, &pos, res->value3.x);

    // Guardamos y.
    write_i32(buf, &pos, res->value3.y);

    // Guardamos z.
    write_i32(buf, &pos, res->value3.z);

    // Devolvemos el buffer construido.
    *body = buf;

    // Devolvemos su tamaño real.
    *body_len = (uint32_t)pos;

    // Todo salió bien.
    return 0;
}

// Esta función convierte el cuerpo recibido en una petición interna.
// Según la operación, el cuerpo tiene un formato distinto.
static int parse_request(uint8_t op, const unsigned char *body, uint32_t body_len, struct request_data *req) {
    size_t pos = 0;                // Posición actual dentro del cuerpo
    int i;                         // Variable para recorrer floats

    // Dejamos la estructura limpia.
    memset(req, 0, sizeof(*req));

    // Guardamos la operación.
    req->op = op;

    // Elegimos cómo interpretar el cuerpo según la operación.
    switch (op) {
        case OP_DESTROY:
            // destroy no debe llevar cuerpo.
            return body_len == 0 ? 0 : -1;

        case OP_DELETE_KEY:
        case OP_EXIST:
        case OP_GET_VALUE:
            // Estas tres operaciones solo necesitan la key.
            if (read_string(body, body_len, &pos, req->key) == -1) {
                return -1;
            }

            // No deben sobrar bytes.
            return pos == body_len ? 0 : -1;

        case OP_SET_VALUE:
        case OP_MODIFY_VALUE:
            // Leemos key.
            if (read_string(body, body_len, &pos, req->key) == -1) {
                return -1;
            }

            // Leemos value1.
            if (read_string(body, body_len, &pos, req->value1) == -1) {
                return -1;
            }

            // Deben quedar al menos 4 bytes para N_value2.
            if (!has_bytes(pos, sizeof(uint32_t), body_len)) {
                return -1;
            }

            // Leemos N_value2.
            req->n_value2 = read_i32(body, &pos);

            // Validamos el rango.
            if (req->n_value2 < 1 || req->n_value2 > MAX_VALUE2) {
                return -1;
            }

            // Deben quedar bytes para los floats y para x, y, z.
            if (!has_bytes(pos,
                           (size_t)req->n_value2 * sizeof(uint32_t) + 3 * sizeof(uint32_t),
                           body_len)) {
                return -1;
            }

            // Leemos todos los floats.
            for (i = 0; i < req->n_value2; i++) {
                req->v_value2[i] = read_f32(body, &pos);
            }

            // Leemos x.
            req->value3.x = read_i32(body, &pos);

            // Leemos y.
            req->value3.y = read_i32(body, &pos);

            // Leemos z.
            req->value3.z = read_i32(body, &pos);

            // No deben sobrar bytes.
            return pos == body_len ? 0 : -1;

        default:
            // Si la operación no existe, devolvemos error.
            return -1;
    }
}

// Esta función llama a las funciones reales del servicio.
// Aquí conectamos la parte de red con claves.c.
static void run_request(const struct request_data *req, struct response_data *res) {
    // Dejamos la estructura de respuesta limpia.
    memset(res, 0, sizeof(*res));

    // Elegimos qué función llamar según la operación.
    switch (req->op) {
        case OP_DESTROY:
            res->status = destroy();
            break;

        case OP_SET_VALUE:
            res->status = set_value(req->key, req->value1, req->n_value2, (float *)req->v_value2, req->value3);
            break;

        case OP_GET_VALUE:
            res->status = get_value(req->key, res->value1, &res->n_value2, res->v_value2, &res->value3);
            break;

        case OP_MODIFY_VALUE:
            res->status = modify_value(req->key, req->value1, req->n_value2, (float *)req->v_value2, req->value3);
            break;

        case OP_DELETE_KEY:
            res->status = delete_key(req->key);
            break;

        case OP_EXIST:
            res->status = exist(req->key);
            break;

        default:
            res->status = -1;
            break;
    }
}

// Esta función manda la respuesta al cliente.
// Primero envía la cabecera y luego el cuerpo, si existe.
static int send_response(int fd, uint8_t op, int status, const unsigned char *body, uint32_t body_len) {
    unsigned char hdr[RES_HDR];    // Buffer de cabecera
    size_t pos = 0;                // Posición actual dentro de la cabecera

    // Guardamos la operación.
    hdr[pos++] = op;

    // Guardamos el estado.
    write_i32(hdr, &pos, (int32_t)status);

    // Guardamos la longitud del cuerpo.
    write_i32(hdr, &pos, (int32_t)body_len);

    // Enviamos la cabecera.
    if (send_all(fd, hdr, sizeof(hdr)) == -1) {
        return -1;
    }

    // Si hay cuerpo, lo enviamos también.
    if (body_len > 0 && send_all(fd, body, body_len) == -1) {
        return -1;
    }

    // Todo salió bien.
    return 0;
}

// Esta función atiende una conexión completa.
// Lee la petición, la interpreta, ejecuta la operación y manda la respuesta.
static void handle_client(int client_fd) {
    unsigned char hdr[PET_HDR];    // Aquí guardamos la cabecera recibida
    unsigned char *body = NULL;    // Aquí guardaremos el cuerpo recibido
    unsigned char *resp_body = NULL; // Aquí guardaremos el cuerpo de la respuesta
    uint8_t op;                    // Operación pedida
    int32_t body_len32;            // Tamaño del cuerpo como entero con signo
    uint32_t body_len;             // Tamaño del cuerpo ya validado
    size_t pos = 1;                // Empezamos en 1 porque el byte 0 es la operación
    struct request_data req;       // Petición ya interpretada
    struct response_data res;      // Resultado de la operación
    int status = -1;               // Estado final a devolver
    uint32_t resp_body_len = 0;    // Tamaño del cuerpo de la respuesta

    // Leemos la cabecera completa.
    if (recv_all(client_fd, hdr, sizeof(hdr)) == -1) {
        close(client_fd);
        return;
    }

    // El primer byte es la operación.
    op = hdr[0];

    // Los 4 bytes siguientes son el tamaño del cuerpo.
    body_len32 = read_i32(hdr, &pos);

    // Validamos ese tamaño.
    if (body_len32 < 0 || body_len32 > MAX_BODY) {
        (void)send_response(client_fd, op, -1, NULL, 0);
        close(client_fd);
        return;
    }

    // Convertimos el tamaño a uint32_t.
    body_len = (uint32_t)body_len32;

    // Si hay cuerpo, reservamos memoria para él.
    if (body_len > 0) {
        body = malloc(body_len);

        // Si falla malloc, respondemos con error.
        if (body == NULL) {
            (void)send_response(client_fd, op, -1, NULL, 0);
            close(client_fd);
            return;
        }

        // Leemos el cuerpo completo.
        if (recv_all(client_fd, body, body_len) == -1) {
            free(body);
            close(client_fd);
            return;
        }
    }

    // Interpretamos el cuerpo recibido.
    if (parse_request(op, body, body_len, &req) == -1) {
        (void)send_response(client_fd, op, -1, NULL, 0);
        free(body);
        close(client_fd);
        return;
    }

    // Ejecutamos la operación real.
    run_request(&req, &res);

    // Guardamos el estado devuelto.
    status = res.status;

    // Si era get_value y salió bien, construimos el cuerpo de respuesta.
    if (op == OP_GET_VALUE && status == 0) {
        if (build_get_body(&resp_body, &resp_body_len, &res) == -1) {
            status = -1;
            resp_body_len = 0;
        }
    }

    // Enviamos la respuesta.
    (void)send_response(client_fd, op, status, resp_body, resp_body_len);

    // Liberamos el cuerpo de respuesta.
    free(resp_body);

    // Liberamos el cuerpo recibido.
    free(body);

    // Cerramos la conexión con este cliente.
    close(client_fd);
}

// Esta es la función que ejecuta cada hilo.
// Cada hilo atiende a un cliente distinto.
static void *client_thread(void *arg) {
    int client_fd = *(int *)arg;   // Recuperamos el socket del cliente
    free(arg);                     // Liberamos la memoria reservada para pasarlo al hilo
    handle_client(client_fd);      // Atendemos al cliente
    return NULL;                   // El hilo termina aquí
}

// Función principal del servidor.
int main(int argc, char *argv[]) {
    struct sockaddr_in addr;       // Dirección del servidor en IPv4
    int port;                      // Puerto leído desde argv
    int opt = 1;                   // Valor para SO_REUSEADDR

    // Debe haber exactamente un argumento: el puerto.
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <PUERTO>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Convertimos el puerto de texto a entero.
    port = atoi(argv[1]);

    // Validamos que sea un puerto correcto.
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Puerto no válido.\n");
        return EXIT_FAILURE;
    }

    // Si llega Ctrl+C, llamamos a stop_server.
    signal(SIGINT, stop_server);

    // Si llega SIGTERM, llamamos a stop_server.
    signal(SIGTERM, stop_server);

    // Ignoramos SIGPIPE para que un send a un socket roto no mate el proceso.
    signal(SIGPIPE, SIG_IGN);

    // Creamos el socket del servidor.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Si falla, mostramos error y terminamos.
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Activamos SO_REUSEADDR para poder reutilizar antes el puerto.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // Dejamos la estructura de dirección a cero.
    memset(&addr, 0, sizeof(addr));

    // Indicamos que usamos IPv4.
    addr.sin_family = AF_INET;

    // Guardamos el puerto en orden de red.
    addr.sin_port = htons((uint16_t)port);

    // Escuchamos en todas las interfaces de red.
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Asociamos el socket al puerto.
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // Ponemos el socket en modo escucha.
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    // Mostramos que el servidor ya está listo.
    printf("Servidor TCP escuchando en el puerto %d...\n", port);

    // Bucle principal del servidor.
    while (keep_running) {
        int client_fd = accept(server_fd, NULL, NULL); // Esperamos una nueva conexión
        pthread_t tid;                                 // ID del nuevo hilo
        int *arg;                                      // Memoria para pasar el socket al hilo
        int err;                                       // Código de error de pthread_create

        // Si accept falla...
        if (client_fd == -1) {
            // ...y el servidor ya debe parar, salimos del bucle.
            if (!keep_running) {
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
        arg = malloc(sizeof(*arg));

        // Si falla malloc, cerramos el cliente y seguimos.
        if (arg == NULL) {
            close(client_fd);
            continue;
        }

        // Guardamos el socket del cliente.
        *arg = client_fd;

        // Creamos un hilo para atender a este cliente.
        err = pthread_create(&tid, NULL, client_thread, arg);

        // Si falla, limpiamos y seguimos.
        if (err != 0) {
            fprintf(stderr, "Error creando hilo: %s\n", strerror(err));
            close(client_fd);
            free(arg);
            continue;
        }

        // Dejamos el hilo detached para no tener que hacer pthread_join.
        pthread_detach(tid);
    }

    // Si el socket servidor sigue abierto, lo cerramos.
    if (server_fd != -1) {
        close(server_fd);
    }

    // Mostramos mensaje final.
    printf("Servidor TCP apagado correctamente.\n");

    // Terminamos bien.
    return EXIT_SUCCESS;
}