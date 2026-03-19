#include <stdio.h>
#include <string.h>
#include "claves.h"

int main(void) {
    // Datos que vamos a usar en las pruebas
    struct Paquete v3 = {1, 2, 3};
    float v2[3] = {1.1f, 2.2f, 3.3f};

    struct Paquete v3_mod = {7, 8, 9};
    float v2_mod[2] = {4.4f, 5.5f};

    // Variables donde guardaremos lo que devuelva get_value
    char value1_out[256];
    int n_out;
    float v2_out[32];
    struct Paquete v3_out;

    // Test 0:
    // Limpiamos todo antes de empezar
    printf("--- Test 0: destroy inicial ---\n");
    if (destroy() == 0) {
        printf("OK: todo limpiado\n");
    } else {
        printf("MAL: destroy fallo\n");
    }

    // Test 1:
    // Insertamos una clave nueva
    printf("--- Test 1: Insertar clave 'A' ---\n");
    if (set_value("A", "valor1", 3, v2, v3) == 0) {
        printf("OK\n");
    } else {
        printf("MAL: error al insertar\n");
    }

    // Test 2:
    // Intentamos insertar otra vez la misma clave
    printf("--- Test 2: Intentar duplicar 'A' (debe fallar) ---\n");
    if (set_value("A", "otro", 1, v2, v3) == -1) {
        printf("OK: fallo correctamente\n");
    } else {
        printf("MAL: permitio duplicar\n");
    }

    // Test 3:
    // Probamos insertar con N_value2 fuera de rango
    printf("--- Test 3: Insertar con N_value2 invalido ---\n");
    if (set_value("B", "valorB", 40, v2, v3) == -1) {
        printf("OK: rechazo correcto\n");
    } else {
        printf("MAL: deberia rechazarlo\n");
    }

    // Test 4:
    // Comprobamos si existen o no ciertas claves
    printf("--- Test 4: Verificar existencia ---\n");
    if (exist("A") == 1) {
        printf("OK: A existe\n");
    } else {
        printf("MAL: A deberia existir\n");
    }

    if (exist("B") == 0) {
        printf("OK: B no existe\n");
    } else {
        printf("MAL: B no deberia existir\n");
    }

    // Test 5:
    // Leemos la clave A
    printf("--- Test 5: Leer clave 'A' ---\n");
    if (get_value("A", value1_out, &n_out, v2_out, &v3_out) == 0) {
        printf("OK: get_value funciona\n");
        printf("value1 = %s\n", value1_out);
        printf("N_value2 = %d\n", n_out);
        printf("value3 = (%d, %d, %d)\n", v3_out.x, v3_out.y, v3_out.z);
    } else {
        printf("MAL: get_value fallo\n");
    }

    // Test 6:
    // Intentamos leer una clave que no existe
    printf("--- Test 6: Leer clave inexistente 'Z' ---\n");
    if (get_value("Z", value1_out, &n_out, v2_out, &v3_out) == -1) {
        printf("OK: fallo correctamente\n");
    } else {
        printf("MAL: deberia fallar\n");
    }

    // Test 7:
    // Modificamos la clave A
    printf("--- Test 7: Modificar clave 'A' ---\n");
    if (modify_value("A", "nuevo_valor", 2, v2_mod, v3_mod) == 0) {
        printf("OK: modificada correctamente\n");
    } else {
        printf("MAL: no se pudo modificar\n");
    }

    // Test 8:
    // Volvemos a leer A para comprobar que cambió
    printf("--- Test 8: Leer 'A' despues de modificar ---\n");
    if (get_value("A", value1_out, &n_out, v2_out, &v3_out) == 0) {
        printf("OK: lectura correcta\n");
        printf("value1 = %s\n", value1_out);
        printf("N_value2 = %d\n", n_out);
        printf("value3 = (%d, %d, %d)\n", v3_out.x, v3_out.y, v3_out.z);
    } else {
        printf("MAL: fallo al leer despues de modificar\n");
    }

    // Test 9:
    // Modificar con N_value2 fuera de rango
    printf("--- Test 9: Modificar clave con N_value2 fuera de rango ---\n");
    float v2_invalid[32] = {0};
    struct Paquete v3_test = {0, 0, 0};

    if (modify_value("A", "valor_test", 40, v2_invalid, v3_test) == -1) {
        printf("OK: rechazo correcto por N_value2 fuera de rango\n");
    } else {
        printf("MAL: deberia haber fallado\n");
    }

    // Test 10:
    // Modificar con value1 demasiado largo
    printf("--- Test 10: Modificar clave con value1 demasiado largo ---\n");
    char long_value1[300];
    for (int i = 0; i < 299; i++) {
        long_value1[i] = 'x';
    }
    long_value1[299] = '\0';

    if (modify_value("A", long_value1, 2, v2_invalid, v3_test) == -1) {
        printf("OK: rechazo correcto por value1 demasiado largo\n");
    } else {
        printf("MAL: deberia haber fallado\n");
    }

    // Test 11:
    // Intentamos modificar una clave que no existe
    printf("--- Test 11: Modificar clave inexistente 'Z' ---\n");
    if (modify_value("Z", "x", 2, v2_mod, v3_mod) == -1) {
        printf("OK: fallo correctamente\n");
    } else {
        printf("MAL: no deberia modificar una clave inexistente\n");
    }

    // Test 12:
    // Borramos la clave A
    printf("--- Test 12: Borrar clave 'A' ---\n");
    if (delete_key("A") == 0) {
        printf("OK: borrada correctamente\n");
    } else {
        printf("MAL: no se pudo borrar\n");
    }

    // Test 13:
    // Comprobamos que ya no existe
    printf("--- Test 13: Comprobar que 'A' ya no existe ---\n");
    if (exist("A") == 0) {
        printf("OK: A ya no existe\n");
    } else {
        printf("MAL: A deberia estar borrada\n");
    }

    // Test 14:
    // Intentamos borrarla otra vez
    printf("--- Test 14: Borrar otra vez 'A' ---\n");
    if (delete_key("A") == -1) {
        printf("OK: fallo correctamente\n");
    } else {
        printf("MAL: deberia fallar\n");
    }

    // Test 15:
    // Limpiamos todo al final
    printf("--- Test 15: destroy final ---\n");
    if (destroy() == 0) {
        printf("OK: todo limpiado\n");
    } else {
        printf("MAL: destroy fallo\n");
    }

    return 0;
}
