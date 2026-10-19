// Ikemen NX Browser: opens the Switch's own web browser on MUGEN/Ikemen
// download sites and saves the files to sdmc:/ikemen-import/, where
// Ikemen NX Config can install them.
//
// The system browser cannot save files. Instead it is started with a
// "callback URL" prefix: when a download redirects to the file server
// (media.invisioncic.com for AndersonKenya1, an Invision Community site),
// the browser closes and hands the URL to this app, which downloads it
// with curl (TLS and certificates come from the system ssl service).
// Sites behind Cloudflare's browser check work this way because
// the check runs in the real browser; the file server itself is open.
#include <switch.h>
#include <curl/curl.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "nxupdate.h"

#define APP_TITLE "Ikemen NX Browser v1.1.0"
#define IMPORT_DIR "sdmc:/ikemen-import"
#define CONFIG_FILE IMPORT_DIR "/browser.ini"

// Sites. browser.ini (optional) can add more, one per line:
//   site = Name | start URL | callback URL prefix
typedef struct {
	char name[64], start[512], callback[512];
} Site;

static Site sites[16] = {
	{ "AndersonKenya1 (descargas)", "https://www.andersonkenya1.net/files/", "https://media.invisioncic.com/" },
};
static int nsites = 1;

static PadState pad;
static char status[200];

static void set_status(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(status, sizeof status, fmt, ap);
	va_end(ap);
}

static void header(const char *sub) {
	printf("\x1b[2J\x1b[1;1H\x1b[36;1m %s\x1b[0m", APP_TITLE);
	if (sub) printf("\x1b[37m  >  %s\x1b[0m", sub);
	printf("\n\x1b[36m%.80s\x1b[0m\n", "--------------------------------------------------------------------------------");
}

static void footer(const char *keys, const char *help) {
	printf("\x1b[40;1H\x1b[36m%.80s\x1b[0m", "--------------------------------------------------------------------------------");
	if (help && *help) printf("\x1b[41;2H\x1b[37m%.78s\x1b[0m", help);
	if (status[0]) printf("\x1b[43;2H\x1b[33;1m%.78s\x1b[0m", status);
	printf("\x1b[45;2H\x1b[32m%.78s\x1b[0m", keys);
}

static void draw_row(int row, bool sel, const char *label, const char *value) {
	printf("\x1b[%d;1H", row);
	if (sel) printf("\x1b[30;46m");
	printf(" %-50.50s %27.27s \x1b[0m", label, value);
}

static u64 buttons_down(void) {
	padUpdate(&pad);
	return padGetButtonsDown(&pad);
}

static void trim(char *s) {
	char *p = s;
	while (isspace((unsigned char)*p)) p++;
	memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void load_sites(void) {
	FILE *f = fopen(CONFIG_FILE, "r");
	if (!f) return;
	char line[1600];
	while (fgets(line, sizeof line, f) && nsites < 16) {
		trim(line);
		if (strncasecmp(line, "site", 4)) continue;
		char *eq = strchr(line, '=');
		if (!eq) continue;
		char *a = eq + 1, *b = strchr(a, '|'), *c = b ? strchr(b + 1, '|') : NULL;
		if (!b || !c) continue;
		*b++ = 0; *c++ = 0;
		Site *s = &sites[nsites++];
		snprintf(s->name, sizeof s->name, "%s", a); trim(s->name);
		snprintf(s->start, sizeof s->start, "%s", b); trim(s->start);
		snprintf(s->callback, sizeof s->callback, "%s", c); trim(s->callback);
	}
	fclose(f);
}

static bool kbd_input(const char *guide, const char *initial, char *out, size_t n) {
	SwkbdConfig kbd;
	if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
	swkbdConfigMakePresetDefault(&kbd);
	swkbdConfigSetGuideText(&kbd, guide);
	if (initial) swkbdConfigSetInitialText(&kbd, initial);
	swkbdConfigSetStringLenMax(&kbd, n - 1);
	Result rc = swkbdShow(&kbd, out, n);
	swkbdClose(&kbd);
	return R_SUCCEEDED(rc);
}

// ---------------------------------------------------------------------------
// System browser.

// Shows url; returns true and fills out_url when the page navigated to a
// URL starting with callback (a download). out_reason gets the exit reason.
static bool browse(const char *url, const char *callback, char *out_url, size_t n) {
	WebCommonConfig cfg;
	WebCommonReply reply;
	Result rc = webPageCreate(&cfg, url);
	if (R_FAILED(rc)) {
		set_status("No se pudo abrir el navegador (0x%x). Abre esta app en modo titulo (R).", rc);
		return false;
	}
	webConfigSetWhitelist(&cfg, "^http*");
	if (callback && *callback) webConfigSetCallbackUrl(&cfg, callback);
	webConfigSetDisplayUrlKind(&cfg, true);
	webConfigSetFooter(&cfg, true);
	webConfigSetPointer(&cfg, true);
	webConfigSetTouchEnabledOnContents(&cfg, true);
	rc = webConfigShow(&cfg, &reply);
	if (R_FAILED(rc)) {
		set_status("El navegador termino con error 0x%x.", rc);
		return false;
	}
	WebExitReason reason;
	webReplyGetExitReason(&reply, &reason);
	if (reason != WebExitReason_LastUrl) return false;
	size_t len = 0;
	webReplyGetLastUrl(&reply, out_url, n, &len);
	return len > 0 && out_url[0];
}

// ---------------------------------------------------------------------------
// Downloads.

typedef struct {
	FILE *fp;
	char name[256];   // from Content-Disposition
	curl_off_t total, now;
	u64 last_draw;
	bool cancel;
} Dl;

static size_t on_header(char *buf, size_t size, size_t n, void *ud) {
	Dl *d = ud;
	size_t len = size * n;
	if (len > 20 && !strncasecmp(buf, "content-disposition:", 20)) {
		char line[600];
		snprintf(line, sizeof line, "%.*s", (int)len, buf);
		char *f = NULL;
		for (char *p = line; *p && !f; p++) if (!strncasecmp(p, "filename=", 9)) f = p;
		if (f) {
			f += 9;
			if (*f == '"') f++;
			size_t k = strcspn(f, "\";\r\n");
			snprintf(d->name, sizeof d->name, "%.*s", (int)k, f);
		}
	}
	return len;
}

static size_t on_data(char *buf, size_t size, size_t n, void *ud) {
	return fwrite(buf, size, n, ((Dl *)ud)->fp);
}

static int on_progress(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	(void)ultotal; (void)ulnow;
	Dl *d = ud;
	d->total = dltotal;
	d->now = dlnow;
	u64 t = armGetSystemTick();
	if (t - d->last_draw > armGetSystemTickFreq() / 5) {
		d->last_draw = t;
		int pct = dltotal > 0 ? (int)(dlnow * 100 / dltotal) : 0;
		printf("\x1b[10;4H\x1b[37mDescargado: %.1f MB", dlnow / 1048576.0);
		if (dltotal > 0) printf(" de %.1f MB (%d%%)   ", dltotal / 1048576.0, pct);
		printf("\x1b[12;4H[");
		for (int i = 0; i < 50; i++) printf(i < pct / 2 ? "#" : " ");
		printf("]");
		consoleUpdate(NULL);
		if (buttons_down() & HidNpadButton_B) d->cancel = true;
	}
	return d->cancel ? 1 : 0;
}

// Invision stores files as "<name>.<32 hex hash>": drop the hash.
static void clean_name(char *name) {
	char *dot = strrchr(name, '.');
	if (dot && strlen(dot + 1) == 32) {
		bool hex = true;
		for (char *p = dot + 1; *p; p++) hex = hex && isxdigit((unsigned char)*p);
		if (hex) *dot = 0;
	}
	for (char *p = name; *p; p++)
		if (strchr("\\/:*?\"<>|", *p) || (unsigned char)*p < 32) *p = '_';
	if (!*name) strcpy(name, "descarga");
}

static void url_basename(const char *url, char *out, size_t n) {
	const char *end = url + strcspn(url, "?#");
	const char *s = end;
	while (s > url && s[-1] != '/') s--;
	snprintf(out, n, "%.*s", (int)(end - s), s);
	// Decode %XX.
	char *w = out;
	for (char *r = out; *r; r++) {
		if (r[0] == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
			char hx[3] = { r[1], r[2], 0 };
			*w++ = (char)strtol(hx, NULL, 16);
			r += 2;
		} else {
			*w++ = *r;
		}
	}
	*w = 0;
}

static bool download(const char *url, char *saved, size_t n) {
	mkdir(IMPORT_DIR, 0777);
	char tmp[300];
	snprintf(tmp, sizeof tmp, IMPORT_DIR "/descarga.part");
	Dl d = { 0 };
	d.fp = fopen(tmp, "wb");
	if (!d.fp) { set_status("No se pudo escribir en " IMPORT_DIR); return false; }
	header("Descargando");
	printf("\x1b[5;4H\x1b[37m%.74s\x1b[0m", url);
	printf("\x1b[7;4HB: cancelar");
	consoleUpdate(NULL);
	CURL *c = curl_easy_init();
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "IkemenNX-Browser/1.0 (Nintendo Switch)");
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, &d);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 256L * 1024);
	CURLcode res = curl_easy_perform(c);
	char *eff = NULL;
	curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
	char name[256];
	if (d.name[0]) snprintf(name, sizeof name, "%s", d.name);
	else url_basename(eff ? eff : url, name, sizeof name);
	curl_easy_cleanup(c);
	fclose(d.fp);
	if (res != CURLE_OK) {
		remove(tmp);
		set_status(d.cancel ? "Descarga cancelada." : "Error al descargar: %s", curl_easy_strerror(res));
		return false;
	}
	clean_name(name);
	char dst[600];
	snprintf(dst, sizeof dst, IMPORT_DIR "/%s", name);
	for (int k = 2; k < 100; k++) {
		struct stat st;
		if (stat(dst, &st) != 0) break;
		const char *ext = strrchr(name, '.');
		int base = ext ? (int)(ext - name) : (int)strlen(name);
		snprintf(dst, sizeof dst, IMPORT_DIR "/%.*s (%d)%s", base, name, k, ext ? ext : "");
	}
	if (rename(tmp, dst) != 0) { set_status("No se pudo renombrar la descarga."); return false; }
	snprintf(saved, n, "%s", dst + strlen(IMPORT_DIR) + 1);
	set_status("Guardado: %s (%.1f MB)", saved, d.now / 1048576.0);
	return true;
}

// ---------------------------------------------------------------------------
// Downloaded files.

static void files_page(void) {
	char names[256][256];
	long long sizes[256];
	int cur = 0, armed = -1;
	status[0] = 0;
	while (appletMainLoop()) {
		int n = 0;
		DIR *d = opendir(IMPORT_DIR);
		struct dirent *e;
		while (d && (e = readdir(d)) && n < 256) {
			if (e->d_name[0] == '.' || !strcasecmp(e->d_name, "browser.ini") || strstr(e->d_name, ".part")) continue;
			char p[600];
			struct stat st;
			snprintf(p, sizeof p, IMPORT_DIR "/%s", e->d_name);
			if (stat(p, &st) != 0 || S_ISDIR(st.st_mode)) continue;
			snprintf(names[n], sizeof names[0], "%s", e->d_name);
			sizes[n++] = st.st_size;
		}
		if (d) closedir(d);
		if (cur >= n) cur = n ? n - 1 : 0;
		header("Archivos descargados");
		printf("\x1b[4;2H\x1b[37m%s  (%d archivos)\x1b[0m", IMPORT_DIR, n);
		int top = cur >= 30 ? cur - 29 : 0;
		for (int r = top; r < n && r < top + 30; r++) {
			char sz[32];
			snprintf(sz, sizeof sz, "%.1f MB", sizes[r] / 1048576.0);
			draw_row(6 + r - top, r == cur, names[r], sz);
		}
		if (!n) printf("\x1b[7;4HTodavia no hay descargas.");
		footer("-: borrar (dos veces)   B: volver",
			armed >= 0 ? "Pulsa - otra vez para borrar el archivo." :
			"Instalalos en tus juegos con Ikemen NX Config > Importar.");
		consoleUpdate(NULL);
		u64 k = buttons_down();
		if (n && (k & HidNpadButton_AnyUp)) { cur = (cur + n - 1) % n; armed = -1; }
		if (n && (k & HidNpadButton_AnyDown)) { cur = (cur + 1) % n; armed = -1; }
		if (n && (k & HidNpadButton_Minus)) {
			if (armed == cur) {
				char p[600];
				snprintf(p, sizeof p, IMPORT_DIR "/%s", names[cur]);
				set_status(remove(p) == 0 ? "Borrado %s" : "No se pudo borrar %s", names[cur]);
				armed = -1;
			} else {
				armed = cur;
			}
		}
		if (k & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------

static void browse_site(const char *start, const char *callback) {
	char url[2048];
	const char *page = start;
	while (appletMainLoop()) {
		if (!browse(page, callback, url, sizeof url)) return; // closed by the user
		char saved[300];
		download(url, saved, sizeof saved);
		// Back to the browser, or to the menu.
		while (appletMainLoop()) {
			header("Descarga");
			printf("\x1b[6;4H\x1b[37mA: seguir navegando    B: volver al menu\x1b[0m");
			footer("A: navegador   B: menu", NULL);
			consoleUpdate(NULL);
			u64 k = buttons_down();
			if (k & HidNpadButton_B) return;
			if (k & HidNpadButton_A) break;
		}
	}
}

static void upd_progress(double now, double total) {
	if (total > 0) printf("\x1b[12;4H\x1b[37m%.1f de %.1f MB     \x1b[0m", now / 1048576.0, total / 1048576.0);
	consoleUpdate(NULL);
}

// Offers a newer Ikemen NX Browser from GitHub (see common/nxupdate.h).
// Returns true if this .nro was replaced: the app must be opened again.
static bool check_update(const char *self) {
	if (!nxu_config_get("auto", 1)) return false;
	static char manifest[16 * 1024];
	NxuInfo info;
	char cur[32];
	if (!nxu_fetch_manifest(manifest, sizeof manifest) || !nxu_lookup(manifest, "nxbrowser", &info) ||
		!nxu_nro_version(self, cur, sizeof cur) || nxu_cmp(info.version, cur) <= 0) return false;
	while (appletMainLoop()) {
		header("Actualizacion");
		printf("\x1b[5;4H\x1b[37mHay una version nueva: %s (tienes %s).\x1b[0m", info.version, cur);
		if (info.notes[0]) printf("\x1b[7;4H%.74s", info.notes);
		footer("A: actualizar   B: ahora no   X: no buscar al abrir", NULL);
		consoleUpdate(NULL);
		u64 k = buttons_down();
		if (k & HidNpadButton_B) return false;
		if (k & HidNpadButton_X) { nxu_config_set_checked(0); return false; }
		if (!(k & HidNpadButton_A)) continue;
		char tmp[700];
		snprintf(tmp, sizeof tmp, "%s.download", self);
		printf("\x1b[10;4H\x1b[33;1mDescargando...\x1b[0m");
		bool ok = nxu_download(info.url, tmp, upd_progress) && nxu_install_nro(tmp, self, false, info.version);
		remove(tmp);
		if (!ok) { set_status("No se pudo actualizar; sigues con la version actual."); return false; }
		header("Actualizacion");
		printf("\x1b[5;4H\x1b[37mListo. Vuelve a abrir Ikemen NX Browser desde el menu homebrew.\x1b[0m");
		footer("+: salir", NULL);
		while (appletMainLoop()) {
			consoleUpdate(NULL);
			if (buttons_down() & HidNpadButton_Plus) break;
		}
		return true;
	}
	return false;
}

int main(int argc, char **argv) {
	consoleInit(NULL);
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);

	socketInitializeDefault();
	curl_global_init(CURL_GLOBAL_DEFAULT);
	mkdir(IMPORT_DIR, 0777);
	load_sites();
	if (argc > 0 && argv[0] && strchr(argv[0], '/') && check_update(argv[0])) {
		curl_global_cleanup();
		socketExit();
		consoleExit(NULL);
		return 0;
	}

	AppletType at = appletGetAppletType();
	bool applet_mode = at != AppletType_Application && at != AppletType_SystemApplication;
	int cur = 0;
	while (appletMainLoop()) {
		int rows = nsites + 3;
		header(NULL);
		printf("\x1b[4;2H\x1b[37mDescarga personajes, escenarios, screenpacks y lifebars a %s\x1b[0m", IMPORT_DIR);
		if (applet_mode)
			printf("\x1b[5;2H\x1b[31;1mModo applet: el navegador no abre. Abre esta app manteniendo R al iniciar un juego.\x1b[0m");
		for (int i = 0; i < nsites; i++) draw_row(7 + i, cur == i, sites[i].name, "");
		draw_row(7 + nsites, cur == nsites, "Abrir otra direccion...", "");
		draw_row(8 + nsites, cur == nsites + 1, "Archivos descargados", "");
		draw_row(9 + nsites, cur == nsites + 2, "Salir", "");
		footer("A: elegir   +: salir",
			cur < nsites ? sites[cur].start :
			"Toca 'Download' en la pagina: el archivo se guarda solo y vuelves aqui.");
		consoleUpdate(NULL);
		u64 k = buttons_down();
		if (k & HidNpadButton_Plus) break;
		if (k & HidNpadButton_AnyUp) cur = (cur + rows - 1) % rows;
		if (k & HidNpadButton_AnyDown) cur = (cur + 1) % rows;
		if (!(k & HidNpadButton_A)) continue;
		status[0] = 0;
		if (cur < nsites) {
			browse_site(sites[cur].start, sites[cur].callback);
		} else if (cur == nsites) {
			char url[1024] = "https://";
			if (kbd_input("Direccion (https://...)", url, url, sizeof url))
				browse_site(url, sites[0].callback);
		} else if (cur == nsites + 1) {
			files_page();
		} else {
			break;
		}
	}
	curl_global_cleanup();
	socketExit();

	consoleExit(NULL);
	return 0;
}
