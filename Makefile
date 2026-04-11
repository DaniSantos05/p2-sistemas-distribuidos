CC = gcc
CFLAGS = -Wall -Werror -fPIC -pthread -Isrc
LDFLAGS = -L. -Wl,-rpath,.
LDLIBS = -pthread

SRCDIR = src

# Librerías
LIB_CLAVES   = libclaves.so
LIB_PROXY    = libproxyclaves.so

# Ejecutables
EXE_SERVER = servidor
EXE_CLIENT = cliente
EXE_CLIENT2 = cliente2

all: $(LIB_CLAVES) $(LIB_PROXY) $(EXE_SERVER) $(EXE_CLIENT) $(EXE_CLIENT2)

# ── Librería del servidor (claves.c) ─────────────────────────────────────────
$(LIB_CLAVES): $(SRCDIR)/claves.o
	$(CC) -shared -o $@ $^

$(SRCDIR)/claves.o: $(SRCDIR)/claves.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Librería del proxy (proxy-sock.c) ────────────────────────────────────────
$(LIB_PROXY): $(SRCDIR)/proxy-sock.o
	$(CC) -shared -o $@ $^

$(SRCDIR)/proxy-sock.o: $(SRCDIR)/proxy-sock.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Servidor TCP ─────────────────────────────────────────────────────────────
$(SRCDIR)/servidor-sock.o: $(SRCDIR)/servidor-sock.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE_SERVER): $(SRCDIR)/servidor-sock.o $(LIB_CLAVES)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/servidor-sock.o -lclaves $(LDLIBS)

# ── Cliente TCP ──────────────────────────────────────────────────────────────
$(SRCDIR)/app-cliente.o: $(SRCDIR)/app-cliente.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE_CLIENT): $(SRCDIR)/app-cliente.o $(LIB_PROXY)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-cliente.o -lproxyclaves $(LDLIBS)

# ── Cliente TCP 2 (Prueba de concurrencia) ──────────────────────────────────
$(SRCDIR)/app-cliente-2.o: $(SRCDIR)/app-cliente-2.c $(SRCDIR)/claves.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE_CLIENT2): $(SRCDIR)/app-cliente-2.o $(LIB_PROXY)
	$(CC) $(LDFLAGS) -o $@ $(SRCDIR)/app-cliente-2.o -lproxyclaves $(LDLIBS)

# ── Limpieza ──────────────────────────────────────────────────────────────────
clean:
	rm -f $(SRCDIR)/*.o *.so $(EXE_SERVER) $(EXE_CLIENT) $(EXE_CLIENT2)
	@echo "Limpieza completada"

.PHONY: all clean
