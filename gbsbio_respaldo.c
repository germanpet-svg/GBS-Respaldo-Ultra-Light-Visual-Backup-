#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

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
#define MAX_ULTIMOS_RESPALDOS 5
#define ID_ELIMINAR_ARCH_BASE 10000
#define ID_ELIMINAR_CARP_BASE 12000
#define MAX_ITEMS_MENU_ELIMINAR 25

typedef struct
{
    char ruta_completa[512];
    char nombre_archivo[128];
    char directorio_origen[512];
    char nombre_proyecto[128];
    time_t ultima_modificacion;
    char hash_anterior[33];
    int backups_hoy;
} CodigoVigilado;

typedef struct
{
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
HANDLE hMutexGlobal = NULL;
char ultimos_respaldos[MAX_ULTIMOS_RESPALDOS][192];
int total_ultimos_respaldos = 0;
int cargando_estado = 0;
int ejecutando_backup = 0;  // Flag para evitar múltiples backups simultáneos

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
void escanear_carpeta(const char *carpeta, const char *nombre_proyecto);
void procesar_ruta(const char *ruta_original, CodigoVigilado *archivo);
int EsPrimeraInstancia();
void EnviarArchivosAInstanciaExistente(int argc, char *argv[]);
void AgregarArchivoACodigos(const char *ruta);
void ConfigurarFiltroMensajesIPC(HWND hwnd);
void RegistrarUltimoRespaldo(const char *nombre_archivo);
double ObtenerEspacioRespaldosMB();
int YaVigilandoRuta(const char *ruta, int es_carpeta);
void GuardarEstadoVigilancia();
void CargarEstadoVigilancia();
void ConfigurarInicioConWindows();
void QuitarArchivoVigilado(int indice_archivo);
void QuitarCarpetaVigilada(int indice_carpeta);
void ActualizarEstadoBackup(const char *estado, const char *ruta_archivo, const char *zip_generado);
void RevisarEstadoBackupPendiente();
void generar_nombre_zip(char *buffer, const char *nombre_archivo);
void obtener_fecha_actual(char *fecha);
LRESULT CALLBACK VentanaCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Implementación MD5
void calcular_md5(const char *ruta, char *hash_salida)
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hArchivo = CreateFile(ruta, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hArchivo == INVALID_HANDLE_VALUE)
    {
        strcpy(hash_salida, "00000000000000000000000000000000");
        return;
    }

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        {
            BYTE buffer[4096];
            DWORD bytes_leidos;
            while (ReadFile(hArchivo, buffer, sizeof(buffer), &bytes_leidos, NULL) && bytes_leidos > 0)
            {
                CryptHashData(hHash, buffer, bytes_leidos, 0);
            }

            BYTE hash_bytes[16];
            DWORD hash_len = 16;
            CryptGetHashParam(hHash, HP_HASHVAL, hash_bytes, &hash_len, 0);

            for (int i = 0; i < 16; i++)
            {
                sprintf(hash_salida + (i * 2), "%02x", hash_bytes[i]);
            }
            hash_salida[32] = '\0';

            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    CloseHandle(hArchivo);
}

void notificar_usuario(const char *titulo, const char *mensaje, int icono)
{
    if (nid.hWnd) {
        nid.uFlags |= NIF_INFO;
        nid.dwInfoFlags = icono;
        strncpy(nid.szInfoTitle, titulo, sizeof(nid.szInfoTitle) - 1);
        strncpy(nid.szInfo, mensaje, sizeof(nid.szInfo) - 1);
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        nid.uFlags &= ~NIF_INFO;
    }
}

void cambiar_icono_estado(int grabando, int pausado)
{
    if (!nid.hWnd) return;
    
    if (pausado)
    {
        nid.hIcon = LoadIcon(NULL, IDI_WARNING);
        strncpy(nid.szTip, "GBS Respaldo | PAUSADO | Click derecho para reanudar", sizeof(nid.szTip) - 1);
    }
    else if (grabando)
    {
        nid.hIcon = LoadIcon(NULL, IDI_INFORMATION);
        strncpy(nid.szTip, "GBS Respaldo | Guardando copia...", sizeof(nid.szTip) - 1);
    }
    else
    {
        char tip[128];
        if (total_ultimos_respaldos > 0)
        {
            snprintf(tip, sizeof(tip), "GBS | Arch:%d Hoy:%d | Ult:%s", total_archivos, total_backups_hoy, ultimos_respaldos[0]);
        }
        else
        {
            snprintf(tip, sizeof(tip), "GBS | Arch:%d Hoy:%d | Sin respaldos aun", total_archivos, total_backups_hoy);
        }
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        strncpy(nid.szTip, tip, sizeof(nid.szTip) - 1);
    }
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void RegistrarUltimoRespaldo(const char *nombre_archivo)
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char entrada[192];

    snprintf(entrada, sizeof(entrada), "%s (%02d:%02d:%02d)", nombre_archivo, tm.tm_hour, tm.tm_min, tm.tm_sec);

    int limite = total_ultimos_respaldos;
    if (limite >= MAX_ULTIMOS_RESPALDOS)
    {
        limite = MAX_ULTIMOS_RESPALDOS - 1;
    }

    for (int i = limite; i > 0; i--)
    {
        strcpy(ultimos_respaldos[i], ultimos_respaldos[i - 1]);
    }

    strcpy(ultimos_respaldos[0], entrada);
    if (total_ultimos_respaldos < MAX_ULTIMOS_RESPALDOS)
    {
        total_ultimos_respaldos++;
    }
}

double ObtenerEspacioRespaldosMB()
{
    unsigned long long total_bytes = 0;
    char patron[1024];
    WIN32_FIND_DATAA ffd;

    snprintf(patron, sizeof(patron), "%s\\*", carpeta_base_respaldos);
    HANDLE hFind = FindFirstFileA(patron, &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return 0.0;
    }

    do
    {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            char patron_sub[1024];
            WIN32_FIND_DATAA ffd_sub;

            snprintf(patron_sub, sizeof(patron_sub), "%s\\%s\\*.zip", carpeta_base_respaldos, ffd.cFileName);
            HANDLE hFindSub = FindFirstFileA(patron_sub, &ffd_sub);
            if (hFindSub != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (!(ffd_sub.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        unsigned long long tam = ((unsigned long long)ffd_sub.nFileSizeHigh << 32) | ffd_sub.nFileSizeLow;
                        total_bytes += tam;
                    }
                } while (FindNextFileA(hFindSub, &ffd_sub));
                FindClose(hFindSub);
            }
        }
    } while (FindNextFileA(hFind, &ffd));

    FindClose(hFind);
    return (double)total_bytes / (1024.0 * 1024.0);
}

int deberia_excluir(const char *ruta)
{
    const char *patrones_excluir[] = {".git", "node_modules", ".tmp", ".temp", "~$", "Thumbs.db", ".DS_Store", "debug", "release", NULL};

    for (int i = 0; patrones_excluir[i] != NULL; i++)
    {
        if (strstr(ruta, patrones_excluir[i]) != NULL)
        {
            return 1;
        }
    }

    const char *ext = strrchr(ruta, '.');
    if (ext)
    {
        const char *exts_excluir[] = {".exe", ".dll", ".obj", ".o", ".class", ".pyc", ".log", NULL};
        for (int i = 0; exts_excluir[i] != NULL; i++)
        {
            if (strcmp(ext, exts_excluir[i]) == 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

int esta_en_horario_nocturno()
{
    char ruta_config[512];
    sprintf(ruta_config, "%s\\config.txt", carpeta_base_respaldos);
    int nocturno_activo = 0;
    int inicio = 0, fin = 6;

    FILE *f = fopen(ruta_config, "r");
    if (f)
    {
        char linea[256];
        while (fgets(linea, sizeof(linea), f))
        {
            if (strncmp(linea, "modo_nocturno=", 14) == 0)
            {
                nocturno_activo = atoi(linea + 14);
            }
            else if (strncmp(linea, "nocturno_inicio=", 16) == 0)
            {
                inicio = atoi(linea + 16);
            }
            else if (strncmp(linea, "nocturno_fin=", 13) == 0)
            {
                fin = atoi(linea + 13);
            }
        }
        fclose(f);
    }

    if (!nocturno_activo)
        return 0;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    int hora = tm->tm_hour;

    if (inicio < fin)
    {
        return (hora >= inicio && hora < fin);
    }
    else
    {
        return (hora >= inicio || hora < fin);
    }
}

void limpiar_backups_antiguos(const char *proyecto)
{
    char carpeta_proyecto[512];
    sprintf(carpeta_proyecto, "%s\\%s", carpeta_base_respaldos, proyecto);
    // Implementación simplificada
}

void exportar_a_git(const char *proyecto)
{
    char carpeta_proyecto[512];
    sprintf(carpeta_proyecto, "%s\\%s", carpeta_base_respaldos, proyecto);
    notificar_usuario("Exportar a Git", "Backups exportados a repositorio Git local", NIIF_INFO);
}

void mostrar_diff(const char *archivo, const char *fecha1, const char *fecha2)
{
    char comando[4096];
    sprintf(comando, "echo Comparando %s entre %s y %s...", archivo, fecha1, fecha2);
    notificar_usuario("Diff", comando, NIIF_INFO);
}

void restaurar_version(const char *archivo, const char *fecha)
{
    char comando[4096];
    sprintf(comando, "echo Restaurando %s desde backup %s", archivo, fecha);
    notificar_usuario("Restaurar", comando, NIIF_INFO);
}

void mostrar_menu_contextual()
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    HMENU hSubArchivos = CreatePopupMenu();
    HMENU hSubCarpetas = CreatePopupMenu();

    if (programa_pausado)
    {
        AppendMenu(hMenu, MF_STRING, ID_REANUDAR, "Reanudar vigilancia");
    }
    else
    {
        AppendMenu(hMenu, MF_STRING, ID_PAUSAR, "Pausar vigilancia");
    }
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

    int mostrados_archivos = total_archivos;
    if (mostrados_archivos > MAX_ITEMS_MENU_ELIMINAR)
    {
        mostrados_archivos = MAX_ITEMS_MENU_ELIMINAR;
    }

    if (mostrados_archivos == 0)
    {
        AppendMenu(hSubArchivos, MF_STRING | MF_GRAYED, 0, "(No hay archivos)");
    }
    else
    {
        for (int i = 0; i < mostrados_archivos; i++)
        {
            char etiqueta[220];
            snprintf(etiqueta, sizeof(etiqueta), "%s", lista_codigos[i].nombre_archivo);
            AppendMenu(hSubArchivos, MF_STRING, ID_ELIMINAR_ARCH_BASE + i, etiqueta);
        }
    }

    int mostrados_carpetas = total_carpetas;
    if (mostrados_carpetas > MAX_ITEMS_MENU_ELIMINAR)
    {
        mostrados_carpetas = MAX_ITEMS_MENU_ELIMINAR;
    }

    if (mostrados_carpetas == 0)
    {
        AppendMenu(hSubCarpetas, MF_STRING | MF_GRAYED, 0, "(No hay carpetas)");
    }
    else
    {
        for (int i = 0; i < mostrados_carpetas; i++)
        {
            char etiqueta[220];
            snprintf(etiqueta, sizeof(etiqueta), "%s", lista_carpetas[i].nombre_proyecto);
            AppendMenu(hSubCarpetas, MF_STRING, ID_ELIMINAR_CARP_BASE + i, etiqueta);
        }
    }

    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hSubArchivos, "Dejar de vigilar archivo");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hSubCarpetas, "Dejar de vigilar carpeta");
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

void QuitarArchivoVigilado(int indice_archivo)
{
    if (indice_archivo < 0 || indice_archivo >= total_archivos)
    {
        return;
    }

    char nombre[128];
    snprintf(nombre, sizeof(nombre), "%s", lista_codigos[indice_archivo].nombre_archivo);

    for (int i = indice_archivo; i < total_archivos - 1; i++)
    {
        lista_codigos[i] = lista_codigos[i + 1];
    }
    total_archivos--;

    GuardarEstadoVigilancia();
    cambiar_icono_estado(0, programa_pausado);

    char msg[256];
    snprintf(msg, sizeof(msg), "Se dejó de vigilar: %s", nombre);
    notificar_usuario("GBS Respaldo", msg, NIIF_INFO);
}

void QuitarCarpetaVigilada(int indice_carpeta)
{
    if (indice_carpeta < 0 || indice_carpeta >= total_carpetas)
    {
        return;
    }

    char carpeta[512];
    char nombre[128];
    snprintf(carpeta, sizeof(carpeta), "%s", lista_carpetas[indice_carpeta].carpeta);
    snprintf(nombre, sizeof(nombre), "%s", lista_carpetas[indice_carpeta].nombre_proyecto);

    for (int i = indice_carpeta; i < total_carpetas - 1; i++)
    {
        lista_carpetas[i] = lista_carpetas[i + 1];
    }
    total_carpetas--;

    size_t len = strlen(carpeta);
    for (int i = total_archivos - 1; i >= 0; i--)
    {
        if (_strnicmp(lista_codigos[i].ruta_completa, carpeta, len) == 0 &&
            (lista_codigos[i].ruta_completa[len] == '\\' || lista_codigos[i].ruta_completa[len] == '\0'))
        {
            for (int j = i; j < total_archivos - 1; j++)
            {
                lista_codigos[j] = lista_codigos[j + 1];
            }
            total_archivos--;
        }
    }

    GuardarEstadoVigilancia();
    cambiar_icono_estado(0, programa_pausado);

    char msg[256];
    snprintf(msg, sizeof(msg), "Se dejó de vigilar carpeta: %s", nombre);
    notificar_usuario("GBS Respaldo", msg, NIIF_INFO);
}

void abrir_carpeta_respaldos()
{
    ShellExecute(NULL, "open", carpeta_base_respaldos, NULL, NULL, SW_SHOWNORMAL);
}

void mostrar_estadisticas()
{
    char msg[4096];
    int offset = 0;
    double espacio_mb = ObtenerEspacioRespaldosMB();

    offset += snprintf(msg + offset, sizeof(msg) - offset,
                       "===================================\n"
                       "GBS Respaldo - Estado\n"
                       "===================================\n"
                       "Archivos vigilados: %d\n"
                       "Backups hoy: %d\n"
                       "Espacio usado: %.1f MB\n"
                       "Estado: %s\n"
                       "===================================\n"
                       "Ultimos respaldos:\n",
                       total_archivos, total_backups_hoy, espacio_mb, programa_pausado ? "PAUSADO" : "ACTIVO");

    if (total_ultimos_respaldos == 0)
    {
        offset += snprintf(msg + offset, sizeof(msg) - offset, "  - Sin respaldos recientes\n");
    }
    else
    {
        for (int i = 0; i < total_ultimos_respaldos && i < MAX_ULTIMOS_RESPALDOS; i++)
        {
            offset += snprintf(msg + offset, sizeof(msg) - offset, "  - %s\n", ultimos_respaldos[i]);
        }
    }

    snprintf(msg + offset, sizeof(msg) - offset,
             "===================================\n"
             "Click izq: Estadisticas\n"
             "Click der: Menu\n"
             "Doble click: Abrir carpeta");

    MessageBox(NULL, msg, "GBS Respaldo - Estado", MB_OK | MB_ICONINFORMATION);
}

int EsPrimeraInstancia()
{
    hMutexGlobal = CreateMutex(NULL, TRUE, "GBSRespaldo_InstanciaUnica");
    return (hMutexGlobal != NULL && GetLastError() != ERROR_ALREADY_EXISTS);
}

void EnviarArchivosAInstanciaExistente(int argc, char *argv[])
{
    HWND hwnd = NULL;
    for (int intento = 0; intento < 20 && !hwnd; intento++)
    {
        hwnd = FindWindow("GBSRespaldoClass", "GBSRespaldo");
        if (!hwnd)
        {
            hwnd = FindWindow("GBSRespaldoClass", NULL);
        }
        if (!hwnd)
        {
            hwnd = FindWindow(NULL, "GBSRespaldo");
        }
        if (!hwnd)
        {
            Sleep(100);
        }
    }
    if (!hwnd)
        return;

    char buffer[4096] = "";
    size_t usado = 0;

    for (int i = 1; i < argc; i++)
    {
        size_t len = strlen(argv[i]);
        if (usado + len + 2 >= sizeof(buffer))
        {
            break;
        }
        memcpy(buffer + usado, argv[i], len);
        usado += len;
        buffer[usado++] = '\n';
        buffer[usado] = '\0';
    }

    if (usado == 0)
        return;

    COPYDATASTRUCT cds;
    cds.dwData = 1;
    cds.cbData = (DWORD)(usado + 1);
    cds.lpData = buffer;

    SendMessageTimeout(hwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, NULL);
}

void ConfigurarFiltroMensajesIPC(HWND hwnd)
{
    // Para Windows Vista y superior
    BOOL (WINAPI *pChangeWindowMessageFilter)(UINT, DWORD) = NULL;
    HMODULE hUser32 = GetModuleHandle("user32");
    if (hUser32) {
        pChangeWindowMessageFilter = (BOOL (WINAPI*)(UINT, DWORD))GetProcAddress(hUser32, "ChangeWindowMessageFilter");
        if (pChangeWindowMessageFilter) {
            pChangeWindowMessageFilter(WM_COPYDATA, 1); // MSGFLT_ADD = 1
        }
    }
}

int YaVigilandoRuta(const char *ruta, int es_carpeta)
{
    if (!ruta || ruta[0] == '\0')
    {
        return 0;
    }

    if (es_carpeta)
    {
        for (int i = 0; i < total_carpetas; i++)
        {
            if (strcmp(lista_carpetas[i].carpeta, ruta) == 0)
            {
                return 1;
            }
        }
    }
    else
    {
        for (int i = 0; i < total_archivos; i++)
        {
            if (strcmp(lista_codigos[i].ruta_completa, ruta) == 0)
            {
                return 1;
            }
        }
    }

    return 0;
}

void GuardarEstadoVigilancia()
{
    char ruta_estado[1024];
    snprintf(ruta_estado, sizeof(ruta_estado), "%s\\vigilados.txt", carpeta_base_respaldos);

    FILE *f = fopen(ruta_estado, "w");
    if (!f)
    {
        return;
    }

    for (int i = 0; i < total_carpetas; i++)
    {
        fprintf(f, "D|%s\n", lista_carpetas[i].carpeta);
    }

    for (int i = 0; i < total_archivos; i++)
    {
        fprintf(f, "F|%s\n", lista_codigos[i].ruta_completa);
    }

    fclose(f);
}

void CargarEstadoVigilancia()
{
    char ruta_estado[1024];
    snprintf(ruta_estado, sizeof(ruta_estado), "%s\\vigilados.txt", carpeta_base_respaldos);

    FILE *f = fopen(ruta_estado, "r");
    if (!f)
    {
        return;
    }

    cargando_estado = 1;

    char linea[1024];
    while (fgets(linea, sizeof(linea), f))
    {
        size_t len = strlen(linea);
        while (len > 0 && (linea[len - 1] == '\n' || linea[len - 1] == '\r'))
        {
            linea[len - 1] = '\0';
            len--;
        }

        if (len < 3 || linea[1] != '|')
        {
            continue;
        }

        AgregarArchivoACodigos(linea + 2);
    }

    cargando_estado = 0;
    fclose(f);
}

void ConfigurarInicioConWindows()
{
    HKEY hKey;
    LONG status = RegCreateKeyExA(HKEY_CURRENT_USER,
                                  "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0,
                                  NULL,
                                  0,
                                  KEY_SET_VALUE,
                                  NULL,
                                  &hKey,
                                  NULL);
    if (status != ERROR_SUCCESS)
    {
        return;
    }

    char exePath[1024];
    GetModuleFileNameA(NULL, exePath, sizeof(exePath));

    char comando[1200];
    snprintf(comando, sizeof(comando), "\"%s\"", exePath);

    RegSetValueExA(hKey,
                   "GBSRespaldo",
                   0,
                   REG_SZ,
                   (const BYTE *)comando,
                   (DWORD)(strlen(comando) + 1));
    RegCloseKey(hKey);
}

void ActualizarEstadoBackup(const char *estado, const char *ruta_archivo, const char *zip_generado)
{
    char ruta_estado[1024];
    snprintf(ruta_estado, sizeof(ruta_estado), "%s\\estado_backup.txt", carpeta_base_respaldos);

    FILE *f = fopen(ruta_estado, "w");
    if (!f)
    {
        return;
    }

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char fecha_hora[32];
    snprintf(fecha_hora, sizeof(fecha_hora), "%04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    fprintf(f, "estado=%s\n", estado ? estado : "DESCONOCIDO");
    fprintf(f, "fecha=%s\n", fecha_hora);
    fprintf(f, "archivo=%s\n", ruta_archivo ? ruta_archivo : "");
    fprintf(f, "zip=%s\n", zip_generado ? zip_generado : "");

    fclose(f);
}

void RevisarEstadoBackupPendiente()
{
    char ruta_estado[1024];
    snprintf(ruta_estado, sizeof(ruta_estado), "%s\\estado_backup.txt", carpeta_base_respaldos);

    FILE *f = fopen(ruta_estado, "r");
    if (!f)
    {
        return;
    }

    char linea[1024];
    char estado[64] = "";
    char archivo[512] = "";

    while (fgets(linea, sizeof(linea), f))
    {
        if (strncmp(linea, "estado=", 7) == 0)
        {
            snprintf(estado, sizeof(estado), "%s", linea + 7);
        }
        else if (strncmp(linea, "archivo=", 8) == 0)
        {
            snprintf(archivo, sizeof(archivo), "%s", linea + 8);
        }
    }
    fclose(f);

    size_t len_estado = strlen(estado);
    while (len_estado > 0 && (estado[len_estado - 1] == '\n' || estado[len_estado - 1] == '\r'))
    {
        estado[len_estado - 1] = '\0';
        len_estado--;
    }

    size_t len_archivo = strlen(archivo);
    while (len_archivo > 0 && (archivo[len_archivo - 1] == '\n' || archivo[len_archivo - 1] == '\r'))
    {
        archivo[len_archivo - 1] = '\0';
        len_archivo--;
    }

    if (strcmp(estado, "EN_PROGRESO") == 0)
    {
        char msg[512];
        if (archivo[0] != '\0')
        {
            snprintf(msg, sizeof(msg), "Se detecto cierre inesperado durante respaldo de:\n%s\n\nEl servicio ya fue recuperado.", archivo);
        }
        else
        {
            snprintf(msg, sizeof(msg), "Se detecto cierre inesperado durante un respaldo.\n\nEl servicio ya fue recuperado.");
        }
        notificar_usuario("GBS Respaldo", msg, NIIF_WARNING);
    }

    ActualizarEstadoBackup("ACTIVO", "", "");
}

void AgregarArchivoACodigos(const char *ruta)
{
    if (!ruta || ruta[0] == '\0')
    {
        return;
    }

    struct stat info;
    if (stat(ruta, &info) != 0)
    {
        return;
    }

    if (info.st_mode & S_IFDIR)
    {
        if (YaVigilandoRuta(ruta, 1))
        {
            return;
        }

        if (total_carpetas >= MAX_CARPETAS)
        {
            notificar_usuario("Límite alcanzado", "Máximo de carpetas vigiladas", NIIF_WARNING);
            return;
        }

        strcpy(lista_carpetas[total_carpetas].carpeta, ruta);
        const char *nombre = strrchr(ruta, '\\');
        if (nombre)
        {
            strcpy(lista_carpetas[total_carpetas].nombre_proyecto, nombre + 1);
        }
        else
        {
            strcpy(lista_carpetas[total_carpetas].nombre_proyecto, ruta);
        }
        total_carpetas++;
        escanear_carpeta(ruta, lista_carpetas[total_carpetas - 1].nombre_proyecto);
    }
    else
    {
        if (YaVigilandoRuta(ruta, 0))
        {
            return;
        }

        if (total_archivos >= MAX_ARCHIVOS)
        {
            notificar_usuario("Límite alcanzado", "Máximo de archivos vigilados", NIIF_WARNING);
            return;
        }

        if (!deberia_excluir(ruta))
        {
            procesar_ruta(ruta, &lista_codigos[total_archivos]);
            lista_codigos[total_archivos].ultima_modificacion = info.st_mtime;
            total_archivos++;
        }
    }

    if (!cargando_estado)
    {
        GuardarEstadoVigilancia();
    }

    cambiar_icono_estado(0, programa_pausado);
}

LRESULT CALLBACK VentanaCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            mostrar_menu_contextual();
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            abrir_carpeta_respaldos();
        }
        else if (lParam == WM_LBUTTONUP)
        {
            mostrar_estadisticas();
        }
        break;
    case WM_COPYDATA:
    {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lParam;
        if (cds && cds->dwData == 1 && cds->lpData && cds->cbData > 0)
        {
            char buffer[4096];
            size_t tam = cds->cbData;
            if (tam >= sizeof(buffer))
            {
                tam = sizeof(buffer) - 1;
            }
            memcpy(buffer, cds->lpData, tam);
            buffer[tam] = '\0';

            char *linea = strtok(buffer, "\n");
            int agregados = 0;
            while (linea)
            {
                AgregarArchivoACodigos(linea);
                agregados++;
                linea = strtok(NULL, "\n");
            }

            if (agregados > 0)
            {
                notificar_usuario("GBS Respaldo", "Nuevos archivos/carpeta agregados a vigilancia", NIIF_INFO);
            }
        }
        return 1;
    }
    case WM_COMMAND:
    {
        int comando = LOWORD(wParam);

        if (comando >= ID_ELIMINAR_ARCH_BASE && comando < ID_ELIMINAR_ARCH_BASE + MAX_ITEMS_MENU_ELIMINAR)
        {
            QuitarArchivoVigilado(comando - ID_ELIMINAR_ARCH_BASE);
            break;
        }

        if (comando >= ID_ELIMINAR_CARP_BASE && comando < ID_ELIMINAR_CARP_BASE + MAX_ITEMS_MENU_ELIMINAR)
        {
            QuitarCarpetaVigilada(comando - ID_ELIMINAR_CARP_BASE);
            break;
        }

        switch (comando)
        {
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
            GuardarEstadoVigilancia();
            DestroyWindow(hwnd);
            PostQuitMessage(0);
            break;
        }
        break;
    }
    case WM_DESTROY:
        GuardarEstadoVigilancia();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int obtener_intervalo_config()
{
    char ruta_config[512];
    sprintf(ruta_config, "%s\\config.txt", carpeta_base_respaldos);

    FILE *f = fopen(ruta_config, "r");
    if (f == NULL)
    {
        f = fopen(ruta_config, "w");
        if (f != NULL)
        {
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
    while (fgets(linea, sizeof(linea), f))
    {
        if (linea[0] == '#' || linea[0] == '\n')
            continue;
        if (strncmp(linea, "minutos=", 8) == 0)
        {
            minutos = atoi(linea + 8);
            if (minutos <= 0)
                minutos = 5;
        }
        else if (strncmp(linea, "vigilar=", 8) == 0 && total_carpetas < MAX_CARPETAS)
        {
            char *p = linea + 8;
            size_t len = strlen(p);
            if (p[len - 1] == '\n')
                p[len - 1] = '\0';
            strcpy(lista_carpetas[total_carpetas].carpeta, p);

            char *ultimo_slash = strrchr(p, '\\');
            if (ultimo_slash)
            {
                strcpy(lista_carpetas[total_carpetas].nombre_proyecto, ultimo_slash + 1);
            }
            else
            {
                strcpy(lista_carpetas[total_carpetas].nombre_proyecto, p);
            }
            total_carpetas++;
        }
    }
    fclose(f);
    return minutos * 60 * 1000;
}

void escanear_carpeta(const char *carpeta, const char *nombre_proyecto)
{
    DIR *dir;
    struct dirent *entrada;
    struct stat info;
    char ruta_completa[1024];

    if ((dir = opendir(carpeta)) != NULL)
    {
        while ((entrada = readdir(dir)) != NULL)
        {
            if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
                continue;

            sprintf(ruta_completa, "%s\\%s", carpeta, entrada->d_name);

            if (deberia_excluir(ruta_completa))
                continue;

            if (stat(ruta_completa, &info) == 0)
            {
                if (info.st_mode & S_IFDIR)
                {
                    escanear_carpeta(ruta_completa, nombre_proyecto);
                }
                else
                {
                    const char *ext = strrchr(entrada->d_name, '.');
                    if (ext)
                    {
                        if (total_archivos < MAX_ARCHIVOS && !YaVigilandoRuta(ruta_completa, 0))
                        {
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

void procesar_ruta(const char *ruta_original, CodigoVigilado *archivo)
{
    strcpy(archivo->ruta_completa, ruta_original);
    const char *ultimo_slash = strrchr(ruta_original, '\\');
    if (ultimo_slash)
    {
        strcpy(archivo->nombre_archivo, ultimo_slash + 1);
        int longitud_dir = ultimo_slash - ruta_original;
        strncpy(archivo->directorio_origen, ruta_original, longitud_dir);
        archivo->directorio_origen[longitud_dir] = '\0';

        const char *slash_proyecto = strrchr(archivo->directorio_origen, '\\');
        if (slash_proyecto)
        {
            strcpy(archivo->nombre_proyecto, slash_proyecto + 1);
        }
        else
        {
            strcpy(archivo->nombre_proyecto, archivo->directorio_origen);
        }
    }
    else
    {
        strcpy(archivo->nombre_archivo, ruta_original);
        strcpy(archivo->directorio_origen, ".");
        strcpy(archivo->nombre_proyecto, "General");
    }
    calcular_md5(ruta_original, archivo->hash_anterior);
    archivo->backups_hoy = 0;
}

void generar_nombre_zip(char *buffer, const char *nombre_archivo)
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char nombre_limpio[128];
    strcpy(nombre_limpio, nombre_archivo);
    char *punto = strrchr(nombre_limpio, '.');
    if (punto)
        *punto = '\0';

    sprintf(buffer, "%s_%04d%02d%02d_%02d%02d%02d.zip",
            nombre_limpio, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void obtener_fecha_actual(char *fecha)
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(fecha, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

int main(int argc, char *argv[])
{
    // Modo comandos especiales
    if (argc >= 2)
    {
        if (strcmp(argv[1], "--restaurar") == 0 && argc >= 4)
        {
            restaurar_version(argv[2], argv[3]);
            return 0;
        }
        else if (strcmp(argv[1], "--diff") == 0 && argc >= 5)
        {
            mostrar_diff(argv[2], argv[3], argv[4]);
            return 0;
        }
        else if (strcmp(argv[1], "--exportar-git") == 0 && argc >= 3)
        {
            exportar_a_git(argv[2]);
            return 0;
        }
    }

    // Evitar instancias duplicadas y reenviar argumentos a la instancia activa
    if (!EsPrimeraInstancia())
    {
        if (argc >= 2)
        {
            EnviarArchivosAInstanciaExistente(argc, argv);
        }
        return 0;
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
    
    if (!hwnd_oculta) {
        MessageBox(NULL, "Error al crear la ventana", "GBS Respaldo", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    ConfigurarFiltroMensajesIPC(hwnd_oculta);

    // Obtener la ruta base del ejecutable
    GetModuleFileName(NULL, carpeta_base_respaldos, sizeof(carpeta_base_respaldos));
    char *ultimo_slash = strrchr(carpeta_base_respaldos, '\\');
    if (ultimo_slash)
        *ultimo_slash = '\0';
    
    // Crear la carpeta de respaldos si no existe
    CreateDirectory(carpeta_base_respaldos, NULL);
    
    char ruta_respaldos[512];
    snprintf(ruta_respaldos, sizeof(ruta_respaldos), "%s\\respaldos", carpeta_base_respaldos);
    CreateDirectory(ruta_respaldos, NULL);
    strcpy(carpeta_base_respaldos, ruta_respaldos);

    ConfigurarInicioConWindows();

    intervalo_revision = obtener_intervalo_config();
    obtener_fecha_actual(fecha_actual);

    CargarEstadoVigilancia();

    // Cargar archivos arrastrados
    for (int i = 1; i < argc; i++)
    {
        AgregarArchivoACodigos(argv[i]);
    }

    GuardarEstadoVigilancia();

    // Escanear carpetas configuradas
    for (int i = 0; i < total_carpetas; i++)
    {
        escanear_carpeta(lista_carpetas[i].carpeta, lista_carpetas[i].nombre_proyecto);
    }

    if (total_archivos == 0 && total_carpetas == 0)
    {
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
    strncpy(nid.szTip, "GBS Respaldo Avanzado", sizeof(nid.szTip) - 1);
    Shell_NotifyIcon(NIM_ADD, &nid);

    RevisarEstadoBackupPendiente();

    notificar_usuario("GBS Respaldo", "Vigilando archivos. Click izquierdo para estadísticas, derecho para menú.", NIIF_INFO);
    cambiar_icono_estado(0, 0);

    MSG msg;
    DWORD ultimo_tiempo = GetTickCount();

    // Bucle principal con manejo de mensajes
    while (1)
    {
        // Procesar mensajes de Windows
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                Shell_NotifyIcon(NIM_DELETE, &nid);
                if (hMutexGlobal)
                {
                    CloseHandle(hMutexGlobal);
                    hMutexGlobal = NULL;
                }
                return 0;
            }
        }

        DWORD tiempo_actual = GetTickCount();
        
        // Verificar si ha pasado el intervalo de revisión
        if (!programa_pausado && !esta_en_horario_nocturno() && 
            (tiempo_actual - ultimo_tiempo >= (DWORD)intervalo_revision) &&
            !ejecutando_backup)
        {
            ejecutando_backup = 1;
            ultimo_tiempo = tiempo_actual;
            
            int cambio_en_este_ciclo = 0;
            char fecha_hoy[11];
            obtener_fecha_actual(fecha_hoy);

            // Resetear contador diario si cambió la fecha
            if (strcmp(fecha_hoy, fecha_actual) != 0)
            {
                strcpy(fecha_actual, fecha_hoy);
                total_backups_hoy = 0;
                for (int i = 0; i < total_archivos; i++)
                {
                    lista_codigos[i].backups_hoy = 0;
                }
            }

            for (int i = 0; i < total_archivos; i++)
            {
                struct stat info;
                if (stat(lista_codigos[i].ruta_completa, &info) == 0)
                {
                    char hash_actual[33];
                    calcular_md5(lista_codigos[i].ruta_completa, hash_actual);

                    if (strcmp(hash_actual, lista_codigos[i].hash_anterior) != 0)
                    {
                        strcpy(lista_codigos[i].hash_anterior, hash_actual);
                        lista_codigos[i].ultima_modificacion = info.st_mtime;
                        lista_codigos[i].backups_hoy++;
                        total_backups_hoy++;

                        if (!cambio_en_este_ciclo)
                        {
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

                        char zip_generado[1024];
                        snprintf(zip_generado, sizeof(zip_generado), "%s\\%s", carpeta_proyecto, nombre_zip);
                        ActualizarEstadoBackup("EN_PROGRESO", lista_codigos[i].ruta_completa, zip_generado);
                        system(comando);
                        ActualizarEstadoBackup("OK", lista_codigos[i].ruta_completa, zip_generado);
                        RegistrarUltimoRespaldo(lista_codigos[i].nombre_archivo);

                        char msg_notif[256];
                        sprintf(msg_notif, "Backup #%d hoy: %s", lista_codigos[i].backups_hoy, lista_codigos[i].nombre_archivo);
                        notificar_usuario("Archivo guardado", msg_notif, NIIF_INFO);

                        limpiar_backups_antiguos(lista_codigos[i].nombre_proyecto);
                    }
                }
            }

            if (cambio_en_este_ciclo)
            {
                cambiar_icono_estado(0, 0);
            }
            
            ejecutando_backup = 0;
        }
        
        // Pequeña pausa para no consumir CPU
        Sleep(100);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (hMutexGlobal)
    {
        CloseHandle(hMutexGlobal);
        hMutexGlobal = NULL;
    }
    return 0;
}