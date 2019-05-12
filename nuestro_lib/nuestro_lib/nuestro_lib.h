//servidor
#ifndef SOCKETS_H_
#define SOCKETS_H_

#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<signal.h>
#include<unistd.h>
#include<netdb.h>
#include<commons/log.h>
#include<commons/string.h>
#include<commons/config.h>
#include<commons/collections/list.h>
#include<string.h>
#include<readline/readline.h>

#define TRUE 0
#define FALSE 1

#define PARAMETROS_SELECT 2
#define PARAMETROS_INSERT 3
#define PARAMETROS_CREATE 4
#define PARAMETROS_DESCRIBE 1
#define PARAMETROS_DROP 1
#define PARAMETROS_JOURNAL 0
#define PARAMETROS_ADD 4
#define PARAMETROS_RUN 1
#define PARAMETROS_METRICS 0

//#define NUESTRO_ERROR -1

typedef enum
{
	KERNEL,
	MEMORIA,
	LFS
} Componente;


typedef enum
{
	SELECT,
	INSERT,
	CREATE,
	DESCRIBE,
	DROP,
	JOURNAL,
	ADD,
	RUN,
	METRICS,
	NUESTRO_ERROR = -1
} cod_request;

/*typedef struct
{
	int size;
	void* stream;
} t_buffer;
*/
typedef struct
{
	cod_request palabraReservada;
	int tamanio;
	void* request;
} t_paquete;

void iterator(char*);
char** separarString(char*);
char** obtenerParametros(char*);
int longitudDeArrayDeStrings(char**);



int crearConexion(char*, char*);
t_config* leer_config(char*);
//void leer_consola(t_log* logger);
//void _leer_consola_haciendo(void(*accion)(char*));
int validarMensaje(char*, Componente, t_log*);
int cantDeParametrosEsCorrecta(int,int);
int validarPalabraReservada(int,Componente, t_log*);
int validadCantDeParametros(int, int, t_log*);

int obtenerCodigoPalabraReservada(char*, Componente);

////servidor
void* recibir_buffer(int*, int);
int iniciar_servidor(char*, char*);
int esperar_cliente(int);
t_paquete* recibir(int);
//void recibir_mensaje(int);
//int recibir_operacion(int);
//
////cliente
//void enviar_mensaje(char* mensaje, int socket_cliente);
//t_paquete* crear_paquete(void);
//t_paquete* crear_super_paquete(void);
//void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio);
void* serializar_paquete(t_paquete* , int);
void enviar(cod_request, char*, int);
void eliminar_paquete(t_paquete*);
void liberar_conexion(int);

/* Multiplexacion */
void eliminarClientesCerrados(t_list*, int*);
int maximo(t_list*, int, int);

#endif /* SOCKETS_H_ */
