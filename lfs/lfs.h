#ifndef LFS_H_
#define LFS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commons/log.h>
#include <commons/string.h>
#include <commons/config.h>
#include <readline/readline.h>
#include <nuestro_lib/nuestro_lib.h>
#include <pthread.h>
#include <sys/stat.h>

t_log* logger_LFS;
t_config* config;
t_list* descriptoresClientes;
fd_set descriptoresDeInteres;			// Coleccion de descriptores de interes para select
#define PATH "/home/utnso/tp-2019-1c-bugbusters/lfs"
char* raiz;

pthread_t hiloRecibirDeMemoria;			// hilo que lee de consola

char* mensaje;
int codValidacion;

void recibirConexionMemoria(void);
void interpretarRequest(int, char*, int);
void leerDeConsola(void);
void create(char*, char*, int, int);
int obtenerBloqueDisponible(void);
int mkdir_p(const char*);
void inicializarLfs(void);


#endif /* LFS_H_ */
