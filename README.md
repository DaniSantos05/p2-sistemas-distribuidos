# P2-SISTEMAS-DISTRIBUIDOS (ejercicio_evaluable2)

Práctica de Sistemas Distribuidos que implementa un servicio de almacenamiento de tuplas de la forma `<key, value1, value2, value3>` utilizando **sockets TCP** para la comunicación entre cliente y servidor. El sistema permite insertar, consultar, modificar, eliminar y comprobar la existencia de claves mediante una API en C

La práctica se desarrolla en una versión distribuida mediante **sockets TCP**, con un servidor concurrente y una biblioteca proxy para el cliente (`libproxyclaves.so`)

Cada tupla almacenada contiene:
- `key`: cadena de hasta 255 caracteres
- `value1`: cadena de hasta 255 caracteres
- `value2`: vector de `float` con tamaño entre 1 y 32
- `value3`: estructura con tres valores enteros `(x, y, z)`

Requisitos:
- Sistema operativo Linux
- gcc
- make

Dependencias:
- pthread

Variables de entorno (cliente):
- `IP_TUPLAS`: dirección IP o nombre de la máquina del servidor (formato decimal-punto o dominio-punto)
- `PORT_TUPLAS`: puerto del servidor de tuplas

Instalación:
1. Clonar o descargar el repositorio del proyecto
2. Compilar el proyecto usando el Makefile
   - make
3. Limpiar archivos compilados (opcional)
   - make clean
4. Ejecución:

Versión distribuida (sockets TCP)
- Iniciar el servidor indicando el puerto (en una terminal):
  ./servidor \<PUERTO\>
  Por ejemplo:
  ./servidor 4500
- Definir las variables de entorno e iniciar el cliente (en otra terminal):
  export IP_TUPLAS=localhost
  export PORT_TUPLAS=4500
  ./app-cliente
  O de forma alternativa en una sola línea:
  env IP_TUPLAS=localhost PORT_TUPLAS=4500 ./app-cliente

El cliente utiliza la biblioteca `libproxyclaves.so`, que se encarga de enviar las peticiones al servidor mediante sockets TCP. Se pueden ejecutar varios clientes simultáneamente para probar el funcionamiento concurrente del servidor

Estructura y datos
- Código principal:
- src/
  - `app-cliente.c` — aplicación cliente de prueba del servicio
  - `claves.c` — implementación del servicio de almacenamiento de tuplas
  - `claves.h` — definición de la API del servicio
  - `proxy-sock.c` — implementación del proxy cliente para comunicación mediante sockets TCP
  - `servidor-sock.c` — servidor concurrente que gestiona las peticiones de los clientes
- Bibliotecas generadas:
  - `libproxyclaves.so` — biblioteca cliente para la versión distribuida
- Ejecutables generados:
  - `servidor` — servidor del sistema distribuido
  - `app-cliente` — cliente de prueba del servicio (versión distribuida)
- Otros archivos:
  - `.gitignore` — excluye archivos objeto, bibliotecas dinámicas y ejecutables (se elimina para la entrega)
  - `Makefile` — compilación automática del proyecto
  - `memoria.pdf` — documentación del diseño, protocolo de aplicación y plan de pruebas

Notas
- Las operaciones sobre las tuplas son atómicas
- El protocolo de aplicación entre `proxy-sock.c` y `servidor-sock.c` es independiente del lenguaje de programación: no se envían estructuras de C por el socket, sino mensajes en formato de texto con campos delimitados
- Las funciones devuelven:
  - `0` si la operación se realiza correctamente
  - `-1` si ocurre un error en el servicio de tuplas (clave inexistente, duplicada, etc.)
  - `-2` si ocurre un error en el sistema de comunicaciones
- Se recomienda probar el sistema ejecutando varios clientes simultáneamente para verificar el funcionamiento concurrente del servidor