#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "claves.h"

// Número máximo de elementos permitidos para value2
// según el enunciado (N entre 1 y 32)
#define MAX_VALUE2 32

// Nodo de la lista enlazada donde guardamos cada tupla:
// <key, value1, value2, value3>
struct Nodo {
    // Clave de la tupla (máximo 255 caracteres + '\0')
    char key[256];

    // Primer valor asociado (máximo 255 caracteres + '\0')
    char value1[256];

    // Número real de elementos usados en V_value2
    int N_value2;

    // Vector de floats (capacidad máxima 32)
    float V_value2[MAX_VALUE2];

    // Tercer valor asociado (estructura con x, y, z)
    struct Paquete value3;

    // Puntero al siguiente nodo de la lista
    struct Nodo *siguiente;
};

// Función auxiliar para cargar value2 en un nodo
// Devuelve true si copia correctamente y false si hay error
int set_value2(struct Nodo *nodo, float *nuevos_valores, int cantidad) {
    // 1) Validamos que la cantidad esté en el rango permitido
    if (cantidad < 1 || cantidad > MAX_VALUE2) {
        // Mensaje informativo para depuración
        printf("Error: La cantidad de elementos (%d) debe estar entre 1 y 32.\n", cantidad);
        return -1;
    }

    // 2) Guardamos el tamaño real del vector
    nodo -> N_value2 = cantidad;

    // 3) Copiamos elemento a elemento al vector del nodo
    for (int i = 0; i < cantidad; i++) {
        nodo -> V_value2[i] = nuevos_valores[i];
    }

    // Si todo ha ido bien, devolvemos éxito
    return 0;
}


// Inicio de la lista enlazada en memoria
struct Nodo *cabeza = NULL;

// Mutex global para garantizar atomicidad en todas las operaciones
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Comprueba que una cadena:
// 1. No sea NULL
// 2. No mida más de 255 caracteres
// Si no cumple, la consideramos inválida
static int is_valid_string(const char *value) {
    // "i" sirve para recorrer la cadena carácter a carácter
    size_t i;

    // Si el puntero es NULL, no hay cadena válida
    if (value == NULL) {
        return 0;
    }

    // Recorremos como máximo 256 posiciones
    for (i = 0; i < 256; i++) {
        // Si encontramos fin de cadena, está bien formada
        if (value[i] == '\0') {
            return 1;
        }
    }

    // Si no apareció '\0' en 256, la cadena no es válida
    return 0;
}

// Función auxiliar privada para buscar un nodo por su clave
// Si la encuentra, devuelve puntero al nodo
// Si no la encuentra, devuelve NULL
struct Nodo* buscar_nodo(char *key) {
    // Empezamos a buscar desde el primer nodo
    struct Nodo *actual = cabeza;

    // Recorremos la lista completa
    while (actual != NULL) {
        // Si la clave coincide, devolvemos ese nodo
        if (strcmp(actual -> key, key) == 0) {  // strcmp devuelve 0 si dos textos son iguales 
            return actual;
        }
        // Pasamos al siguiente nodo
        actual = actual -> siguiente;
    }

    // Si no se encontró la clave, devolvemos NULL
    return NULL;
}

// Elimina todas las tuplas almacenadas en memoria
// Equivale a "reiniciar" el servicio local
int destroy(void) {
    // Cerramos el mutex para que nadie más toque la lista a la vez
    pthread_mutex_lock(&mutex);

    // "actual" apunta al nodo que estamos revisando
    struct Nodo *actual = cabeza;
    // Borramos nodo por nodo hasta vaciar la lista
    while (actual != NULL) {
        // Guardamos el siguiente antes de liberar el actual
        struct Nodo *siguiente = actual -> siguiente;
        // Liberamos memoria del nodo actual
        free(actual);
        // Avanzamos al siguiente nodo
        actual = siguiente;
    }
    // La lista queda vacía
    cabeza = NULL;

    // Abrimos el mutex
    pthread_mutex_unlock(&mutex);
    // 0 significa "todo correcto"
    return 0;
}

// Inserta una nueva tupla <key, value1, value2, value3>
// Reglas de validación de entrada:
// - key debe ser válida
// - value1 debe ser válida
// - N_value2 debe estar entre 1 y 32
// - no puede existir ya una tupla con la misma key
int set_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3) {
    // Validamos que los datos de entrada sean correcto
    if (!is_valid_string(key) || !is_valid_string(value1) || N_value2 < 1 || N_value2 > MAX_VALUE2 || V_value2 == NULL) {
        return -1;
    }

    // Cerramos el mutex para hacer la operación atómica
    pthread_mutex_lock(&mutex);

    // Si ya existe esa clave, no se puede insertar otra vez
    if (buscar_nodo(key) != NULL) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    // Reservamos memoria para un nuevo nodo
    struct Nodo *nuevo = malloc(sizeof(struct Nodo));
    // Si falla la reserva, devolvemos error
    if (nuevo == NULL) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    // Copiamos la clave al nodo nuevo
    strcpy(nuevo -> key, key);
    // Copiamos value1 al nodo nuevo
    strcpy(nuevo -> value1, value1);
    // Guardamos el vector value2 y su tamaño N
    if (set_value2(nuevo, V_value2, N_value2)) {
        // Si falla, limpiamos memoria y salimos con error
        free(nuevo);
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    // Guardamos la estructura value3
    nuevo -> value3 = value3;

    // Insertamos el nodo al principio de la lista
    nuevo -> siguiente = cabeza;
    cabeza = nuevo;

    // Abrimos el mutex
    pthread_mutex_unlock(&mutex);
    // 0 significa inserción correcta
    return 0;
}

// Obtiene los valores de una clave existente
// Escribe el resultado en las variables de salida:
// - value1
// - *N_value2
// - V_value2
// - *value3
int get_value(char *key, char *value1, int *N_value2, float *V_value2, struct Paquete *value3) {
    // Validamos los parámetros de salida y la clave de entrada
    if (!is_valid_string(key) || value1 == NULL || N_value2 == NULL || V_value2 == NULL || value3 == NULL) {
        return -1;
    }

    // Cerramos el candado para leer de forma segura
    pthread_mutex_lock(&mutex);

    // Buscamos la clave
    struct Nodo *nodo = buscar_nodo(key);
    // Si no existe, devolvemos error
    if (nodo == NULL) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    // Copiamos value1 al buffer de salida
    strcpy(value1, nodo -> value1);
    // Devolvemos el tamaño del vector value2
    *N_value2 = nodo -> N_value2;
    // Copiamos el vector de floats a la salida
    memcpy(V_value2, nodo -> V_value2, (size_t)nodo -> N_value2 * sizeof(float));
    // Copiamos la estructura value3 a la salida
    *value3 = nodo -> value3;

    // Abrimos el mutex
    pthread_mutex_unlock(&mutex);
    // 0 significa lectura correcta
    return 0;
}

// Modifica una tupla ya existente identificada por key
// Reemplaza por completo value1, value2 y value3
// Si la clave no existe, devuelve error
int modify_value(char *key, char *value1, int N_value2, float *V_value2, struct Paquete value3) {
    // Validamos datos antes de modificar
    if (!is_valid_string(key) || !is_valid_string(value1) || N_value2 < 1 || N_value2 > MAX_VALUE2 || V_value2 == NULL) {
        return -1;
    }

    // Cerramos el mutex para modificar de forma atómica
    pthread_mutex_lock(&mutex);

    // Buscamos el nodo de la clave indicada
    struct Nodo *nodo = buscar_nodo(key);
    // Si no existe la clave, no se puede modificar
    if (nodo == NULL) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    // Sustituimos value1
    strcpy(nodo -> value1, value1);
    // Sustituimos value2 y su tamaño
    if (set_value2(nodo, V_value2, N_value2)) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    // Sustituimos value3
    nodo -> value3 = value3;

    // Abrimos el mutex
    pthread_mutex_unlock(&mutex);
    // 0 significa modificación correcta
    return 0;
}

// Elimina del almacén la tupla asociada a key
// Si no existe, devuelve -1
int delete_key(char *key) {
    // Validamos la clave
    if (!is_valid_string(key)) {
        return -1;
    }

    // Cerramos el mutex para borrar de forma segura
    pthread_mutex_lock(&mutex);

    // "actual" recorre la lista; "anterior" guarda el nodo previo
    struct Nodo *actual = cabeza;
    struct Nodo *anterior = NULL;

    // Recorremos todos los nodos hasta encontrar la clave
    while (actual != NULL) {
        // Si encontramos la clave objetivo
        if (strcmp(actual -> key, key) == 0) {
            // Si está en la cabeza, movemos la cabeza al siguiente nodo
            if (anterior == NULL) {
                cabeza = actual -> siguiente;
            } else {
                // Si no, enlazamos el anterior con el siguiente
                anterior -> siguiente = actual -> siguiente;
            }

            // Liberamos memoria del nodo borrado
            free(actual);
            // Abrimos el mutex y devolvemos éxito
            pthread_mutex_unlock(&mutex);
            return 0;
        }

        // Avanzamos en la lista
        anterior = actual;
        actual = actual -> siguiente;
    }

    // Si no encontramos la clave, devolvemos error
    pthread_mutex_unlock(&mutex);
    return -1;
}

// Comprueba si existe una clave en el almacén
// Devuelve:
// - 1 si existe
// - 0 si no existe
// - -1 si la clave de entrada es inválida
int exist(char *key) {
    // Validamos la clave que recibimos
    if (!is_valid_string(key)) {
        return -1;
    }

    // Cerramos el mutex para consultar de forma segura
    pthread_mutex_lock(&mutex);
    // Resultado: 1 si existe, 0 si no existe.
    int result = (buscar_nodo(key) != NULL) ? 1 : 0;
    // Abrimos el mutex
    pthread_mutex_unlock(&mutex);

    // Devolvemos el resultado de la consulta
    return result;
}