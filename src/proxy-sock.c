#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include "claves.h"

// Códigos de operación
#define OP_DESTROY      1
#define OP_SET_VALUE    2
#define OP_GET_VALUE    3
#define OP_MODIFY_VALUE 4
#define OP_DELETE_KEY   5
#define OP_EXIST        6

#define MAX_TEXTO 256
#define MAX_VALUE2 32
#define MAX_CUERPO 4096

// Variable global para mantener la conexión
static int sd = -1;

// Funciones auxiliares para enviar/recibir exactamente N bytes
static int enviar_todo(int descriptor, const void *buffer, size_t cantidad_bytes) {
    const unsigned char *puntero = buffer;
    size_t bytes_enviados = 0;

    while (bytes_enviados < cantidad_bytes) {
        ssize_t resultado = send(descriptor, puntero + bytes_enviados, 
                                 cantidad_bytes - bytes_enviados, 0);

        if (resultado < 0) {
            return -1;
        }

        if (resultado == 0) {
            return -1;
        }

        bytes_enviados += (size_t)resultado;
    }

    return 0;
}

static int recibir_todo(int descriptor, void *buffer, size_t cantidad_bytes) {
    unsigned char *puntero = buffer;
    size_t bytes_recibidos = 0;

    while (bytes_recibidos < cantidad_bytes) {
        ssize_t resultado = recv(descriptor, puntero + bytes_recibidos,
                                 cantidad_bytes - bytes_recibidos, 0);

        if (resultado < 0) {
            return -1;
        }

        if (resultado == 0) {
            return -1;
        }

        bytes_recibidos += (size_t)resultado;
    }

    return 0;
}

// Escribir entero en orden de red
static void escribir_entero_32(unsigned char *buffer, size_t *posicion, int32_t valor) {
    uint32_t valor_red = htonl((uint32_t)valor);
    memcpy(buffer + *posicion, &valor_red, sizeof(valor_red));
    *posicion += sizeof(valor_red);
}

// Leer entero desde orden de red
static int32_t leer_entero_32(const unsigned char *buffer, size_t *posicion) {
    uint32_t valor_red = 0;
    memcpy(&valor_red, buffer + *posicion, sizeof(valor_red));
    *posicion += sizeof(valor_red);
    return (int32_t)ntohl(valor_red);
}

// Escribir float en orden de red
static void escribir_float_32(unsigned char *buffer, size_t *posicion, float valor) {
    uint32_t bits = 0;
    memcpy(&bits, &valor, sizeof(bits));
    bits = htonl(bits);
    memcpy(buffer + *posicion, &bits, sizeof(bits));
    *posicion += sizeof(bits);
}

// Leer float desde orden de red
static float leer_float_32(const unsigned char *buffer, size_t *posicion) {
    uint32_t bits = 0;
    memcpy(&bits, buffer + *posicion, sizeof(bits));
    *posicion += sizeof(bits);
    bits = ntohl(bits);
    float valor = 0.0f;
    memcpy(&valor, &bits, sizeof(valor));
    return valor;
}

// Conectar al servidor
static int conectar(void) {
    struct sockaddr_in server_addr;
    struct hostent *hp;
    const char *ip;
    const char *puerto_str;
    int puerto;

    ip = getenv("IP_TUPLAS");
    puerto_str = getenv("PORT_TUPLAS");

    if (ip == NULL || puerto_str == NULL) {
        printf("Error: IP_TUPLAS o PORT_TUPLAS no definidas\n");
        return -1;
    }

    puerto = atoi(puerto_str);
    if (puerto <= 0 || puerto > 65535) {
        printf("Error: puerto invalido\n");
        return -1;
    }

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        printf("Error en socket\n");
        return -1;
    }

    bzero((char *)&server_addr, sizeof(server_addr));
    hp = gethostbyname(ip);
    if (hp == NULL) {
        printf("Error en gethostbyname\n");
        close(sd);
        sd = -1;
        return -1;
    }

    memcpy(&(server_addr.sin_addr), hp->h_addr, hp->h_length);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);

    if (connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        printf("Error en connect\n");
        close(sd);
        sd = -1;
        return -1;
    }

    return 0;
}

// destroy
int destroy(void) {
    unsigned char cabecera[5];
    unsigned char respuesta[9];
    size_t pos = 0;
    int32_t estado;

    if (sd == -1) {
        if (conectar() == -1) {
            return -1;
        }
    }

    // Construir cabecera: [OP_DESTROY] [longitud=0]
    cabecera[0] = OP_DESTROY;
    pos = 1;
    escribir_entero_32(cabecera, &pos, 0);

    // Enviar cabecera
    if (enviar_todo(sd, cabecera, 5) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Recibir respuesta: [OP] [estado] [longitud]
    if (recibir_todo(sd, respuesta, 9) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    pos = 1;
    estado = leer_entero_32(respuesta, &pos);

    return estado;
}

// set_value
int set_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3) {
    unsigned char cuerpo[MAX_CUERPO];
    unsigned char cabecera[5];
    unsigned char respuesta[9];
    size_t pos_cuerpo = 0;
    size_t pos_cab = 0;
    int i;
    int32_t estado;
    int32_t longitud_cuerpo;

    if (sd == -1) {
        if (conectar() == -1) {
            return -1;
        }
    }

    if (key == NULL || strlen(key) >= MAX_TEXTO || 
        value1 == NULL || strlen(value1) >= MAX_TEXTO || 
        V_value2 == NULL || N_value2 < 1 || N_value2 > MAX_VALUE2) {
        return -1;
    }

    // Construir cuerpo
    // key: [longitud] [datos]
    longitud_cuerpo = (int32_t)strlen(key);
    escribir_entero_32(cuerpo, &pos_cuerpo, longitud_cuerpo);
    memcpy(cuerpo + pos_cuerpo, key, strlen(key));
    pos_cuerpo += strlen(key);

    // value1: [longitud] [datos]
    longitud_cuerpo = (int32_t)strlen(value1);
    escribir_entero_32(cuerpo, &pos_cuerpo, longitud_cuerpo);
    memcpy(cuerpo + pos_cuerpo, value1, strlen(value1));
    pos_cuerpo += strlen(value1);

    // N_value2
    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)N_value2);

    // V_value2
    for (i = 0; i < N_value2; i++) {
        escribir_float_32(cuerpo, &pos_cuerpo, V_value2[i]);
    }

    // value3.x, y, z
    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)value3.x);
    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)value3.y);
    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)value3.z);

    // Construir cabecera
    cabecera[0] = OP_SET_VALUE;
    pos_cab = 1;
    escribir_entero_32(cabecera, &pos_cab, (int32_t)pos_cuerpo);

    // Enviar cabecera
    if (enviar_todo(sd, cabecera, 5) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Enviar cuerpo
    if (enviar_todo(sd, cuerpo, pos_cuerpo) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Recibir respuesta
    if (recibir_todo(sd, respuesta, 9) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    pos_cab = 1;
    estado = leer_entero_32(respuesta, &pos_cab);

    return estado;
}

// get_value
int get_value(char *key, char *value1, int *N_value2, float *V_value2, struct Paquete *value3) {
    unsigned char cuerpo_peticion[MAX_TEXTO];
    unsigned char cabecera[5];
    unsigned char cabecera_respuesta[9];
    unsigned char cuerpo_respuesta[MAX_CUERPO];
    size_t pos = 0;
    int32_t estado;
    int32_t longitud_cuerpo;
    int i;

    if (sd == -1) {
        if (conectar() == -1) {
            return -1;
        }
    }

    if (key == NULL || strlen(key) >= MAX_TEXTO || value1 == NULL || N_value2 == NULL || V_value2 == NULL || value3 == NULL) {
        return -1;
    }

    // Construir cuerpo: [longitud_key] [key]
    pos = 0;
    longitud_cuerpo = (int32_t)strlen(key);
    escribir_entero_32(cuerpo_peticion, &pos, longitud_cuerpo);
    memcpy(cuerpo_peticion + pos, key, strlen(key));
    pos += strlen(key);

    // Construir cabecera
    cabecera[0] = OP_GET_VALUE;
    size_t pos_cab = 1;
    escribir_entero_32(cabecera, &pos_cab, (int32_t)pos);

    // Enviar cabecera
    if (enviar_todo(sd, cabecera, 5) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Enviar cuerpo
    if (enviar_todo(sd, cuerpo_peticion, pos) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Recibir cabecera de respuesta
    if (recibir_todo(sd, cabecera_respuesta, 9) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    pos_cab = 1;
    estado = leer_entero_32(cabecera_respuesta, &pos_cab);

    if (estado != 0) {
        return estado;
    }

    longitud_cuerpo = leer_entero_32(cabecera_respuesta, &pos_cab);

    // Recibir cuerpo de respuesta
    if (longitud_cuerpo > 0) {
        if (recibir_todo(sd, cuerpo_respuesta, (size_t)longitud_cuerpo) == -1) {
            close(sd);
            sd = -1;
            return -1;
        }
    }

    // Parsear cuerpo: value1
    pos = 0;
    int32_t len_value1 = leer_entero_32(cuerpo_respuesta, &pos);
    if (len_value1 < 0 || len_value1 >= MAX_TEXTO) {
        return -1;
    }
    memcpy(value1, cuerpo_respuesta + pos, (size_t)len_value1);
    value1[len_value1] = '\0';
    pos += len_value1;

    // Parsear: N_value2
    *N_value2 = leer_entero_32(cuerpo_respuesta, &pos);
    if (*N_value2 < 1 || *N_value2 > MAX_VALUE2) {
        return -1;
    }

    // Parsear: V_value2
    for (i = 0; i < *N_value2; i++) {
        V_value2[i] = leer_float_32(cuerpo_respuesta, &pos);
    }

    // Parsear: value3
    value3->x = leer_entero_32(cuerpo_respuesta, &pos);
    value3->y = leer_entero_32(cuerpo_respuesta, &pos);
    value3->z = leer_entero_32(cuerpo_respuesta, &pos);

    return 0;
}

// modify_value
int modify_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3) {
    unsigned char cuerpo[MAX_CUERPO];
    unsigned char cabecera[5];
    unsigned char respuesta[9];
    size_t pos_cuerpo = 0;
    size_t pos_cab = 0;
    int i;
    int32_t estado;
    int32_t longitud_cuerpo;

    if (sd == -1) {
        if (conectar() == -1) {
            return -1;
        }
    }

    if (key == NULL || strlen(key) >= MAX_TEXTO || 
        value1 == NULL || strlen(value1) >= MAX_TEXTO || 
        V_value2 == NULL || N_value2 < 1 || N_value2 > MAX_VALUE2) {
        return -1;
    }

    // Construir cuerpo (igual que set_value)
    longitud_cuerpo = (int32_t)strlen(key);
    escribir_entero_32(cuerpo, &pos_cuerpo, longitud_cuerpo);
    memcpy(cuerpo + pos_cuerpo, key, strlen(key));
    pos_cuerpo += strlen(key);

    longitud_cuerpo = (int32_t)strlen(value1);
    escribir_entero_32(cuerpo, &pos_cuerpo, longitud_cuerpo);
    memcpy(cuerpo + pos_cuerpo, value1, strlen(value1));
    pos_cuerpo += strlen(value1);

    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)N_value2);

    for (i = 0; i < N_value2; i++) {
        escribir_float_32(cuerpo, &pos_cuerpo, V_value2[i]);
    }

    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)value3.x);
    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)value3.y);
    escribir_entero_32(cuerpo, &pos_cuerpo, (int32_t)value3.z);

    // Construir cabecera
    cabecera[0] = OP_MODIFY_VALUE;
    pos_cab = 1;
    escribir_entero_32(cabecera, &pos_cab, (int32_t)pos_cuerpo);

    // Enviar cabecera
    if (enviar_todo(sd, cabecera, 5) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Enviar cuerpo
    if (enviar_todo(sd, cuerpo, pos_cuerpo) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Recibir respuesta
    if (recibir_todo(sd, respuesta, 9) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    pos_cab = 1;
    estado = leer_entero_32(respuesta, &pos_cab);

    return estado;
}

// delete_key
int delete_key(char *key) {
    unsigned char cuerpo[MAX_TEXTO];
    unsigned char cabecera[5];
    unsigned char respuesta[9];
    size_t pos = 0;
    int32_t estado;
    int32_t longitud_cuerpo;

    if (sd == -1) {
        if (conectar() == -1) {
            return -1;
        }
    }

    if (key == NULL || strlen(key) >= MAX_TEXTO) {
        return -1;
    }

    // Construir cuerpo
    longitud_cuerpo = (int32_t)strlen(key);
    escribir_entero_32(cuerpo, &pos, longitud_cuerpo);
    memcpy(cuerpo + pos, key, strlen(key));
    pos += strlen(key);

    // Construir cabecera
    cabecera[0] = OP_DELETE_KEY;
    size_t pos_cab = 1;
    escribir_entero_32(cabecera, &pos_cab, (int32_t)pos);

    // Enviar cabecera
    if (enviar_todo(sd, cabecera, 5) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Enviar cuerpo
    if (enviar_todo(sd, cuerpo, pos) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Recibir respuesta
    if (recibir_todo(sd, respuesta, 9) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    pos_cab = 1;
    estado = leer_entero_32(respuesta, &pos_cab);

    return estado;
}

// exist
int exist(char *key) {
    unsigned char cuerpo[MAX_TEXTO];
    unsigned char cabecera[5];
    unsigned char respuesta[9];
    size_t pos = 0;
    int32_t estado;
    int32_t longitud_cuerpo;

    if (sd == -1) {
        if (conectar() == -1) {
            return -1;
        }
    }

    if (key == NULL || strlen(key) >= MAX_TEXTO) {
        return -1;
    }

    // Construir cuerpo
    longitud_cuerpo = (int32_t)strlen(key);
    escribir_entero_32(cuerpo, &pos, longitud_cuerpo);
    memcpy(cuerpo + pos, key, strlen(key));
    pos += strlen(key);

    // Construir cabecera
    cabecera[0] = OP_EXIST;
    size_t pos_cab = 1;
    escribir_entero_32(cabecera, &pos_cab, (int32_t)pos);

    // Enviar cabecera
    if (enviar_todo(sd, cabecera, 5) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Enviar cuerpo
    if (enviar_todo(sd, cuerpo, pos) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    // Recibir respuesta
    if (recibir_todo(sd, respuesta, 9) == -1) {
        close(sd);
        sd = -1;
        return -1;
    }

    pos_cab = 1;
    estado = leer_entero_32(respuesta, &pos_cab);

    return estado;
}