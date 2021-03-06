#include "Compactador.h"

/* hiloCompactacion()
 * Parametros:
 * 	-> nombreTabla ::  char*
 * Descripcion: ejecuta la funcion compactar() cada cierta cantidad de tiempo (tiempoEntreCompactaciones) definido en la metadata de la tabla
 * Return:
 * 	-> :: void* */
void* hiloCompactacion(void* arg) {

	char* pathTabla = (char*)arg;
	char* pathMetadataTabla = string_from_format("%s/Metadata.bin", pathTabla);
	t_config* configMetadataTabla = config_create(pathMetadataTabla);
	int tiempoEntreCompactaciones = config_get_int_value(configMetadataTabla, "COMPACTION_TIME");
	free(pathMetadataTabla);
	config_destroy(configMetadataTabla);
	char* nombreTabla = string_reverse(pathTabla);
	char** aux = string_split(nombreTabla, "/");
	free(nombreTabla);
	nombreTabla = string_reverse(aux[0]);
	liberarArrayDeChar(aux);

	int encontrarTabla(t_hiloTabla* tabla) {
		return string_equals_ignore_case(tabla->nombreTabla, nombreTabla);
	}

	pthread_mutex_lock(&mutexTablasParaCompactaciones);
	t_hiloTabla* hiloTabla = list_find(tablasParaCompactaciones, (void*) encontrarTabla);
	pthread_mutex_unlock(&mutexTablasParaCompactaciones);


	while(1) {
		usleep(tiempoEntreCompactaciones*1000);

		pthread_mutex_lock(&(hiloTabla->mutex));
		if(finalizarHilo(pathTabla)){
			log_info(logger_LFS, "Hilo de compactacion de %s terminado", pathTabla);
			break;
		}
		compactar(pathTabla);
		pthread_mutex_unlock(&(hiloTabla->mutex));
	}
	free(nombreTabla);
	free(pathTabla);
	return NULL;
}

int finalizarHilo(char* pathTabla){
	int finalizarHilo = 0;
	char* nombreTabla = string_reverse(pathTabla);
	char** aux = string_split(nombreTabla, "/");
	free(nombreTabla);
	nombreTabla = string_reverse(aux[0]);
	liberarArrayDeChar(aux);

	int encontrarTabla(t_hiloTabla* tabla) {
		return string_equals_ignore_case(tabla->nombreTabla, nombreTabla);
	}

	void liberarRequest(t_request* request){
		free(request->parametros);
		free(request);
	}

	void liberarRecursos(t_hiloTabla* tabla){
		list_destroy_and_destroy_elements(tabla->cosasABloquear, (void*)liberarMutexTabla);
		free(tabla->nombreTabla);

		free(tabla);
	}

	pthread_mutex_lock(&mutexTablasParaCompactaciones);
	t_hiloTabla* tablaEncontrada = list_find(tablasParaCompactaciones, (void*) encontrarTabla);
	if(tablaEncontrada != NULL && tablaEncontrada->finalizarCompactacion){
		list_remove_and_destroy_by_condition(tablasParaCompactaciones, (void*)encontrarTabla, (void*)liberarRecursos);
		finalizarHilo = 1;
	}
	pthread_mutex_unlock(&mutexTablasParaCompactaciones);

	free(nombreTabla);
	return finalizarHilo;
}

/* compactar()
 * Parametros:
 * 	-> pathTabla ::  char*
 * Descripcion: convierte los tmp a tmpC, los lee, los compara con lo que ya hay en las particiones,
 * y mergea todos los archivos quedandose con los timestamps mas altos en caso de que se repita la key
 * Return:
 * 	-> :: void */
void compactar(char* pathTabla) {

	DIR *tabla;
	struct dirent *archivoDeLaTabla;
	t_list* registrosDeParticiones;
	t_list* particiones = list_create();

	// Leemos el tamanio del bloque
	char* pathMetadata = string_from_format("%sMetadata/Metadata.bin", pathRaiz);
	t_config* configMetadata = config_create(pathMetadata);
	int tamanioBloque = config_get_int_value(configMetadata, "BLOCK_SIZE");
	free(pathMetadata);
	config_destroy(configMetadata);

	// Leemos el numero de particiones de la tabla
	char* pathMetadataTabla = string_from_format("%s/Metadata.bin", pathTabla);
	t_config* configMetadataTabla = config_create(pathMetadataTabla);
	int numeroDeParticiones = config_get_int_value(configMetadataTabla, "PARTITIONS");
	free(pathMetadataTabla);
	config_destroy(configMetadataTabla);

	t_registro* obtenerTimestampMasAltoSiExiste(t_registro* registroDeTmpC) {
		t_registro* registroAEscribir = malloc(sizeof(t_registro));
		int tieneMismaKey(t_registro* registroBuscado) {
			return registroDeTmpC->key == registroBuscado->key;
		}
    	t_registro* registroEncontrado = list_find(registrosDeParticiones, (void*)tieneMismaKey);
    	if(registroEncontrado != NULL) {
    		if(registroEncontrado->timestamp > registroDeTmpC->timestamp) {
    			registroAEscribir->timestamp = registroEncontrado->timestamp;
    			registroAEscribir->key = registroEncontrado->key;
    			registroAEscribir->value = strdup(registroEncontrado->value);
    		} else {
    			registroAEscribir->timestamp = registroDeTmpC->timestamp;
    			registroAEscribir->key = registroDeTmpC->key;
    			registroAEscribir->value = strdup(registroDeTmpC->value);
    		}
    	} else {
    		registroAEscribir->timestamp = registroDeTmpC->timestamp;
    		registroAEscribir->key = registroDeTmpC->key;
    		registroAEscribir->value = strdup(registroDeTmpC->value);
    	}
    	return registroAEscribir;
	}

	// Abrimos el directorio de la tabla para poder recorrerlo
	if((tabla = opendir(pathTabla)) == NULL) {
		perror("opendir");
	}

	char* nombreTabla = string_reverse(pathTabla);
	char** aux = string_split(nombreTabla, "/");
	free(nombreTabla);
	nombreTabla = string_reverse(aux[0]);
	liberarArrayDeChar(aux);

	unsigned long long inicioDeBloqueo;
	unsigned long long finDeBloqueo;
	unsigned long long tiempoBloqueo;

	inicioDeBloqueo = obtenerHoraActual();
	setBlockTo(nombreTabla, 1);
	// Renombramos los tmp a tmpc
	//sleep(1); para ver como funca la tabla bloqueada
	renombrarTmp_a_TmpC(pathTabla, archivoDeLaTabla, tabla);

	setBlockTo(nombreTabla, 0);
	finDeBloqueo = obtenerHoraActual();
	tiempoBloqueo = finDeBloqueo - inicioDeBloqueo;

	// Leemos todos los registros de los temporales a compactar y los guardamos en una lista
	t_list* registrosDeTmpC = leerDeTodosLosTmpC(pathTabla, archivoDeLaTabla, tabla, particiones, numeroDeParticiones, tamanioBloque);

	// Verificamos si hay datos que compactar
	if (registrosDeTmpC->elements_count != 0) {

		// Leemos todos los registros de las particiones y los guardamos en una lista
		registrosDeParticiones = leerDeTodasLasParticiones(pathTabla, particiones, tamanioBloque);

		// Mergeamos la lista de registros de tmpC con la lista de registros de las particiones y obtenemos una nueva lista
		// (filtrando por timestamp mas alto en caso de que hayan keys repetidas)
		t_list* registrosAEscribir = list_map(registrosDeTmpC, (void*) obtenerTimestampMasAltoSiExiste);

		int estaEnTmpC(t_registro* registro){
			int tieneMismaKey(t_registro* registroBuscado) {
				return registro->key == registroBuscado->key;
			}
			return list_find(registrosAEscribir, (void*) tieneMismaKey) != NULL;
		}

		list_remove_and_destroy_by_condition(registrosDeParticiones, (void*)estaEnTmpC, (void*) eliminarRegistro);

		list_add_all(registrosAEscribir, registrosDeParticiones);
		list_destroy(registrosDeParticiones);

		// TODO: Bloquear tabla
		inicioDeBloqueo = obtenerHoraActual();
		setBlockTo(nombreTabla, 1);
		//sleep(20); //sirve para simular una compactacion larga
		//sleep(3); para ver como funca la tabla bloqueada
		// Liberamos los bloques que contienen el archivo “.tmpc” y los que contienen el archivo “.bin”
		liberarBloquesDeTmpCyParticiones(pathTabla, archivoDeLaTabla, tabla, particiones);

		// Grabamos los datos en el nuevo archivo “.bin”
		guardarDatosNuevos(pathTabla, registrosAEscribir, particiones, tamanioBloque, numeroDeParticiones);

		setBlockTo(nombreTabla, 0);
		finDeBloqueo = obtenerHoraActual();

		tiempoBloqueo += finDeBloqueo - inicioDeBloqueo;
		if(tiempoBloqueo > 0){
			log_debug(logger_LFS, "Tabla %s bloqueada durante %llu milisegundos", nombreTabla, tiempoBloqueo);
		}
		// TODO: Desbloquear la tabla y dejar un registro de cuanto tiempo estuvo bloqueada la tabla para realizar esta operatoria
		list_destroy_and_destroy_elements(registrosAEscribir, (void*) eliminarRegistro);
	}
	free(nombreTabla);

	// Cerramos el directorio
	if (closedir(tabla) == -1) {
		perror("closedir");
	}

	list_destroy_and_destroy_elements(registrosDeTmpC, (void*) eliminarRegistro);
	list_destroy_and_destroy_elements(particiones, (void*) eliminarParticion);
}

void setBlockTo(char* nombreTabla, int value){
	int encontrarTabla(t_hiloTabla* tabla) {
		return string_equals_ignore_case(tabla->nombreTabla, nombreTabla);
	}

	void bloquearODesbloquearTodosLosMutex(t_bloqueo* idYMutex) {
		if(value) {
			pthread_mutex_lock(&(idYMutex->mutex));
		} else {
			pthread_mutex_unlock(&(idYMutex->mutex));
		}
	}

	pthread_mutex_lock(&mutexTablasParaCompactaciones);
	t_hiloTabla* tabla = list_find(tablasParaCompactaciones, (void*) encontrarTabla);
	pthread_mutex_unlock(&mutexTablasParaCompactaciones);

	pthread_mutex_lock(&mutexTablasParaCompactaciones);
	for(int i = 0; list_get(tabla->cosasABloquear,i) != NULL; i++) {
		t_bloqueo* idYMutex = list_get(tabla->cosasABloquear,i);
		pthread_mutex_unlock(&mutexTablasParaCompactaciones);
		if(value==1) {
			pthread_mutex_lock(&(idYMutex->mutex));
		} else if(value == 0) {
			pthread_mutex_unlock(&(idYMutex->mutex));
		}
		pthread_mutex_lock(&mutexTablasParaCompactaciones);
	}
	pthread_mutex_unlock(&mutexTablasParaCompactaciones);
}

/* renombrarTmp_a_TmpC()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> archivoDeLaTabla :: struct dirent*
 * 	-> tabla :: DIR*
 * Descripcion: renombro los temporales del dumpeo a temporales a compactar
 * Return:
 * 	-> :: void */
void renombrarTmp_a_TmpC(char* pathTabla, struct dirent* archivoDeLaTabla, DIR* tabla) {
	while ((archivoDeLaTabla = readdir(tabla)) != NULL) {
		if (string_ends_with(archivoDeLaTabla->d_name, ".tmp")) {
			char* pathTmp = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
			char* pathTmpC = string_from_format("%s/%s%c", pathTabla, archivoDeLaTabla->d_name, 'c');
			int resultadoDeRenombrar = rename(pathTmp, pathTmpC);
			if (resultadoDeRenombrar != 0) {
				log_error(logger_LFS, "No se pudo renombrar el temporal");
			}
			free(pathTmp);
			free(pathTmpC);
		}
	}
	rewinddir(tabla);
}

/* leerDeTodosLosTmpC()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> archivoDeLaTabla :: struct dirent*
 * 	-> tabla :: DIR*
 * 	-> particiones :: t_list*
 * 	-> numeroDeParticiones :: int
 * 	-> puntoDeMontaje :: char*
 * Descripcion: leo todos los registros de todos los tmpC y devuelvo una lista con todos los registros
 * Return:
 * 	-> :: t_list* */
t_list* leerDeTodosLosTmpC(char* pathTabla, struct dirent* archivoDeLaTabla, DIR* tabla, t_list* particiones, int numeroDeParticiones, int tamanioBloque) {

	t_registro* tRegistro;
	t_int* particion;
	int particionDeEstaKey;
	t_list* registrosDeTmpC = list_create();

	int existeParticion(t_int* particionAComparar) {
		return particionAComparar->valor == particionDeEstaKey;
	}

	int tieneMismaKey(t_registro* registroBuscado) {
		return tRegistro->key == registroBuscado->key;
	}

	while ((archivoDeLaTabla = readdir(tabla)) != NULL) {

		if (string_ends_with(archivoDeLaTabla->d_name, ".tmpc")) {

			char* pathTmpC = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
			t_config* configTmpC = config_create(pathTmpC);
			char* bloquesString = config_get_string_value(configTmpC, "BLOCKS");
			bloquesString++;
			bloquesString[strlen(bloquesString) -1] = 0;
			char** bloques = string_split(bloquesString, ",");
			int size = config_get_int_value(configTmpC, "SIZE");
			free(pathTmpC);
			config_destroy(configTmpC);

			if(size != 0) {
				// Leo de todos los bloques y guardo en un string (datosDelTmpC)
				int cantidadDeBloques = longitudDeArrayDeStrings(bloques);
				char* datosDelTmpC = strdup("");
				char* datosALeer;
				for (int i = 0; i < cantidadDeBloques; i++) {
					char* pathBloque = string_from_format("%sBloques/%s.bin", pathRaiz, bloques[i]);
					int fd = open(pathBloque, O_RDWR, S_IRWXU);
					free(pathBloque);
					if (fd == -1) {
						perror("Error");
					} else {
						if (i == cantidadDeBloques - 1) {
							int sizeToRead = size % tamanioBloque;
							sizeToRead =  size % tamanioBloque == 0 ? tamanioBloque : size % tamanioBloque;
							datosALeer = mmap(NULL, sizeToRead, PROT_READ, MAP_SHARED, fd, 0);
							if(datosALeer == -1){
								perror("error en mmap");
								log_error(logger_LFS, "error en mmap de compactador");
							}
							string_append_with_format(&datosDelTmpC, "%s",
									datosALeer);
							munmap(datosALeer, sizeToRead);
						} else {
							datosALeer = mmap(NULL, tamanioBloque, PROT_READ,
									MAP_SHARED, fd, 0);
							string_append_with_format(&datosDelTmpC, "%s",
									datosALeer);
							munmap(datosALeer, tamanioBloque);
						}
						close(fd);
					}
				}
				liberarArrayDeChar(bloques);

				char** registros = string_split(datosDelTmpC, "\n");
				free(datosDelTmpC);

				for (int j = 0; registros[j] != NULL; j++) {
					char** registroSeparado = string_split(registros[j], ";");
					tRegistro = (t_registro*) malloc(sizeof(t_registro));
					convertirTimestamp(registroSeparado[0], &(tRegistro->timestamp));
					tRegistro->key = convertirKey(registroSeparado[1]);
					tRegistro->value = strdup(registroSeparado[2]);

					liberarArrayDeChar(registroSeparado);

					particionDeEstaKey = tRegistro->key % numeroDeParticiones;

					particion = list_find(particiones, (void*) existeParticion);
					if (particion == NULL) {
						t_int* particionAAgregar = malloc(sizeof(t_int*));
						particionAAgregar->valor = particionDeEstaKey;
						list_add(particiones, particionAAgregar);
					}

					t_registro* registroEncontrado = list_find(registrosDeTmpC,	(void*) tieneMismaKey);
					if (registroEncontrado != NULL) {
						if (tRegistro->timestamp > registroEncontrado->timestamp) {
							list_remove_and_destroy_by_condition(registrosDeTmpC, (void*) tieneMismaKey, (void*) eliminarRegistro);
							list_add(registrosDeTmpC, tRegistro);
						} else {
							eliminarRegistro(tRegistro);
						}
					} else {
						list_add(registrosDeTmpC, tRegistro);
					}
				}
				liberarArrayDeChar(registros);
			}
		}
	}
	rewinddir(tabla);
	return registrosDeTmpC;
}

/* leerDeTodasLasParticiones()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> particiones :: unit16*
 * 	-> puntoDeMontaje :: char*
 * Descripcion: leo todos los registros de todas las particiones (que esten en el tmpC) y devuelvo una lista con todos los registros
 * Return:
 * 	-> :: t_list* */
t_list* leerDeTodasLasParticiones(char* pathTabla, t_list* particiones, int tamanioBloque) {

	t_int* particion;
	t_registro* tRegistro;
	t_list* registrosDeParticiones = list_create();

	for (int i = 0; list_get(particiones, i) != NULL; i++) {
		particion = list_get(particiones, i);
		char* pathParticion = string_from_format("%s/%d.bin", pathTabla, particion->valor);
		t_config* particionConfig = config_create(pathParticion);
		char* bloquesString = config_get_string_value(particionConfig, "BLOCKS");
		bloquesString++;
		bloquesString[strlen(bloquesString) -1] = 0;
		char** bloques = string_split(bloquesString, ",");
		int size = config_get_int_value(particionConfig, "SIZE");
		free(pathParticion);
		config_destroy(particionConfig);

		if(size != 0) {
			int cantidadDeBloques = longitudDeArrayDeStrings(bloques);
			char* datosDeLasParticiones = strdup("");
			char* datosALeer;
			for (int i = 0; i < cantidadDeBloques; i++) {
				char* pathBloque = string_from_format("%sBloques/%s.bin", pathRaiz, bloques[i]);
				int fd = open(pathBloque, O_RDWR, S_IRWXU);
				free(pathBloque);
				if (fd == -1) {
					perror("Error");
				} else {
					if (i == cantidadDeBloques - 1) {
						int sizeToRead = size % tamanioBloque;
						sizeToRead =  size % tamanioBloque == 0 ? tamanioBloque : size % tamanioBloque;
						datosALeer = mmap(NULL, sizeToRead, PROT_READ, MAP_SHARED, fd, 0);
						if(datosALeer == -1){
							perror("error en mmap");
							log_error(logger_LFS, "error en mmap de select");
						}
						string_append_with_format(&datosDeLasParticiones, "%s", datosALeer);
						munmap(datosALeer, sizeToRead);
					} else {
						datosALeer = mmap(NULL, tamanioBloque, PROT_READ, MAP_SHARED, fd, 0);
						string_append_with_format(&datosDeLasParticiones, "%s", datosALeer);
						munmap(datosALeer, tamanioBloque);
					}
					close(fd);
				}
			}

			char** registros = string_split(datosDeLasParticiones, "\n");
			free(datosDeLasParticiones);

			for (int j = 0; registros[j] != NULL; j++) {
				char** registroSeparado = string_split(registros[j], ";");
				tRegistro = (t_registro*) malloc(sizeof(t_registro));
				convertirTimestamp(registroSeparado[0], &(tRegistro->timestamp));
				tRegistro->key = convertirKey(registroSeparado[1]);
				tRegistro->value = strdup(registroSeparado[2]);
				liberarArrayDeChar(registroSeparado);
				list_add(registrosDeParticiones, tRegistro);
			}
			liberarArrayDeChar(registros);
		}
		liberarArrayDeChar(bloques);
	}
	return registrosDeParticiones;
}

/* liberarBloquesDeTmpCyParticiones()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> archivoDeLaTabla :: unit16*
 * 	-> tabla :: DIR*
 * 	-> particiones :: t_list*
 * 	-> puntoDeMontaje :: char*
 * Descripcion: libero todos los bloques de los tmpC y de las particiones (que esten en el tmpC)
 * Return:
 * 	-> :: void */
void liberarBloquesDeTmpCyParticiones(char* pathTabla, struct dirent* archivoDeLaTabla, DIR* tabla, t_list* particiones) {

	while ((archivoDeLaTabla = readdir(tabla)) != NULL) {
		if (string_ends_with(archivoDeLaTabla->d_name, ".tmpc")) {
			char* pathTmpC = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
			t_config* configTmpC = config_create(pathTmpC);
			char* bloquesString = config_get_string_value(configTmpC, "BLOCKS");
			bloquesString++;
			bloquesString[strlen(bloquesString) -1] = 0;
			char** bloques = string_split(bloquesString, ",");
			config_destroy(configTmpC);
			int i = 0;
			while (bloques[i] != NULL) {
				char* pathBloque = string_from_format("%sBloques/%s.bin", pathRaiz, bloques[i]);
				FILE* bloque = fopen(pathBloque, "w");
				pthread_mutex_lock(&mutexBitmap);
				bitarray_clean_bit(bitarray, (int) strtol(bloques[i], NULL, 10));
				pthread_mutex_unlock(&mutexBitmap);
				free(pathBloque);
				fclose(bloque);
				i++;
			}
			liberarArrayDeChar(bloques);
			remove(pathTmpC);
			free(pathTmpC);
		}

		if (string_ends_with(archivoDeLaTabla->d_name, ".bin") && !string_equals_ignore_case(archivoDeLaTabla->d_name, "Metadata.bin")) {
			char** numeroDeParticionString = string_split(archivoDeLaTabla->d_name, ".");
			int numeroDeParticion = strtol(numeroDeParticionString[0], NULL, 10);
			liberarArrayDeChar(numeroDeParticionString);

			int particionActual(t_int* particion_actual) {
				return particion_actual->valor == numeroDeParticion;
			}

			t_int* particionEncontrada = list_find(particiones, (void*) particionActual);
			if (particionEncontrada != NULL) {
				char* particionPath = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
				t_config* configParticion = config_create(particionPath);
				free(particionPath);
				char* bloquesString = config_get_string_value(configParticion, "BLOCKS");
				bloquesString++;
				bloquesString[strlen(bloquesString) -1] = 0;
				char** bloques = string_split(bloquesString, ",");
				config_destroy(configParticion);
				int i = 0;
				while (bloques[i] != NULL) {
					char* pathBloque = string_from_format("%sBloques/%s.bin", pathRaiz, bloques[i]);
					FILE* bloque = fopen(pathBloque, "w");
					pthread_mutex_lock(&mutexBitmap);
					bitarray_clean_bit(bitarray, (int) strtol(bloques[i], NULL, 10));
					pthread_mutex_unlock(&mutexBitmap);
					free(pathBloque);
					fclose(bloque);
					i++;
				}
				liberarArrayDeChar(bloques);
			}
		}
	}
}

/* guardarDatosNuevos()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> registrosAEscribir :: unit16*
 * 	-> particiones :: t_list*
 * 	-> tamanioBloque :: int
 * 	-> puntoDeMontaje :: char*
 * 	-> numeroDeParticiones :: int
 * Descripcion: pido bloques nuevos para cada particion y guardo los datos nuevos
 * Return:
 * 	-> :: void */
void guardarDatosNuevos(char* pathTabla, t_list* registrosAEscribir, t_list* particiones, int tamanioBloque, int numeroDeParticiones) {

	errorNo error;
	t_int* particion;

	int keyCorrespondeAParticion(t_registro* registro) {
		return particion->valor == registro->key % numeroDeParticiones;
	}

	for (int i = 0; list_get(particiones, i) != NULL; i++) {
		particion = list_get(particiones, i);
		char* pathParticion = string_from_format("%s/%d.bin", pathTabla, particion->valor);
		t_config* configParticion = config_create(pathParticion);
		free(pathParticion);

		t_list* registrosPorParticion = list_filter(registrosAEscribir, (void*) keyCorrespondeAParticion);

		char* datosACompactar = calloc(1, 27 + tamanioValue);
		// 65635 como maximo para el key van a ser 5 bytes y 18.446.744.073.709.551.616 para el timestamp son 20 bytes + 2 punto y coma
		// 5 bytes son 5 char

		for (int j = 0; list_get(registrosPorParticion, j) != NULL; j++) {
			t_registro* registro = list_get(registrosPorParticion, j);
			string_append_with_format(&datosACompactar, "%llu;%u;%s\n", registro->timestamp, registro->key, registro->value);
		}

		list_destroy(registrosPorParticion);

		int cantidadDeBloquesAPedir = strlen(datosACompactar) / tamanioBloque;
		if (strlen(datosACompactar) % tamanioBloque != 0) {
			cantidadDeBloquesAPedir++;
		}
		char* tamanioDatos = string_from_format("%d", strlen(datosACompactar));
		config_set_value(configParticion, "SIZE", tamanioDatos);
		free(tamanioDatos);

		char* bloques = strdup("");
		for (int i = 0; i < cantidadDeBloquesAPedir; i++) {
			int bloqueDeParticion = obtenerBloqueDisponible();
			if (bloqueDeParticion == -1) {
				log_info(logger_LFS, "no hay bloques disponibles");
			} else {
				if (i == cantidadDeBloquesAPedir - 1) {
					string_append_with_format(&bloques, "%d", bloqueDeParticion);
				} else {
					string_append_with_format(&bloques, "%d,", bloqueDeParticion);
				}
				char* pathBloque = string_from_format("%sBloques/%d.bin", pathRaiz, bloqueDeParticion);
				FILE* bloque = fopen(pathBloque, "a+");
				free(pathBloque);
				if (cantidadDeBloquesAPedir != 1 && i < cantidadDeBloquesAPedir - 1) {
					char* registrosAEscribirString = string_substring_until(datosACompactar, tamanioBloque);
					char* stringAuxiliar = string_substring_from(datosACompactar, tamanioBloque);
					free(datosACompactar);
					datosACompactar = stringAuxiliar;
					fprintf(bloque, "%s", registrosAEscribirString);
					free(registrosAEscribirString);
				} else {
					fprintf(bloque, "%s", datosACompactar);
				}
				fclose(bloque);
			}
		}
		char* blocks = string_from_format("[%s]", bloques);
		config_set_value(configParticion, "BLOCKS", blocks);
		free(blocks);
		free(bloques);
		free(datosACompactar);
		config_save(configParticion);
		config_destroy(configParticion);
	}
}

/* eliminarParticion()
 * Parametros:
 * 	-> particionAEliminar ::  t_int*
 * Descripcion: libero la memoria de un t_int* (particion)
 * Return:
 * 	-> :: void */
void eliminarParticion(t_int* particionAEliminar) {
	free(particionAEliminar);
}

/* eliminarRegistro()
 * Parametros:
 * 	-> registroAEliminar ::  t_registro*
 * Descripcion: libero la memoria de un t_registro* (registro)
 * Return:
 * 	-> :: void */
void eliminarRegistro(t_registro* registroAEliminar) {
	free(registroAEliminar->value);
	free(registroAEliminar);
}
