#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "claves.h"

// Cola principal del servidor donde todos los clientes envian sus peticiones
#define SERVER_QUEUE_NAME "/mq_servidor_claves"

// Prefijo para construir la cola privada de cada cliente
#define CLIENT_QUEUE_PREFIX "/mq_cliente_claves_"

// Tamaños maximos
#define MAX_TEXT 256
#define MAX_VALUE2 32

// Codigos para identificar que operacion pide el cliente
enum operation_code {
    OP_DESTROY = 1,
    OP_SET_VALUE = 2,
    OP_GET_VALUE = 3,
    OP_MODIFY_VALUE = 4,
    OP_DELETE_KEY = 5,
    OP_EXIST = 6
};

// Estructura de la peticion que viaja del cliente al servidor
struct request_msg {
    int op;                     // Codigo de la operacion
    pid_t client_pid;           // PID del cliente para saber a que cola responder
    char key[MAX_TEXT];         
    char value1[MAX_TEXT];      
    int n_value2;               
    float v_value2[MAX_VALUE2]; 
    struct Paquete value3;      
};

// Estructura de la respuesta que viaja del servidor al cliente
struct response_msg {
    int result;                 // 0 si exito, -1 si error de logica
    char value1[MAX_TEXT];      
    int n_value2;               
    float v_value2[MAX_VALUE2]; 
    struct Paquete value3;      
};

// Datos que le pasamos al hilo recien creado
struct thread_data {
    struct request_msg request;
};

// Bandera global para detener el servidor de forma segura
static volatile sig_atomic_t keep_running = 1;

// Manejador para atrapar el Ctrl+C, que pone la bandera a 0 para que el servidor deje de aceptar clientes y cierre
static void stop_server(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

// Crea el nombre unico de la cola del cliente usando su PID
static int build_client_queue_name(char *buffer, size_t buffer_len, pid_t pid) {
    int written = snprintf(buffer, buffer_len, "%s%ld", CLIENT_QUEUE_PREFIX, (long)pid);
    if (written < 0 || (size_t)written >= buffer_len) {
        return -1;
    }
    return 0;
}

// Procesa la peticion llamando a las funciones locales y rellena la respuesta
static void process_request(struct request_msg *request, struct response_msg *response) {
    // Limpiamos la estructura de respuesta por seguridad
    memset(response, 0, sizeof(*response));

    switch (request -> op) {
        case OP_DESTROY:
            response -> result = destroy();
            break;

        case OP_SET_VALUE:
            response -> result = set_value(
                request -> key,
                request -> value1,
                request -> n_value2,
                request -> v_value2,
                request -> value3
            );
            break;

        case OP_GET_VALUE: {
            int n_value2 = 0;
            char value1[MAX_TEXT] = {0};
            float v_value2[MAX_VALUE2] = {0.0f};
            struct Paquete value3;

            response -> result = get_value(
                request -> key,
                value1,
                &n_value2,
                v_value2,
                &value3
            );

            // Solo rellenamos los datos si la clave existia
            if (response -> result == 0) {
                memcpy(response -> value1, value1, sizeof(value1));
                response -> n_value2 = n_value2;
                memcpy(response -> v_value2, v_value2, sizeof(v_value2));
                response -> value3 = value3;
            }
            break;
        }

        case OP_MODIFY_VALUE:
            response -> result = modify_value(
                request -> key,
                request -> value1,
                request -> n_value2,
                request -> v_value2,
                request -> value3
            );
            break;

        case OP_DELETE_KEY:
            response -> result = delete_key(request -> key);
            break;

        case OP_EXIST:
            response -> result = exist(request -> key);
            break;

        default:
            response -> result = -1;
            break;
    }
}

// Funcion principal que ejecuta cada hilo trabajador
static void *worker_thread(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    struct response_msg response;
    char client_queue_name[64];

    // Aviso de que el hilo ha empezado a trabajar
    printf("[HILO %lu] Iniciando atencion al Cliente PID: %d (Operacion: %d)\n", 
           (unsigned long)pthread_self(), data->request.client_pid, data->request.op);

    // Llamamos a la logica del servicio
    process_request(&data->request, &response);

    // Averiguamos como se llama el buzon de este cliente
    if (build_client_queue_name(client_queue_name, sizeof(client_queue_name), data -> request.client_pid) != 0) {
        fprintf(stderr, "Error al generar nombre de cola cliente\n");
        free(data);
        return NULL;
    }

    // Abrimos su buzon temporal
    mqd_t client_mq = mq_open(client_queue_name, O_WRONLY);
    if (client_mq == (mqd_t)-1) {
        fprintf(stderr, "Error al abrir cola cliente %s: %s\n", client_queue_name, strerror(errno));
        free(data);
        return NULL;
    }

    // Le devolvemos el paquete con la respuesta
    if (mq_send(client_mq, (const char *)&response, sizeof(response), 0) < 0) {
        fprintf(stderr, "Error enviando a %s: %s\n", client_queue_name, strerror(errno));
    } else {
        // ---> CAMBIO 2: Confirmación visual de que la respuesta ha llegado bien <---
        printf("[HILO %lu] Respuesta enviada con EXITO al Cliente PID: %d\n", 
               (unsigned long)pthread_self(), data->request.client_pid);
    }

    // Cerramos nuestro acceso a su cola
    mq_close(client_mq);

    // Limpiamos la memoria dinamica que nos pasaron
    free(data);
    return NULL;
}

int main(void) {
    mqd_t server_mq;
    struct mq_attr request_attr;

    // Enganchamos las senales para poder salir limpiamente
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);

    // Aseguramos que empezamos con la cola limpia
    mq_unlink(SERVER_QUEUE_NAME);

    // Configuramos como queremos la cola del servidor
    memset(&request_attr, 0, sizeof(request_attr));
    request_attr.mq_flags = 0;                            
    request_attr.mq_maxmsg = 10;                          
    request_attr.mq_msgsize = sizeof(struct request_msg); 

    // Abrimos el buzon principal
    server_mq = mq_open(SERVER_QUEUE_NAME, O_CREAT | O_RDONLY, 0660, &request_attr);
    if (server_mq == (mqd_t)-1) {
        perror("Error creando la cola del servidor");
        return EXIT_FAILURE;
    }

    printf("Servidor activo escuchando en %s\n", SERVER_QUEUE_NAME);

    // Bucle infinito hasta que nos pidan parar, cada iteracion es un nuevo cliente
    while (keep_running) {
        struct request_msg request;

        // Nos bloqueamos aqui esperando que alguien meta un sobre en el buzon
        ssize_t bytes_received = mq_receive(server_mq, (char *)&request, sizeof(request), NULL);

        if (bytes_received < 0) {
            // Si nos despertaron para cerrar el programa volvemos a evaluar el while
            if (errno == EINTR) {
                continue;
            }
            perror("Error recibiendo mensaje");
            break;
        }

        if ((size_t)bytes_received != sizeof(request)) {
            fprintf(stderr, "Mensaje con tamaño incorrecto\n");
            continue;
        }

        // Creamos una caja de memoria para que el hilo tenga sus propios datos
        struct thread_data *data = malloc(sizeof(struct thread_data));
        if (data == NULL) {
            fprintf(stderr, "Error de memoria al crear tarea\n");
            continue;
        }

        data -> request = request;

        // Despertamos a un hilo para que haga el trabajo
        pthread_t thread_id;
        int err = pthread_create(&thread_id, NULL, worker_thread, data);
        if (err != 0) {
            fprintf(stderr, "Error creando el hilo: %s\n", strerror(err));
            free(data);
            continue;
        }

        // Le decimos al sistema que libere al hilo cuando termine (no le esperamos)
        pthread_detach(thread_id);
    }

    // Tareas de limpieza al salir
    mq_close(server_mq);
    mq_unlink(SERVER_QUEUE_NAME);

    printf("\nServidor apagado correctamente\n");
    return EXIT_SUCCESS;
}