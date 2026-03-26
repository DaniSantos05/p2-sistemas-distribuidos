CC = gcc
THREAD_FLAGS = -pthread
CFLAGS = -Wall -Werror -fPIC -Isrc $(THREAD_FLAGS)
LDFLAGS = -L. -Wl,-rpath,.
LDLIBS = $(THREAD_FLAGS)

# Directorio fuente
SRCDIR = src

# Librería dinámica disponible ahora mismo
LIB_REAL = libclaves.so

# Ejecutables que sí vamos a generar ahora
EXE_MONO = app-cliente_nd
EXE_SERVER = servidor

# Regla principal
all: $(EXE_MONO) $(EXE_SERVER)

# ==================================================
# 1. LIBRERÍA DINÁMICA
# ==================================================

$(LIB_REAL): $(SRCDIR)/claves.o
	$(CC) -shared -o $@ $^

# ==================================================
# 2. OBJETOS
# ==================================================

$(SRCDIR)/claves.o: $(SRCDIR)/claves.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/app-cliente.o: $(SRCDIR)/app-cliente.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/servidor-sock.o: $(SRCDIR)/servidor-sock.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

# ==================================================
# 3. EJECUTABLES
# ==================================================

# Cliente no distribuido
$(EXE_MONO): $(SRCDIR)/app-cliente.o $(LIB_REAL)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-cliente.o -lclaves $(LDLIBS)

# Servidor TCP
$(EXE_SERVER): $(SRCDIR)/servidor-sock.o $(LIB_REAL)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/servidor-sock.o -lclaves $(LDLIBS)

# ==================================================
# 4. LIMPIEZA
# ==================================================

clean:
	rm -f $(SRCDIR)/*.o
	rm -f *.so
	rm -f $(EXE_MONO) $(EXE_SERVER)
	@echo "Limpieza completada: objetos, librerías y ejecutables borrados."

.PHONY: all clean
