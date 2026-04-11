#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "claves.h"

// Función que ejecutará cada "cliente" (proceso hijo) simulado
void comportamiento_cliente(int id_cliente) {
    char key[32];
    char value1[32];
    struct Paquete v3 = {id_cliente, id_cliente * 10, id_cliente * 100};
    float v2[2] = {1.1f * id_cliente, 2.2f * id_cliente};

    // Cada cliente usará una clave única
    sprintf(key, "CLAVE_CONC_%d", id_cliente);
    sprintf(value1, "VALOR_%d", id_cliente);

    printf("[Cliente %d] Iniciando y enviando set_value...\n", id_cliente);

    // 1. Insertamos el valor
    if (set_value(key, value1, 2, v2, v3) == 0) {
        printf("[Cliente %d] set_value insertado OK (%s)\n", id_cliente, key);
    } else {
        printf("[Cliente %d] ERROR en set_value\n", id_cliente);
    }

    // Pequeña pausa para forzar que el servidor atienda a otros a la vez
    usleep(50000); // 50 milisegundos

    // 2. Recuperamos el valor para comprobar que nadie nos lo ha pisado
    char value1_out[256];
    int n_out;
    float v2_out[32];
    struct Paquete v3_out;

    if (get_value(key, value1_out, &n_out, v2_out, &v3_out) == 0) {
        printf("[Cliente %d] get_value recuperado OK -> x=%d, y=%d, z=%d\n", 
               id_cliente, v3_out.x, v3_out.y, v3_out.z);
    } else {
        printf("[Cliente %d] ERROR en get_value\n", id_cliente);
    }

    // Terminamos el proceso hijo
    exit(0);
}

int main(void) {
    int num_clientes_simultaneos = 5;
    pid_t pids[num_clientes_simultaneos];

    printf("=== INICIANDO PRUEBA DE CONCURRENCIA CON %d CLIENTES ===\n", num_clientes_simultaneos);

    // Lanzamos N procesos cliente a la vez
    for (int i = 0; i < num_clientes_simultaneos; i++) {
        pids[i] = fork();
        
        if (pids[i] == 0) {
            // Código que ejecuta el proceso HIJO
            comportamiento_cliente(i + 1);
        } else if (pids[i] < 0) {
            perror("Error al hacer fork");
        }
    }

    // El proceso PADRE espera a que todos los hijos terminen
    for (int i = 0; i < num_clientes_simultaneos; i++) {
        wait(NULL);
    }

    printf("=== PRUEBA DE CONCURRENCIA FINALIZADA ===\n");
    return 0;
}