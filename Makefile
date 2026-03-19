CC = gcc
CFLAGS = -Wall -Werror -fPIC -Isrc
LDFLAGS = -L. -Wl,-rpath,.

# Detectar el sistema operativo para las librerías POSIX
UNAME_S := $(shell uname -s)

# En Linux hacen falta -lrt y -lpthread
ifeq ($(UNAME_S), Linux)
    LDLIBS = -lrt -lpthread
else
    LDLIBS = -lpthread
endif

# Directorios
SRCDIR = src
TESTDIR = tests

# Librerías dinámicas
LIB_REAL  = libclaves.so
LIB_PROXY = libproxyclaves.so

# Ejecutables principales
EXE_MONO   = app-cliente_nd
EXE_SERVER = servidor_mq
EXE_DIST   = app-cliente_dist

# Ejecutables de benchmark
EXE_BENCH_GET_ND   = benchmark-get_nd
EXE_BENCH_GET_DIST = benchmark-get_dist

# Regla principal
all: $(EXE_MONO) $(EXE_SERVER) $(EXE_DIST) $(EXE_BENCH_GET_ND) $(EXE_BENCH_GET_DIST)

# ==================================================
# 1. LIBRERÍAS DINÁMICAS
# ==================================================

# Librería con la lógica local
$(LIB_REAL): $(SRCDIR)/claves.o
	$(CC) -shared -o $@ $^

# Librería con el proxy de colas
$(LIB_PROXY): $(SRCDIR)/proxy-mq.o
	$(CC) -shared -o $@ $^ $(LDLIBS)

# ==================================================
# 2. OBJETOS
# ==================================================

$(SRCDIR)/claves.o: $(SRCDIR)/claves.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/proxy-mq.o: $(SRCDIR)/proxy-mq.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/servidor-mq.o: $(SRCDIR)/servidor-mq.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/app-cliente.o: $(SRCDIR)/app-cliente.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/app-benchmark-get.o: $(SRCDIR)/app-benchmark-get.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

# ==================================================
# 3. EJECUTABLES
# ==================================================

# Cliente no distribuido
$(EXE_MONO): $(SRCDIR)/app-cliente.o $(LIB_REAL)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-cliente.o -lclaves $(LDLIBS)

# Servidor distribuido
$(EXE_SERVER): $(SRCDIR)/servidor-mq.o $(LIB_REAL)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/servidor-mq.o -lclaves $(LDLIBS)

# Cliente distribuido
$(EXE_DIST): $(SRCDIR)/app-cliente.o $(LIB_PROXY)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-cliente.o -lproxyclaves $(LDLIBS)

# Benchmark de get_value no distribuido
$(EXE_BENCH_GET_ND): $(SRCDIR)/app-benchmark-get.o $(LIB_REAL)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-benchmark-get.o -lclaves $(LDLIBS)

# Benchmark de get_value distribuido
$(EXE_BENCH_GET_DIST): $(SRCDIR)/app-benchmark-get.o $(LIB_PROXY)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-benchmark-get.o -lproxyclaves $(LDLIBS)

# ==================================================
# 4. AYUDAS PARA SCRIPTS DE TEST
# ==================================================

# Dar permisos de ejecución a los scripts
scripts:
	chmod +x $(TESTDIR)/prueba_concurrencia.sh
	chmod +x $(TESTDIR)/prueba_estres_get.sh

# Lanzar prueba de concurrencia
# Hay que tener el servidor arrancado en otra terminal
test-concurrencia: scripts
	./$(TESTDIR)/prueba_concurrencia.sh

# Lanzar prueba de estrés de ejemplo
# Hay que tener el servidor arrancado en otra terminal
test-estres: scripts
	./$(TESTDIR)/prueba_estres_get.sh 4 50000 ./$(EXE_BENCH_GET_DIST)

# ==================================================
# 5. LIMPIEZA
# ==================================================

clean:
	rm -f $(SRCDIR)/*.o
	rm -f *.so
	rm -f $(EXE_MONO) $(EXE_SERVER) $(EXE_DIST) $(EXE_BENCH_GET_ND) $(EXE_BENCH_GET_DIST)
	@echo "Limpieza completada: objetos, librerías y ejecutables borrados."

.PHONY: all clean scripts test-concurrencia test-estres
