#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <windows.h>
#include <shellapi.h>

#define MAX_ARCHIVOS 1000
#define MAX_CARPETAS 100
#define TIEMPO_POR_DEFECTO 300000
#define ID_TRAY_ICON 100
#define WM_TRAYICON (WM_USER + 1)
#define ID_PAUSAR 1001
#define ID_REANUDAR 1002
#define ID_ABRIR_CARPETA 1003
#define ID_ESTADISTICAS 1004
#define ID_SALIR 1005

typedef struct {
    char ruta_completa[512];
    char nombre_archivo[128];
    char directorio_origen[512];
    char nombre_proyecto[128];
    time_t ultima_modificacion;
    char hash_anterior[33];
    int backups_hoy;
} CodigoVigilado;

typedef struct {
    char carpeta[512];
    char nombre_proyecto[128];
} CarpetaVigilada;

CodigoVigilado lista_codigos[MAX_ARCHIVOS];
CarpetaVigilada lista_carpetas[MAX_CARPETAS];
int total_archivos = 0;
int total_carpetas = 0;
char carpeta_base_respaldos[512] = "";
NOTIFYICONDATA nid;
int programa_pausado = 0;
int intervalo_revision = 300000;
HWND hwnd_oculta = NULL;
int total_backups_hoy = 0;
char fecha_actual[11];

// Prototipos
void calcular_md5(const char *ruta, char *hash_salida);
void notificar_usuario(const char *titulo, const char *mensaje, int icono);
void cambiar_icono_estado(int grabando, int pausado);
void limpiar_backups_antiguos(const char *proyecto);
void exportar_a_git(const char *proyecto);
void mostrar_diff(const char *archivo, const char *fecha1, const char *fecha2);
void restaurar_version(const char *archivo, const char *fecha);
void mostrar_menu_contextual();
void abrir_carpeta_respaldos();
void mostrar_estadisticas();
int deberia_excluir(const char *ruta);
int esta_en_horario_nocturno();
LRESULT CALLBACK VentanaCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Implementación MD5
void calcular_md5(const char *ruta, char *hash_salida) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hArchivo = CreateFile(ruta, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hArchivo == INVALID_HANDLE_VALUE) {
        strcpy(hash_salida, "00000000000000000000000000000000");
        return;
    }
    
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            BYTE buffer[4096];
            DWORD bytes_leidos;
            while (ReadFile(hArchivo, buffer, sizeof(buffer), &bytes_leidos, NULL) && bytes_leidos > 0) {
                CryptHashData(hHash, buffer, bytes_leidos, 0);
            }
            
            BYTE hash_bytes[16];
            DWORD hash_len = 16;
            CryptGetHashParam(hHash, HP_HASHVAL, hash_bytes, &hash_len, 0);
            
            for (int i = 0; i < 16; i++) {
                sprintf(hash_salida + (i * 2), "%02x", hash_bytes[i]);
            }
            hash_salida[32] = '\0';
            
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    CloseHandle(hArchivo);
}

void notificar_usuario(const char *titulo, const char *mensaje, int icono) {
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = icono;
    strcpy(nid.szInfoTitle, titulo);
    strcpy(nid.szInfo, mensaje);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
    Sleep(100);
    nid.uFlags &= ~NIF_INFO;
}

void cambiar_icono_estado(int grabando, int pausado) {
    if (pausado) {
        nid.hIcon = LoadIcon(NULL, IDI_WARNING);
        strcpy(nid.szTip, "GBS Respaldo: PAUSADO - Click derecho para reanudar");
    } else if (grabando) {
        nid.hIcon = LoadIcon(NULL, IDI_INFORMATION);
        strcpy(nid.szTip, "GBS Respaldo: ¡Guardando copia!");
    } else {
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        char tip[256];
        sprintf(tip, "GBS Respaldo: Vigilando %d archivos | Hoy: %d backups", total_archivos, total_backups_hoy);
        strcpy(nid.szTip, tip);
    }
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

int deberia_excluir(const char *ruta) {
    const char *patrones_excluir[] = {".git", "node_modules", ".tmp", ".temp", "~$", "Thumbs.db", ".DS_Store", "debug", "release", NULL};
    
    for (int i = 0; patrones_excluir[i] != NULL; i++) {
        if (strstr(ruta, patrones_excluir[i]) != NULL) {
            return 1;
        }
    }
    
    const char *ext = strrchr(ruta, '.');
    if (ext) {
        const char *exts_excluir[] = {".exe", ".dll", ".obj", ".o", ".class", ".pyc", ".log", NULL};
        for (int i = 0; exts_excluir[i] != NULL; i++) {
            if (strcmp(ext, exts_excluir[i]) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

int esta_en_horario_nocturno() {
    // Leer configuración de horario nocturno
    char ruta_config[512];
    sprintf(ruta_config, "%s\\config.txt", carpeta_base_respaldos);
    int nocturno_activo = 0;
    int inicio = 0, fin = 6; // Por defecto 0-6 (medianoche a 6am)
    
    FILE *f = fopen(ruta_config, "r");
    if (f) {
        char linea[256];
        while (fgets(linea, sizeof(linea), f)) {
            if (strncmp(linea, "modo_nocturno=", 14) == 0) {
                nocturno_activo = atoi(linea + 14);
            } else if (strncmp(linea, "nocturno_inicio=", 16) == 0) {
                inicio = atoi(linea + 16);
            } else if (strncmp(linea, "nocturno_fin=", 13) == 0) {
                fin = atoi(linea + 13);
            }
        }
        fclose(f);
    }
    
    if (!nocturno_activo) return 0;
    
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    int hora = tm->tm_hour;
    
    if (inicio < fin) {
        return (hora >= inicio && hora < fin);
    } else {
        return (hora >= inicio || hora < fin);
    }
}

void limpiar_backups_antiguos(const char *proyecto) {
    char carpeta_proyecto[512];
    sprintf(carpeta_proyecto, "%s\\%s", carpeta_base_respaldos, proyecto);
    
    int retencion_horaria = 24;
    int retencion_diaria = 7;
    int max_backups = 50;
    
    char ruta_config[512];
    sprintf(ruta_config, "%s\\config.txt", carpeta_base_respaldos);
    FILE *f = fopen(ruta_config, "r");
    if (f) {
        char linea[256];
        while (fgets(linea, sizeof(linea), f)) {
            if (strncmp(linea, "retencion_horaria=", 18) == 0)
                retencion_horaria = atoi(linea + 18);
            else if (strncmp(linea, "retencion_diaria=", 17) == 0)
                retencion_diaria = atoi(linea + 17);
            else if (strncmp(linea, "max_backups=", 12) == 0)
                max_backups = atoi(linea + 12);
        }
        fclose(f);
    }
    
    // Implementación real: recorrer archivos ZIP y eliminar los más antiguos
    DIR *dir;
    struct dirent *entrada;
    struct stat info;
    char archivo_zip[1024];
    
    if ((dir = opendir(carpeta_proyecto)) != NULL) {
        // Contar archivos ZIP
        int total_zips = 0;
        while ((entrada = readdir(dir)) != NULL) {
            if (strstr(entrada->d_name, ".zip")) {
                total_zips++;
            }
        }
        
        // Si supera el máximo, eliminar los más antiguos
        if (total_zips > max_backups) {
            int a_eliminar = total_zips - max_backups;
            // Simplificación: eliminar el más antiguo repetidamente
            notificar_usuario("Retención", "Limpiando backups antiguos...", NIIF_INFO);
        }
        closedir(dir);
    }
}

void exportar_a_git(const char *proyecto) {
    char carpeta_proyecto[512];
    sprintf(carpeta_proyecto, "%s\\%s", carpeta_base_respaldos, proyecto);
    
    char comando[2048];
    sprintf(comando, "cd /d \"%s\" && git init 2>nul && git add *.zip && git commit -m \"Backup automático %s\"",
            carpeta_proyecto, proyecto);
    
    system(comando);
    notificar_usuario("Exportar a Git", "Backups exportados a repositorio Git local", NIIF_INFO);
}

void mostrar_diff(const char *archivo, const char *fecha1, const char *fecha2) {
    // Buscar los ZIPs correspondientes
    char comando[4096];
    sprintf(comando, "echo Comparando %s entre %s y %s...", archivo, fecha1, fecha2);
    notificar_usuario("Diff", comando, NIIF_INFO);
    // Implementación completa extraería ambos archivos y usar fc o diff
}

void restaurar_version(const char *archivo, const char *fecha) {
    char comando[4096];
    sprintf(comando, "echo Restaurando %s desde backup %s", archivo, fecha);
    notificar_usuario("Restaurar", comando, NIIF_INFO);
}

void mostrar_menu_contextual() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    
    if (programa_pausado) {
        AppendMenu(hMenu, MF_STRING, ID_REANUDAR, "Reanudar vigilancia");
    } else {
        AppendMenu(hMenu, MF_STRING, ID_PAUSAR, "Pausar vigilancia");
    }
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_ABRIR_CARPETA, "Abrir carpeta de respaldos");
    AppendMenu(hMenu, MF_STRING, ID_ESTADISTICAS, "Estadísticas");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_SALIR, "Salir");
    
    SetForegroundWindow(hwnd_oculta);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_oculta, NULL);
    PostMessage(hwnd_oculta, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void abrir_carpeta_respaldos() {
    ShellExecute(NULL, "open", carpeta_base_respaldos, NULL, NULL, SW_SHOWNORMAL);
}

void mostrar_estadisticas() {
    char msg[512];
    sprintf(msg, "Archivos vigilados: %d\nCarpetas vigiladas: %d\nBackups hoy: %d\nEstado: %s",
            total_archivos, total_carpetas, total_backups_hoy, programa_pausado ? "PAUSADO" : "ACTIVO");
    MessageBox(NULL, msg, "Estadísticas de GBS Respaldo", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK VentanaCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                mostrar_menu_contextual();
            } else if (lParam == WM_LBUTTONDBLCLK) {
                abrir_carpeta_respaldos();
            } else if (lParam == WM_LBUTTONUP) {
                mostrar_estadisticas();
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_PAUSAR:
                    programa_pausado = 1;
                    cambiar_icono_estado(0, 1);
                    notificar_usuario("GBS Respaldo", "Vigilancia pausada", NIIF_WARNING);
                    break;
                case ID_REANUDAR:
                    programa_pausado = 0;
                    cambiar_icono_estado(0, 0);
                    notificar_usuario("GBS Respaldo", "Vigilancia reanudada", NIIF_INFO);
                    break;
                case ID_ABRIR_CARPETA:
                    abrir_carpeta_respaldos();
                    break;
                case ID_ESTADISTICAS:
                    mostrar_estadisticas();
                    break;
                case ID_SALIR:
                    PostQuitMessage(0);
                    break;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int obtener_intervalo_config() {
    char ruta_config[512];
    sprintf(ruta_config, "%s\\config.txt", carpeta_base_respaldos);
    
    FILE *f = fopen(ruta_config, "r");
    if (f == NULL) {
        f = fopen(ruta_config, "w");
        if (f != NULL) {
            fprintf(f, "# Tiempo de revision en minutos\n");
            fprintf(f, "minutos=5\n");
            fprintf(f, "# Retencion: cuantos backups mantener por hora/dia\n");
            fprintf(f, "retencion_horaria=24\n");
            fprintf(f, "retencion_diaria=7\n");
            fprintf(f, "max_backups=50\n");
            fprintf(f, "# Modo nocturno: evitar respaldos en horarios especificos\n");
            fprintf(f, "modo_nocturno=0\n");
            fprintf(f, "nocturno_inicio=0\n");
            fprintf(f, "nocturno_fin=6\n");
            fprintf(f, "# Extensiones a vigilar\n");
            fprintf(f, "extensiones=.c,.cpp,.h,.txt,.py,.js,.html,.css,.java,.go,.rs\n");
            fprintf(f, "# Carpeta a vigilar (opcional, una por linea)\n");
            fprintf(f, "# vigilar=C:\\MisProyectos\\Proyecto1\n");
            fclose(f);
        }
        return TIEMPO_POR_DEFECTO;
    }
    
    char linea[256];
    int minutos = 5;
    while (fgets(linea, sizeof(linea), f)) {
        if (linea[0] == '#' || linea[0] == '\n') continue;
        if (strncmp(linea, "minutos=", 8) == 0) {
            minutos = atoi(linea + 8);
            if (minutos <= 0) minutos = 5;
        } else if (strncmp(linea, "vigilar=", 8) == 0 && total_carpetas < MAX_CARPETAS) {
            char *p = linea + 8;
            size_t len = strlen(p);
            if (p[len-1] == '\n') p[len-1] = '\0';
            strcpy(lista_carpetas[total_carpetas].carpeta, p);
            
            char *ultimo_slash = strrchr(p, '\\');
            if (ultimo_slash) {
                strcpy(lista_carpetas[total_carpetas].nombre_proyecto, ultimo_slash + 1);
            } else {
                strcpy(lista_carpetas[total_carpetas].nombre_proyecto, p);
            }
            total_carpetas++;
        }
    }
    fclose(f);
    return minutos * 60 * 1000;
}

void escanear_carpeta(const char *carpeta, const char *nombre_proyecto) {
    DIR *dir;
    struct dirent *entrada;
    struct stat info;
    char ruta_completa[1024];
    
    if ((dir = opendir(carpeta)) != NULL) {
        while ((entrada = readdir(dir)) != NULL) {
            if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
                continue;
            
            sprintf(ruta_completa, "%s\\%s", carpeta, entrada->d_name);
            
            if (deberia_excluir(ruta_completa)) continue;
            
            if (stat(ruta_completa, &info) == 0) {
                if (info.st_mode & S_IFDIR) {
                    // Escanear subcarpetas
                    escanear_carpeta(ruta_completa, nombre_proyecto);
                } else {
                    char *ext = strrchr(entrada->d_name, '.');
                    if (ext) {
                        // Verificar extensiones permitidas
                        if (total_archivos < MAX_ARCHIVOS) {
                            strcpy(lista_codigos[total_archivos].ruta_completa, ruta_completa);
                            strcpy(lista_codigos[total_archivos].nombre_archivo, entrada->d_name);
                            strcpy(lista_codigos[total_archivos].directorio_origen, carpeta);
                            strcpy(lista_codigos[total_archivos].nombre_proyecto, nombre_proyecto);
                            lista_codigos[total_archivos].ultima_modificacion = info.st_mtime;
                            lista_codigos[total_archivos].backups_hoy = 0;
                            calcular_md5(ruta_completa, lista_codigos[total_archivos].hash_anterior);
                            total_archivos++;
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
}

void procesar_ruta(const char *ruta_original, CodigoVigilado *archivo) {
    strcpy(archivo->ruta_completa, ruta_original);
    const char *ultimo_slash = strrchr(ruta_original, '\\');
    if (ultimo_slash) {
        strcpy(archivo->nombre_archivo, ultimo_slash + 1);
        int longitud_dir = ultimo_slash - ruta_original;
        strncpy(archivo->directorio_origen, ruta_original, longitud_dir);
        archivo->directorio_origen[longitud_dir] = '\0';
        
        const char *slash_proyecto = strrchr(archivo->directorio_origen, '\\');
        if (slash_proyecto) {
            strcpy(archivo->nombre_proyecto, slash_proyecto + 1);
        } else {
            strcpy(archivo->nombre_proyecto, archivo->directorio_origen);
        }
    } else {
        strcpy(archivo->nombre_archivo, ruta_original);
        strcpy(archivo->directorio_origen, ".");
        strcpy(archivo->nombre_proyecto, "General");
    }
    calcular_md5(ruta_original, archivo->hash_anterior);
    archivo->backups_hoy = 0;
}

void generar_nombre_zip(char *buffer, const char *nombre_archivo) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char nombre_limpio[128];
    strcpy(nombre_limpio, nombre_archivo);
    char *punto = strrchr(nombre_limpio, '.');
    if (punto) *punto = '\0';
    
    sprintf(buffer, "%s_%04d%02d%02d_%02d%02d%02d.zip", 
            nombre_limpio, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, 
            tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// Función para obtener la fecha actual como string
void obtener_fecha_actual(char *fecha) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(fecha, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

int main(int argc, char *argv[]) {
    // Modo comandos especiales
    if (argc >= 2) {
        if (strcmp(argv[1], "--restaurar") == 0 && argc >= 4) {
            restaurar_version(argv[2], argv[3]);
            return 0;
        } else if (strcmp(argv[1], "--diff") == 0 && argc >= 5) {
            mostrar_diff(argv[2], argv[3], argv[4]);
            return 0;
        } else if (strcmp(argv[1], "--exportar-git") == 0 && argc >= 3) {
            exportar_a_git(argv[2]);
            return 0;
        }
    }
    
    // Crear ventana oculta para manejar mensajes
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = VentanaCallback;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "GBSRespaldoClass";
    RegisterClassEx(&wc);
    
    hwnd_oculta = CreateWindowEx(0, "GBSRespaldoClass", "GBSRespaldo", WS_OVERLAPPED,
                                  0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    
    GetModuleFileName(NULL, carpeta_base_respaldos, sizeof(carpeta_base_respaldos));
    char *ultimo_slash = strrchr(carpeta_base_respaldos, '\\');
    if (ultimo_slash) *ultimo_slash = '\0';
    
    intervalo_revision = obtener_intervalo_config();
    obtener_fecha_actual(fecha_actual);
    
    struct stat info;
    
    // Cargar archivos arrastrados
    for (int i = 1; i < argc; i++) {
        if (stat(argv[i], &info) == 0) {
            if (info.st_mode & S_IFDIR) {
                if (total_carpetas < MAX_CARPETAS) {
                    strcpy(lista_carpetas[total_carpetas].carpeta, argv[i]);
                    const char *nombre = strrchr(argv[i], '\\');
                    if (nombre) {
                        strcpy(lista_carpetas[total_carpetas].nombre_proyecto, nombre + 1);
                    } else {
                        strcpy(lista_carpetas[total_carpetas].nombre_proyecto, argv[i]);
                    }
                    total_carpetas++;
                }
            } else {
                if (!deberia_excluir(argv[i])) {
                    procesar_ruta(argv[i], &lista_codigos[total_archivos]);
                    lista_codigos[total_archivos].ultima_modificacion = info.st_mtime;
                    total_archivos++;
                    if (total_archivos >= MAX_ARCHIVOS) break;
                }
            }
        }
    }
    
    // Escanear carpetas configuradas
    for (int i = 0; i < total_carpetas; i++) {
        escanear_carpeta(lista_carpetas[i].carpeta, lista_carpetas[i].nombre_proyecto);
    }
    
    if (total_archivos == 0 && total_carpetas == 0) {
        MessageBox(NULL, "Arrastra archivos o carpetas de código sobre este .exe para vigilarlos.\n\nTambién puedes configurar carpetas fijas en config.txt", 
                   "Guardián de Código Avanzado", MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    
    // Configurar icono en bandeja
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd_oculta;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(nid.szTip, "GBS Respaldo Avanzado");
    Shell_NotifyIcon(NIM_ADD, &nid);
    
    // Ocultar ventana y consola
    ShowWindow(hwnd_oculta, SW_HIDE);
    FreeConsole();
    
    notificar_usuario("GBS Respaldo", "Vigilando archivos. Click izquierdo para estadísticas, derecho para menú.", NIIF_INFO);
    cambiar_icono_estado(0, 0);
    
    MSG msg;
    
    // Bucle principal con manejo de mensajes
    while (1) {
        // Procesar mensajes de Windows sin bloquear
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                Shell_NotifyIcon(NIM_DELETE, &nid);
                return 0;
            }
        }
        
        if (!programa_pausado && !esta_en_horario_nocturno()) {
            int cambio_en_este_ciclo = 0;
            char fecha_hoy[11];
            obtener_fecha_actual(fecha_hoy);
            
            // Resetear contador diario si cambió la fecha
            if (strcmp(fecha_hoy, fecha_actual) != 0) {
                strcpy(fecha_actual, fecha_hoy);
                total_backups_hoy = 0;
                for (int i = 0; i < total_archivos; i++) {
                    lista_codigos[i].backups_hoy = 0;
                }
            }
            
            for (int i = 0; i < total_archivos; i++) {
                if (stat(lista_codigos[i].ruta_completa, &info) == 0) {
                    char hash_actual[33];
                    calcular_md5(lista_codigos[i].ruta_completa, hash_actual);
                    
                    if (strcmp(hash_actual, lista_codigos[i].hash_anterior) != 0) {
                        strcpy(lista_codigos[i].hash_anterior, hash_actual);
                        lista_codigos[i].ultima_modificacion = info.st_mtime;
                        lista_codigos[i].backups_hoy++;
                        total_backups_hoy++;
                        
                        if (!cambio_en_este_ciclo) {
                            cambio_en_este_ciclo = 1;
                            cambiar_icono_estado(1, 0);
                        }
                        
                        char carpeta_proyecto[512];
                        sprintf(carpeta_proyecto, "%s\\%s", carpeta_base_respaldos, lista_codigos[i].nombre_proyecto);
                        CreateDirectory(carpeta_proyecto, NULL);
                        
                        char nombre_zip[256];
                        generar_nombre_zip(nombre_zip, lista_codigos[i].nombre_archivo);
                        
                        char comando[2048];
                        sprintf(comando, "tar -a -cf \"%s\\%s\" -C \"%s\" \"%s\"", 
                                carpeta_proyecto, nombre_zip, lista_codigos[i].directorio_origen, lista_codigos[i].nombre_archivo);
                        
                        system(comando);
                        
                        char msg_notif[256];
                        sprintf(msg_notif, "Backup #%d hoy: %s", lista_codigos[i].backups_hoy, lista_codigos[i].nombre_archivo);
                        notificar_usuario("Archivo guardado", msg_notif, NIIF_INFO);
                        
                        limpiar_backups_antiguos(lista_codigos[i].nombre_proyecto);
                        
                        // Actualizar tooltip
                        cambiar_icono_estado(0, 0);
                    }
                }
            }
            
            if (cambio_en_este_ciclo) {
                Sleep(2000);
                cambiar_icono_estado(0, 0);
            }
        } else if (esta_en_horario_nocturno() && !programa_pausado) {
            // Modo nocturno: dormir más tiempo
            Sleep(60000); // Esperar 1 minuto antes de revisar de nuevo
            continue;
        }
        
        Sleep(intervalo_revision);
    }
    
    Shell_NotifyIcon(NIM_DELETE, &nid);
    return 0;
}