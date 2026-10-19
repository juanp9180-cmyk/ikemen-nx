// Ikemen GO for Nintendo Switch: homebrew entry point.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>
#include <SDL2/SDL.h>
#include "libikemen.h"
#include "nxupdate.h"

#define IKEMEN_NX_VERSION_STR "Ikemen NX v0.3.1"

static char log_path[FS_MAX_PATH + 32] = "sdmc:/switch/ikemen/go_stdout.log";

void nx_log_close(void); // horizon.c

// show_log prints the tail of the Go log on the console, so errors can be
// read on the device without taking the SD card out.
static void show_log(void) {
	nx_log_close(); // the Horizon layer keeps it open for writing
	SDL_Quit(); // release the display so the text console can use it
	consoleInit(NULL);
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	PadState pad;
	padInitializeDefault(&pad);

	static char lines[40][160];
	int n = 0;
	FILE *f = fopen(log_path, "r");
	if (f) {
		char line[160];
		while (fgets(line, sizeof line, f)) {
			// Skip the very chatty shader setup lines.
			if (strncmp(line, "GLES: ", 6) == 0 && !strstr(line, "framebuffer") && !strstr(line, "Error"))
				continue;
			strcpy(lines[n % 40], line);
			n++;
		}
		fclose(f);
	}
	printf("%s - Ikemen termino\n\n", IKEMEN_NX_VERSION_STR);
	int first = n > 40 ? n - 40 : 0;
	for (int i = first; i < n; i++)
		fputs(lines[i % 40], stdout);
	if (n == 0)
		printf("(sin registro en %s)\n", log_path);
	printf("\nPulsa + para salir.\n");
	while (appletMainLoop()) {
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
		consoleUpdate(NULL);
	}
	consoleExit(NULL);
}

// Called by the Horizon runtime layer when Go exits the process (os.Exit,
// fatal errors), which never returns to main.
void nx_on_exit(int code) {
	(void)code;
	show_log();
}

// warn_applet_mode explains how to launch with full memory when started as
// a library applet (e.g. from the Album), where only ~400 MB are available.
// Returns false if the user chose to quit.
static bool warn_applet_mode(void) {
	AppletType t = appletGetAppletType();
	if (t == AppletType_Application || t == AppletType_SystemApplication)
		return true;
	consoleInit(NULL);
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	PadState pad;
	padInitializeDefault(&pad);
	printf("%s\n\n", IKEMEN_NX_VERSION_STR);
	printf("Ikemen se abrio en modo applet (por ejemplo desde el Album).\n");
	printf("En ese modo hay muy poca memoria y las peleas se ven en negro.\n\n");
	printf("Para abrirlo bien: manten pulsado R mientras abres cualquier\n");
	printf("juego, y elige Ikemen en el menu de homebrew.\n\n");
	printf("Pulsa + para salir, o A para intentarlo igual.\n");
	bool go = false;
	while (appletMainLoop()) {
		padUpdate(&pad);
		u64 d = padGetButtonsDown(&pad);
		if (d & HidNpadButton_Plus) break;
		if (d & HidNpadButton_A) { go = true; break; }
		consoleUpdate(NULL);
	}
	consoleExit(NULL);
	return go;
}

static void update_progress(double now, double total) {
	if (total > 0) printf("\x1b[14;4H%.1f de %.1f MB   ", now / 1048576.0, total / 1048576.0);
	else printf("\x1b[14;4H%.1f MB   ", now / 1048576.0);
	consoleUpdate(NULL);
}

// Looks for a newer engine (once a day, see nxupdate.h) and offers to
// update this .nro, keeping its icon and name. Returns true if the .nro
// was replaced: the game must be started again.
static bool check_update(const char *self) {
	if (!nxu_config_get("auto", 1)) return false;
	long last = nxu_config_get("last_check", 0);
	if (last && time(NULL) - last < 24 * 3600 && time(NULL) >= last) return false;
	if (R_FAILED(socketInitializeDefault())) return false;
	curl_global_init(CURL_GLOBAL_DEFAULT);
	static char manifest[16 * 1024];
	NxuInfo info;
	char cur[32];
	bool replaced = false, fetched = nxu_fetch_manifest(manifest, sizeof manifest);
	if (fetched) nxu_config_set_checked(1);
	if (fetched && nxu_lookup(manifest, "ikemen", &info) && nxu_nro_version(self, cur, sizeof cur) &&
		nxu_cmp(info.version, cur) > 0) {
		consoleInit(NULL);
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		PadState pad;
		padInitializeDefault(&pad);
		printf("%s\n\nHay una version nueva del motor: %s (tienes %s).\n", IKEMEN_NX_VERSION_STR, info.version, cur);
		if (info.notes[0]) printf("%s\n", info.notes);
		printf("\nSe conservan el icono y el nombre de este juego.\n\n");
		printf("A: actualizar ahora   B: ahora no   X: no volver a preguntar\n");
		while (appletMainLoop()) {
			consoleUpdate(NULL);
			padUpdate(&pad);
			u64 d = padGetButtonsDown(&pad);
			if (d & HidNpadButton_B) break;
			if (d & HidNpadButton_X) { nxu_config_set_checked(0); break; }
			if (!(d & HidNpadButton_A)) continue;
			char tmp[FS_MAX_PATH + 16];
			snprintf(tmp, sizeof tmp, "%s.download", self);
			printf("\x1b[12;1HDescargando...\n");
			bool ok = nxu_download(info.url, tmp, update_progress) && nxu_install_nro(tmp, self, true, info.version);
			remove(tmp);
			printf(ok ? "\x1b[16;1HListo. Pulsa + y vuelve a abrir el juego.\n" :
				"\x1b[16;1HNo se pudo actualizar (se mantiene la version actual). Pulsa +.\n");
			while (appletMainLoop()) {
				consoleUpdate(NULL);
				padUpdate(&pad);
				if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
			}
			replaced = ok;
			break;
		}
		consoleExit(NULL);
	}
	curl_global_cleanup();
	socketExit();
	return replaced;
}

int main(int argc, char **argv) {
	if (!warn_applet_mode())
		return 0;
	if (argc > 0 && argv[0] && strchr(argv[0], '/') && check_update(argv[0]))
		return 0;
	// The engine data lives next to the .nro (hbloader passes its path in
	// argv[0]); fall back to the usual folder.
	char dir[FS_MAX_PATH] = "sdmc:/switch/ikemen";
	if (argc > 0 && argv[0] && strchr(argv[0], '/')) {
		strncpy(dir, argv[0], sizeof dir - 1);
		*strrchr(dir, '/') = 0;
	}
	snprintf(log_path, sizeof log_path, "%s/go_stdout.log", dir);
	remove(log_path);
	// Make the Go log land next to the .nro from the first write on.
	chdir(dir);
	GoMain(dir);
	show_log();
	return 0;
}

// FFmpeg falls back to reading PMCCNTR_EL0 for random seeds when
// /dev/urandom is missing, which faults on Horizon. Use the system RNG.
// Linked with -Wl,--wrap=av_get_random_seed,--wrap=av_random_bytes.
uint32_t __wrap_av_get_random_seed(void) {
	uint32_t seed;
	randomGet(&seed, sizeof seed);
	return seed;
}

int __wrap_av_random_bytes(uint8_t *buf, size_t len) {
	randomGet(buf, len);
	return 0;
}
