#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "claves.h"

// Definimos las mismas constantes que usa el servidor
#define SERVER_QUEUE_NAME "/mq_servidor_claves"
#define CLIENT_QUEUE_PREFIX "/mq_cliente_claves_"
#define MAX_TEXT 256
#define MAX_VALUE2 32

// Codigos de operacion exactos a los del servidor
enum operation_code {
    OP_DESTROY = 1,
    OP_SET_VALUE = 2,
    OP_GET_VALUE = 3,
    OP_MODIFY_VALUE = 4,
    OP_DELETE_KEY = 5,
    OP_EXIST = 6
};

// Las estructuras son identicas a las del servidor
struct request_msg {
    int op;                     
    pid_t client_pid;           
    char key[MAX_TEXT];         
    char value1[MAX_TEXT];      
    int n_value2;               
    float v_value2[MAX_VALUE2]; 
    struct Paquete value3;      
};

struct response_msg {
    int result;                 
    char value1[MAX_TEXT];      
    int n_value2;               
    float v_value2[MAX_VALUE2]; 
    struct Paquete value3;      
};

// Funcion privada que usan todas las llamadas del proxy
// Encapsula el abrir la cola, enviar, recibir y limpiar
// Devuelve -2 si hay error de colas, o el status del servidor si todo va bien
static int send_and_receive(struct request_msg *req, struct response_msg *res) {
    char client_queue_name[64];
    mqd_t q_server, q_client;
    struct mq_attr attr;

    // Ponemos nuestro PID en la peticion para que el servidor sepa quien somos
    // Utilizamos snprintf para limitar el tamaño y evitar desbordamientos
    req -> client_pid = getpid();
    snprintf(client_queue_name, sizeof(client_queue_name), "%s%ld", CLIENT_QUEUE_PREFIX, (long)req -> client_pid); 

    // Configuramos nuestro buzon temporal
    attr.mq_flags = 0;
    attr.mq_maxmsg = 1;
    attr.mq_msgsize = sizeof(struct response_msg);

    // Creamos nuestra cola privada para leer la respuesta
    q_client = mq_open(client_queue_name, O_CREAT | O_RDONLY, 0660, &attr);
    if (q_client == (mqd_t)-1) {
        return -2;
    }

    // Abrimos el buzon del servidor para escribirle
    q_server = mq_open(SERVER_QUEUE_NAME, O_WRONLY);
    if (q_server == (mqd_t)-1) {
        mq_close(q_client);
        mq_unlink(client_queue_name);
        return -2;
    }

    // Enviamos el paquete
    if (mq_send(q_server, (const char *)req, sizeof(struct request_msg), 0) < 0) {
        mq_close(q_server);
        mq_close(q_client);
        mq_unlink(client_queue_name);
        return -2;
    }

    // Nos bloqueamos esperando que el hilo del servidor nos conteste
    if (mq_receive(q_client, (char *)res, sizeof(struct response_msg), NULL) < 0) {
        mq_close(q_server);
        mq_close(q_client);
        mq_unlink(client_queue_name);
        return -2;
    }

    // Limpieza de colas
    mq_close(q_server);
    mq_close(q_client);
    mq_unlink(client_queue_name);

    // Devolvemos el resultado logico (0 o -1) que mando el servidor
    return res -> result;
}

// Ahora implementamos las funciones de la API usando esa funcion base
int destroy(void) {
    struct request_msg req;
    struct response_msg res;

    // Vaciamos la estructura para evitar mandar basura
    memset(&req, 0, sizeof(req));
    req.op = OP_DESTROY;

    return send_and_receive(&req, &res);
}

// En este caso tenemos el set_value 
int set_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3) {
    struct request_msg req;
    struct response_msg res;

    // Validacion local segun el enunciado
    if (key == NULL || strlen(key) >= MAX_TEXT || 
        value1 == NULL || strlen(value1) >= MAX_TEXT || 
        N_value2 < 1 || N_value2 > MAX_VALUE2) {
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.op = OP_SET_VALUE;
    
    // Rellenamos los datos de la peticion copiando las cadenas y la memoria
    strncpy(req.key, key, MAX_TEXT - 1);
    strncpy(req.value1, value1, MAX_TEXT - 1);
    req.n_value2 = N_value2;
    memcpy(req.v_value2, V_value2, N_value2 * sizeof(float));
    req.value3 = value3;

    return send_and_receive(&req, &res);
}

// Implementacion similar a set_value pero con la logica de rellenar los datos de respuesta
int get_value(char *key, char *value1, int *N_value2, float *V_value2, struct Paquete *value3) {
    struct request_msg req;
    struct response_msg res;
    int status;

    // Validacion del get_value, similar a set_value pero comprobando punteros de salida
    if (key == NULL || strlen(key) >= MAX_TEXT || 
        value1 == NULL || N_value2 == NULL || V_value2 == NULL || value3 == NULL) {
        return -1;
    }

    // Rellenamos la estructura de peticion con la clave, el resto se ignora
    memset(&req, 0, sizeof(req));
    req.op = OP_GET_VALUE;
    strncpy(req.key, key, MAX_TEXT - 1);

    status = send_and_receive(&req, &res);

    // Si el servidor encontro la clave, volcamos los datos en los punteros del cliente
    if (status == 0) {
        strncpy(value1, res.value1, MAX_TEXT - 1);
        // Aseguramos que termine en nulo por si acaso
        value1[MAX_TEXT - 1] = '\0';
        *N_value2 = res.n_value2;
        memcpy(V_value2, res.v_value2, res.n_value2 * sizeof(float));
        *value3 = res.value3;
    }

    return status;
}

// Modificar requiere enviar todos los datos al igual que set_value
int modify_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3) {
    struct request_msg req;
    struct response_msg res;

    // Las mismas validaciones de seguridad que en set_value
    if (key == NULL || strlen(key) >= MAX_TEXT || 
        value1 == NULL || strlen(value1) >= MAX_TEXT || 
        N_value2 < 1 || N_value2 > MAX_VALUE2) {
        return -1;
    }

    // Rellenamos la estructura de peticion igual que en set_value
    memset(&req, 0, sizeof(req));
    req.op = OP_MODIFY_VALUE;
    strncpy(req.key, key, MAX_TEXT - 1);
    strncpy(req.value1, value1, MAX_TEXT - 1);
    req.n_value2 = N_value2;
    memcpy(req.v_value2, V_value2, N_value2 * sizeof(float));
    req.value3 = value3;

    return send_and_receive(&req, &res);
}

// Para borrar solo necesitamos mandar la clave
int delete_key(char *key) {
    struct request_msg req;
    struct response_msg res;

    // Validacion de la clave
    if (key == NULL || strlen(key) >= MAX_TEXT) {
        return -1;
    }
    memset(&req, 0, sizeof(req));
    req.op = OP_DELETE_KEY;
    strncpy(req.key, key, MAX_TEXT - 1);

    return send_and_receive(&req, &res);
}

// Para comprobar existencia tambien basta solo con la clave
int exist(char *key) {
    struct request_msg req;
    struct response_msg res;

    // Validacion de seguridad, no tiene sentido comprobar claves vacias o demasiado largas
    if (key == NULL || strlen(key) >= MAX_TEXT) {
        return -1;
    }
    memset(&req, 0, sizeof(req));
    req.op = OP_EXIST;
    strncpy(req.key, key, MAX_TEXT - 1);

    return send_and_receive(&req, &res);
}