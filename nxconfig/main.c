// Ikemen NX Config: a text-mode Switch homebrew that edits the settings of
// the Ikemen installs on the SD card (sdmc:/switch/<folder>/), so they can be
// tuned without a PC: arcade ladder and bosses (select.def), fight rules,
// video, sound and performance (save/config.ini).
#include <switch.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define APP_TITLE "Ikemen NX Config v1.1"
#define MAX_INSTALLS 16
#define ORDERS 10       // arcade.maxmatches values written (orders 1..10)
#define BOSS_ORDER 10   // bosses that can appear in arcade
#define BOSS_OFF 11     // bosses left out of arcade (no matches for order 11)

// ---------------------------------------------------------------------------
// Text files kept as lines, written back with their original line endings.

typedef struct {
	char path[512];
	char **l;
	int n, cap;
	bool crlf, final_nl, dirty, loaded;
} TextFile;

static bool file_exists(const char *p) {
	struct stat st;
	return stat(p, &st) == 0;
}

static void tf_free(TextFile *f) {
	for (int i = 0; i < f->n; i++) free(f->l[i]);
	free(f->l);
	memset(f, 0, sizeof *f);
}

static void tf_insert(TextFile *f, int at, const char *s) {
	if (f->n == f->cap) {
		f->cap = f->cap ? f->cap * 2 : 256;
		f->l = realloc(f->l, f->cap * sizeof *f->l);
	}
	memmove(&f->l[at + 1], &f->l[at], (f->n - at) * sizeof *f->l);
	f->l[at] = strdup(s);
	f->n++;
}

static void tf_replace(TextFile *f, int i, const char *s) {
	// s may point into the line being replaced: copy it before freeing.
	char *copy = strdup(s);
	free(f->l[i]);
	f->l[i] = copy;
	f->dirty = true;
}

static void tf_delete(TextFile *f, int i) {
	free(f->l[i]);
	memmove(&f->l[i], &f->l[i + 1], (f->n - i - 1) * sizeof *f->l);
	f->n--;
	f->dirty = true;
}

static bool tf_load(TextFile *f, const char *path) {
	tf_free(f);
	snprintf(f->path, sizeof f->path, "%s", path);
	FILE *fp = fopen(path, "rb");
	if (!fp) return false;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char *buf = malloc(sz + 1);
	sz = fread(buf, 1, sz, fp);
	fclose(fp);
	buf[sz] = 0;
	f->crlf = strstr(buf, "\r\n") != NULL;
	f->final_nl = sz > 0 && buf[sz - 1] == '\n';
	char *p = buf;
	while (*p) {
		char *e = strchr(p, '\n');
		if (e) *e = 0;
		size_t len = strlen(p);
		if (len && p[len - 1] == '\r') p[len - 1] = 0;
		tf_insert(f, f->n, p);
		if (!e) break;
		p = e + 1;
	}
	free(buf);
	f->loaded = true;
	return true;
}

static bool tf_write(const TextFile *f, const char *path) {
	FILE *fp = fopen(path, "wb");
	if (!fp) return false;
	for (int i = 0; i < f->n; i++) {
		fputs(f->l[i], fp);
		if (i < f->n - 1 || f->final_nl) fputs(f->crlf ? "\r\n" : "\n", fp);
	}
	return fclose(fp) == 0;
}

// Saves a changed file; the first save keeps the original as <file>.bak.
static bool tf_save(TextFile *f) {
	if (!f->loaded || !f->dirty) return true;
	char bak[530];
	snprintf(bak, sizeof bak, "%s.bak", f->path);
	if (!file_exists(bak)) {
		TextFile orig = {0};
		if (tf_load(&orig, f->path)) tf_write(&orig, bak);
		tf_free(&orig);
	}
	if (!tf_write(f, f->path)) return false;
	f->dirty = false;
	return true;
}

// ---------------------------------------------------------------------------
// INI helpers (MUGEN/Ikemen style: [Section], key = value ; comment).

static void trim_into(char *dst, size_t n, const char *s, size_t len) {
	while (len && isspace((unsigned char)*s)) s++, len--;
	while (len && isspace((unsigned char)s[len - 1])) len--;
	if (len >= n) len = n - 1;
	memcpy(dst, s, len);
	dst[len] = 0;
}

// Section name if the line is a header, else NULL.
static bool line_section(const char *line, char *out, size_t n) {
	while (isspace((unsigned char)*line)) line++;
	if (*line != '[') return false;
	const char *e = strchr(line, ']');
	if (!e) return false;
	trim_into(out, n, line + 1, e - line - 1);
	return true;
}

// Key of a "key = value" line, else false.
static bool line_key(const char *line, char *out, size_t n) {
	const char *t = line;
	while (isspace((unsigned char)*t)) t++;
	if (*t == ';' || *t == '[' || !*t) return false;
	const char *eq = strchr(t, '=');
	const char *sc = strchr(t, ';');
	if (!eq || (sc && sc < eq)) return false;
	trim_into(out, n, t, eq - t);
	return true;
}

static void line_value(const char *line, char *out, size_t n) {
	const char *eq = strchr(line, '=');
	if (!eq) { *out = 0; return; }
	const char *v = eq + 1;
	const char *sc = strchr(v, ';');
	trim_into(out, n, v, sc ? (size_t)(sc - v) : strlen(v));
}

static int ini_find(const TextFile *f, const char *sec, const char *key) {
	char cur[128] = "", k[128];
	for (int i = 0; i < f->n; i++) {
		if (line_section(f->l[i], cur, sizeof cur)) continue;
		if (strcasecmp(cur, sec) == 0 && line_key(f->l[i], k, sizeof k) && strcasecmp(k, key) == 0)
			return i;
	}
	return -1;
}

static bool ini_get(const TextFile *f, const char *sec, const char *key, char *out, size_t n) {
	int i = ini_find(f, sec, key);
	if (i < 0) { *out = 0; return false; }
	line_value(f->l[i], out, n);
	return true;
}

static void ini_set(TextFile *f, const char *sec, const char *key, const char *val) {
	char line[512], cur[128] = "", k[128];
	int i = ini_find(f, sec, key);
	if (i >= 0) {
		// Keep the original key spelling and alignment, and any comment.
		const char *eq = strchr(f->l[i], '=');
		const char *sc = strchr(eq, ';');
		snprintf(line, sizeof line, "%.*s= %s%s%s", (int)(eq - f->l[i]), f->l[i], val,
			sc ? " " : "", sc ? sc : "");
		tf_replace(f, i, line);
		return;
	}
	snprintf(line, sizeof line, "%s = %s", key, val);
	int last = -1;
	for (int j = 0; j < f->n; j++) {
		if (line_section(f->l[j], cur, sizeof cur)) {
			if (last >= 0) break;
			if (strcasecmp(cur, sec) == 0) last = j;
			continue;
		}
		if (last >= 0 && line_key(f->l[j], k, sizeof k)) last = j;
	}
	if (last >= 0) {
		tf_insert(f, last + 1, line);
	} else {
		char hdr[160];
		snprintf(hdr, sizeof hdr, "[%s]", sec);
		tf_insert(f, f->n, "");
		tf_insert(f, f->n, hdr);
		tf_insert(f, f->n, line);
	}
	f->dirty = true;
}

// ---------------------------------------------------------------------------
// Installs on the SD card and their files.

typedef struct {
	char dir[300];       // sdmc:/switch/<folder>
	char folder[128];
	char title[128];     // motif [Info] name
	TextFile cfg;        // save/config.ini
	TextFile sel;        // the motif's select.def
	TextFile mot;        // the motif's system.def
	char selpath[512];
	char motifpath[512]; // the motif's system.def
} Install;

// Folder searched for installs (the self-test build points it elsewhere).
static const char *scan_root = "sdmc:/switch";

static Install inst[MAX_INSTALLS];
static int ninst;

static void find_select(Install *in) {
	char motif[256] = "data/system.def", path[600], rel[256] = "", dir[512];
	ini_get(&in->cfg, "Config", "Motif", motif, sizeof motif);
	snprintf(path, sizeof path, "%s/%s", in->dir, motif);
	snprintf(in->motifpath, sizeof in->motifpath, "%s", path);
	TextFile m = {0};
	if (tf_load(&m, path)) {
		ini_get(&m, "Info", "name", in->title, sizeof in->title);
		ini_get(&m, "Files", "select", rel, sizeof rel);
	}
	tf_free(&m);
	// Strip quotes from the motif name.
	size_t tl = strlen(in->title);
	if (tl >= 2 && in->title[0] == '"') { memmove(in->title, in->title + 1, tl); in->title[tl - 2] = 0; }
	if (!*rel) snprintf(rel, sizeof rel, "select.def");
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) *slash = 0;
	const char *cands[3] = { dir, NULL, NULL };
	char data[400];
	snprintf(data, sizeof data, "%s/data", in->dir);
	cands[1] = data;
	cands[2] = in->dir;
	for (int i = 0; i < 3; i++) {
		snprintf(in->selpath, sizeof in->selpath, "%s/%s", cands[i], rel);
		if (file_exists(in->selpath)) return;
	}
	in->selpath[0] = 0;
}

static void scan_installs(void) {
	DIR *d = opendir(scan_root);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) && ninst < MAX_INSTALLS) {
		if (e->d_name[0] == '.') continue;
		Install *in = &inst[ninst];
		memset(in, 0, sizeof *in);
		snprintf(in->dir, sizeof in->dir, "%s/%s", scan_root, e->d_name);
		char cfg[400], lua[400];
		snprintf(cfg, sizeof cfg, "%s/save/config.ini", in->dir);
		snprintf(lua, sizeof lua, "%s/external/script/main.lua", in->dir);
		if (!file_exists(cfg) || !file_exists(lua)) continue;
		snprintf(in->folder, sizeof in->folder, "%s", e->d_name);
		ninst++;
	}
	closedir(d);
}

static bool load_install(Install *in) {
	char cfg[400];
	snprintf(cfg, sizeof cfg, "%s/save/config.ini", in->dir);
	if (!tf_load(&in->cfg, cfg)) return false;
	find_select(in);
	if (in->selpath[0]) tf_load(&in->sel, in->selpath);
	tf_load(&in->mot, in->motifpath);
	return true;
}

// ---------------------------------------------------------------------------
// Console UI.

static PadState pad;
static u64 kdown, kheld;
static int rep_frames;

// Buttons pressed this frame, with auto-repeat for held directions.
static u64 input(void) {
	padUpdate(&pad);
	kdown = padGetButtonsDown(&pad);
	kheld = padGetButtons(&pad);
	u64 dirs = HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
	if (kheld & dirs) {
		rep_frames++;
		if (rep_frames > 18 && rep_frames % 4 == 0) return kdown | (kheld & dirs);
	} else {
		rep_frames = 0;
	}
	return kdown;
}

#define UP(k)    ((k) & HidNpadButton_AnyUp)
#define DOWN(k)  ((k) & HidNpadButton_AnyDown)
#define LEFT(k)  ((k) & HidNpadButton_AnyLeft)
#define RIGHT(k) ((k) & HidNpadButton_AnyRight)

static char status[160];

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
	printf(" %-46.46s %31.31s \x1b[0m", label, value);
}

// ---------------------------------------------------------------------------
// Settings stored in config.ini.

typedef struct {
	const char *label, *help, *sec, *key;
	int min, max, step;               // integer setting when nopt == 0
	int nopt;
	const char *optlabel[7];
	const char *optval[7];
	const char *key2;                 // optional second key set together
	const char *optval2[7];
} Setting;

static const Setting rules[] = {
	{ "Dificultad de la CPU", "Nivel de la IA de los rivales (1 = muy facil, 8 = muy dificil).",
		"Options", "Difficulty", 1, 8, 1 },
	{ "Vida (%)", "Vida inicial de cada personaje.", "Options", "Life", 10, 300, 10 },
	{ "Tiempo del round", "Segundos por round.", "Options", "Time", 0, 0, 0,
		6, { "30", "45", "60", "90", "99", "Infinito" }, { "30", "45", "60", "90", "99", "-1" } },
	{ "Rounds para ganar", "Rounds que hay que ganar para llevarse la pelea.", "Options", "Match.Wins", 1, 9, 1 },
	{ "Velocidad del juego", "0 = normal; cada paso sube o baja la velocidad.", "Options", "GameSpeed", -9, 9, 1 },
	{ "Creditos (continues)", "Continues disponibles en arcade.", "Options", "Credits", 0, 99, 1 },
	{ "Guardia automatica", "Cubrirse solo cuando no tocas botones.", "Options", "AutoGuard", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Dificultad progresiva (arcade)", "La CPU se vuelve mas dificil a medida que avanzas.", "Arcade", "AI.Ramping", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Colores de la CPU al azar", "La CPU elige un color de personaje al azar.", "Arcade", "AI.RandomColor", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
};

static const Setting media[] = {
	{ "Formato de la pelea", "Pantalla completa 16:9, el formato de cada escenario, o 4:3.", "Video", "FightAspectWidth", 0, 0, 0,
		3, { "Pantalla completa 16:9", "Segun el escenario", "4:3" }, { "0", "-1", "4" },
		"FightAspectHeight", { "0", "-1", "3" } },
	{ "Volumen general", "Volumen de todo el juego (0-100).", "Sound", "MasterVolume", 0, 100, 5 },
	{ "Volumen de la musica", "Musica de menus y peleas (0-100).", "Sound", "BGMVolume", 0, 100, 5 },
	{ "Volumen de efectos y voces", "Golpes, voces y efectos (0-100).", "Sound", "WavVolume", 0, 100, 5 },
	{ "Sonido estereo", "Efectos que se mueven entre parlante izquierdo y derecho.", "Sound", "StereoEffects", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Suavizado de imagenes RGB", "Filtro bilineal para sprites de color real.", "Video", "RGBSpriteBilinearFilter", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
};

static const Setting perf[] = {
	{ "Carga de retratos", "Al inicio: menus fluidos, arranque lento. Segundo plano: arranque rapido (rosters grandes).",
		"Config", "BootLoadingMode", 0, 0, 0,
		3, { "Todo al inicio", "Fondo, esperar en seleccion", "Segundo plano" }, { "0", "1", "2" } },
	{ "Musica en RAM", "Si: canciones enteras en memoria (puede agotar la RAM y cerrar el juego). No: desde la SD.",
		"Sound", "BGMRAMBuffer", 0, 0, 0, 2, { "No (recomendado)", "Si" }, { "0", "1" } },
	{ "Cargar pelea durante la pantalla VS", "Carga los personajes mientras se muestra la pantalla VS.",
		"Config", "VsScreenLoading", 0, 0, 0, 2, { "No", "Si" }, { "0", "1" } },
	{ "Max. efectos por personaje", "Menos = menos memoria; muy bajo puede ocultar efectos.", "Config", "ExplodMax", 64, 512, 32 },
	{ "Max. imagenes residuales", "Estelas de movimiento por personaje.", "Config", "AfterImageMax", 16, 512, 16 },
};

static int setting_index(const TextFile *cfg, const Setting *s) {
	char v[64], v2[64] = "";
	ini_get(cfg, s->sec, s->key, v, sizeof v);
	if (s->key2) ini_get(cfg, s->sec, s->key2, v2, sizeof v2);
	for (int i = 0; i < s->nopt; i++)
		if (atoi(v) == atoi(s->optval[i]) && (!s->key2 || atoi(v2) == atoi(s->optval2[i])) && *v) return i;
	return -1;
}

static void setting_text(const TextFile *cfg, const Setting *s, char *out, size_t n) {
	char v[64];
	bool has = ini_get(cfg, s->sec, s->key, v, sizeof v);
	if (s->nopt) {
		int i = setting_index(cfg, s);
		if (i >= 0) snprintf(out, n, "< %s >", s->optlabel[i]);
		else snprintf(out, n, "< %s >", has ? v : "(sin definir)");
	} else {
		snprintf(out, n, "< %s >", has ? v : "(sin definir)");
	}
}

static void setting_change(TextFile *cfg, const Setting *s, int dir) {
	char buf[32];
	if (s->nopt) {
		int i = setting_index(cfg, s);
		i = i < 0 ? 0 : (i + dir + s->nopt) % s->nopt;
		ini_set(cfg, s->sec, s->key, s->optval[i]);
		if (s->key2) ini_set(cfg, s->sec, s->key2, s->optval2[i]);
		return;
	}
	char v[64];
	int x = ini_get(cfg, s->sec, s->key, v, sizeof v) ? atoi(v) : s->min;
	x += dir * s->step;
	if (x < s->min) x = s->min;
	if (x > s->max) x = s->max;
	snprintf(buf, sizeof buf, "%d", x);
	ini_set(cfg, s->sec, s->key, buf);
}

static void settings_page(Install *in, TextFile *tf, const char *title, const Setting *set, int count) {
	int cur = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		header(title);
		printf("\x1b[4;2H\x1b[37m%s (%s)\x1b[0m", in->title[0] ? in->title : in->folder, in->folder);
		for (int i = 0; i < count; i++) {
			char val[64];
			setting_text(tf, &set[i], val, sizeof val);
			draw_row(6 + i, i == cur, set[i].label, val);
		}
		footer("Arriba/Abajo: elegir   Izq/Der: cambiar   B: volver   +: guardar", set[cur].help);
		consoleUpdate(NULL);
		u64 k = input();
		if (UP(k)) cur = (cur + count - 1) % count;
		if (DOWN(k)) cur = (cur + 1) % count;
		if (LEFT(k)) { setting_change(tf, &set[cur], -1); status[0] = 0; }
		if (RIGHT(k)) { setting_change(tf, &set[cur], 1); status[0] = 0; }
		if (kdown & HidNpadButton_Plus) set_status(tf_save(tf) ? "Guardado." : "ERROR al guardar");
		if (kdown & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------
// Arcade ladder: arcade.maxmatches and the order= of boss entries in select.def.

typedef struct {
	int line;
	char name[64];
	int order;
} CharEntry;

static CharEntry chars[2048];
static int nchars;
static int order_count[32];   // characters per order (1..31)

static int find_order(const char *line, const char **at) {
	// Last "order=<n>" in the part before the comment.
	char low[512];
	size_t n = strcspn(line, ";");
	if (n >= sizeof low) n = sizeof low - 1;
	for (size_t i = 0; i < n; i++) low[i] = tolower((unsigned char)line[i]);
	low[n] = 0;
	const char *p = low, *hit = NULL;
	while ((p = strstr(p, "order"))) {
		const char *q = p + 5;
		while (*q == ' ' || *q == '\t') q++;
		if (*q == '=') {
			q++;
			while (*q == ' ' || *q == '\t') q++;
			if (isdigit((unsigned char)*q)) hit = q;
		}
		p += 5;
	}
	if (!hit) return -1;
	if (at) *at = line + (hit - low);
	return atoi(hit);
}

static void scan_chars(Install *in) {
	nchars = 0;
	memset(order_count, 0, sizeof order_count);
	char cur[128] = "";
	for (int i = 0; i < in->sel.n && nchars < 2048; i++) {
		const char *l = in->sel.l[i];
		if (line_section(l, cur, sizeof cur)) continue;
		if (strcasecmp(cur, "Characters") != 0) continue;
		while (isspace((unsigned char)*l)) l++;
		if (!*l || *l == ';' || *l == '{' || *l == '}' || !strncasecmp(l, "slot", 4)) continue;
		char name[64];
		trim_into(name, sizeof name, l, strcspn(l, ",;"));
		if (!strcasecmp(name, "blank") || !strcasecmp(name, "randomselect") ||
			!strcasecmp(name, "emptyslot") || !strcasecmp(name, "dummyslot") || !strcasecmp(name, "skipslot")) continue;
		int o = find_order(l, NULL);
		if (o < 0) o = 1; // MUGEN default
		if (o < 32) order_count[o]++;
		CharEntry *c = &chars[nchars++];
		c->line = i;
		c->order = o;
		snprintf(c->name, sizeof c->name, "%s", name);
	}
}

static void set_char_order(Install *in, CharEntry *c, int order) {
	const char *at;
	const char *l = in->sel.l[c->line];
	if (find_order(l, &at) < 0) return;
	const char *end = at;
	while (isdigit((unsigned char)*end)) end++;
	char line[600];
	snprintf(line, sizeof line, "%.*s%d%s", (int)(at - l), l, order, end);
	tf_replace(&in->sel, c->line, line);
	c->order = order;
}

static int mm[ORDERS];

static void read_maxmatches(Install *in) {
	char v[256];
	memset(mm, 0, sizeof mm);
	if (!ini_get(&in->sel, "Options", "arcade.maxmatches", v, sizeof v)) return;
	char *p = v;
	for (int i = 0; i < ORDERS && *p; i++) {
		mm[i] = atoi(p);
		p = strchr(p, ',');
		if (!p) break;
		p++;
	}
}

static void write_maxmatches(Install *in) {
	char v[128];
	int n = 0;
	for (int i = 0; i < ORDERS; i++) n += snprintf(v + n, sizeof v - n, i ? ",%d" : "%d", mm[i]);
	ini_set(&in->sel, "Options", "arcade.maxmatches", v);
}

// Boss = a select.def entry that was ever ordered 10+ (on: 10, off: 11).
static bool is_boss(const CharEntry *c) { return c->order >= BOSS_ORDER; }

static void arcade_page(Install *in) {
	if (!in->sel.loaded) {
		set_status("No se encontro select.def para esta instalacion.");
		return;
	}
	scan_chars(in);
	read_maxmatches(in);
	int bosses[256], nb = 0;
	for (int i = 0; i < nchars && nb < 256; i++) if (is_boss(&chars[i])) bosses[nb++] = i;
	// Rows: orders 1..9, boss count (order 10), then one toggle per boss.
	int rows = 9 + 1 + nb, cur = 0, top = 0;
	const int visible = 30;
	status[0] = 0;
	while (appletMainLoop()) {
		int on = 0, total = 0;
		for (int i = 0; i < nb; i++) on += chars[bosses[i]].order == BOSS_ORDER;
		for (int i = 0; i < ORDERS; i++) total += mm[i];
		header("Arcade");
		printf("\x1b[4;2H\x1b[37mPeleas en arcade: \x1b[33;1m%d\x1b[0m\x1b[37m   (jefes activos: %d de %d)\x1b[0m", total, on, nb);
		if (cur < top) top = cur;
		if (cur >= top + visible) top = cur - visible + 1;
		for (int r = top; r < rows && r < top + visible; r++) {
			char label[96], val[64];
			if (r < 9) {
				snprintf(label, sizeof label, "Rivales de dificultad %d  (%d personajes)", r + 1, order_count[r + 1]);
				snprintf(val, sizeof val, "< %d >", mm[r]);
			} else if (r == 9) {
				snprintf(label, sizeof label, "Jefes finales (al azar entre los activos)");
				snprintf(val, sizeof val, "< %d >", mm[9]);
			} else {
				CharEntry *c = &chars[bosses[r - 10]];
				snprintf(label, sizeof label, "   Jefe: %s", c->name);
				snprintf(val, sizeof val, c->order == BOSS_ORDER ? "< Activo >" : "< Fuera >");
			}
			draw_row(6 + r - top, r == cur, label, val);
		}
		const char *help = cur < 9 ? "Cuantos rivales de esta dificultad (orden del select.def) te tocan, en orden creciente."
			: cur == 9 ? (nb ? "Cuantos jefes enfrentas al final. Se eligen al azar entre los activos."
			             : "Este juego no tiene jefes marcados (orden 10 o mas en select.def).")
			: "Activo: puede salir como jefe final. Fuera: no aparece en arcade.";
		footer("Arriba/Abajo: elegir   Izq/Der: cambiar   B: volver   +: guardar", help);
		consoleUpdate(NULL);
		u64 k = input();
		if (UP(k)) cur = (cur + rows - 1) % rows;
		if (DOWN(k)) cur = (cur + 1) % rows;
		int d = LEFT(k) ? -1 : RIGHT(k) ? 1 : 0;
		if (d) {
			status[0] = 0;
			if (cur < 10) {
				int max = cur < 9 ? order_count[cur + 1] : on;
				if (max > 20) max = 20;
				mm[cur] += d;
				if (mm[cur] < 0) mm[cur] = 0;
				if (mm[cur] > max) mm[cur] = max;
				write_maxmatches(in);
			} else {
				CharEntry *c = &chars[bosses[cur - 10]];
				set_char_order(in, c, c->order == BOSS_ORDER ? BOSS_OFF : BOSS_ORDER);
				int now = 0;
				for (int i = 0; i < nb; i++) now += chars[bosses[i]].order == BOSS_ORDER;
				if (mm[9] > now) { mm[9] = now; write_maxmatches(in); }
			}
		}
		if (kdown & HidNpadButton_Plus) set_status(tf_save(&in->sel) ? "Guardado." : "ERROR al guardar select.def");
		if (kdown & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------

static bool dirty(Install *in) { return in->cfg.dirty || in->sel.dirty || in->mot.dirty; }

static bool save_all(Install *in) {
	bool ok = tf_save(&in->cfg);
	ok = tf_save(&in->sel) && ok;
	ok = tf_save(&in->mot) && ok;
	return ok;
}

// Returns false if the user cancelled leaving.
static bool confirm_leave(Install *in) {
	if (!dirty(in)) return true;
	while (appletMainLoop()) {
		header("Cambios sin guardar");
		printf("\x1b[8;4H\x1b[33;1mHay cambios sin guardar en %s.\x1b[0m", in->folder);
		printf("\x1b[11;4H A: guardar y salir");
		printf("\x1b[12;4H X: salir sin guardar");
		printf("\x1b[13;4H B: volver");
		consoleUpdate(NULL);
		input();
		if (kdown & HidNpadButton_A) return save_all(in);
		if (kdown & HidNpadButton_X) return true;
		if (kdown & HidNpadButton_B) return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Select grid: some screenpacks ship alternative system.def files in a
// "slots" folder next to it, each with a different grid size. Applying one
// copies it over the motif's system.def.

typedef struct {
	char name[128];
	int rows, cols;
	bool current;
} SlotFile;

static unsigned char *read_all(const char *path, long *size) {
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	*size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	unsigned char *b = malloc(*size + 1);
	*size = fread(b, 1, *size, fp);
	fclose(fp);
	return b;
}

static bool copy_file(const char *from, const char *to) {
	long n;
	unsigned char *b = read_all(from, &n);
	if (!b) return false;
	FILE *fp = fopen(to, "wb");
	bool ok = fp && fwrite(b, 1, n, fp) == (size_t)n;
	if (fp) ok = fclose(fp) == 0 && ok;
	free(b);
	return ok;
}

// Cells the roster takes: each entry or "slot = { ... }" group in
// [Characters] is one cell.
static int roster_cells(const TextFile *sel) {
	char cur[128] = "";
	int n = 0;
	bool in_slot = false;
	for (int i = 0; i < sel->n; i++) {
		const char *l = sel->l[i];
		if (line_section(l, cur, sizeof cur)) continue;
		if (strcasecmp(cur, "Characters") != 0) continue;
		while (isspace((unsigned char)*l)) l++;
		if (!*l || *l == ';') continue;
		if (in_slot) { if (*l == '}') in_slot = false; continue; }
		if (!strncasecmp(l, "slot", 4) && strchr(l, '{')) { in_slot = !strchr(l, '}'); n++; continue; }
		n++;
	}
	return n;
}

static int slot_cmp(const void *a, const void *b) {
	const SlotFile *x = a, *y = b;
	return x->rows * x->cols - y->rows * y->cols;
}

static void slots_page(Install *in) {
	char dir[600];
	snprintf(dir, sizeof dir, "%s", in->motifpath);
	char *slash = strrchr(dir, '/');
	if (!slash) return;
	snprintf(slash, sizeof dir - (slash - dir), "/slots");
	DIR *d = opendir(dir);
	if (!d) {
		set_status("Este juego no trae grillas alternativas (carpeta slots).");
		return;
	}
	SlotFile sf[32];
	int ns = 0;
	long cursz = 0;
	unsigned char *curdata = read_all(in->motifpath, &cursz);
	struct dirent *e;
	while ((e = readdir(d)) && ns < 32) {
		size_t len = strlen(e->d_name);
		if (len < 5 || strcasecmp(e->d_name + len - 4, ".def")) continue;
		char path[800];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		SlotFile *s = &sf[ns];
		memset(s, 0, sizeof *s);
		snprintf(s->name, sizeof s->name, "%.*s", (int)(len - 4), e->d_name);
		TextFile t = {0};
		char v[32];
		if (tf_load(&t, path)) {
			if (ini_get(&t, "Select Info", "rows", v, sizeof v)) s->rows = atoi(v);
			if (ini_get(&t, "Select Info", "columns", v, sizeof v)) s->cols = atoi(v);
		}
		tf_free(&t);
		long n;
		unsigned char *b = read_all(path, &n);
		s->current = b && curdata && n == cursz && !memcmp(b, curdata, n);
		free(b);
		ns++;
	}
	closedir(d);
	free(curdata);
	qsort(sf, ns, sizeof *sf, slot_cmp);
	int need = in->sel.loaded ? roster_cells(&in->sel) : 0;
	int cur = 0;
	for (int i = 0; i < ns; i++) if (sf[i].current) cur = i;
	status[0] = 0;
	while (appletMainLoop()) {
		header("Grilla de seleccion");
		printf("\x1b[4;2H\x1b[37mTu roster ocupa \x1b[33;1m%d\x1b[0m\x1b[37m casillas.\x1b[0m", need);
		for (int i = 0; i < ns; i++) {
			char label[160], val[64];
			int cells = sf[i].rows * sf[i].cols;
			snprintf(label, sizeof label, "%s%s", sf[i].name, sf[i].current ? "  [actual]" : "");
			snprintf(val, sizeof val, "%dx%d = %d%s", sf[i].rows, sf[i].cols, cells, cells < need ? " !" : "");
			if (cells < need && i != cur) printf("\x1b[31m");
			draw_row(6 + i, i == cur, label, val);
		}
		const char *help = !ns ? "No hay grillas en la carpeta slots." :
			sf[cur].rows * sf[cur].cols < need ? "Esta grilla es mas chica que tu roster: no se veran todos los personajes." :
			"Filas x columnas de la pantalla de seleccion. A: aplicar (guarda system.def.bak).";
		footer("Arriba/Abajo: elegir   A: aplicar   B: volver", help);
		consoleUpdate(NULL);
		u64 k = input();
		if (ns && UP(k)) cur = (cur + ns - 1) % ns;
		if (ns && DOWN(k)) cur = (cur + 1) % ns;
		if (kdown & HidNpadButton_B) return;
		if (ns && (kdown & HidNpadButton_A) && !sf[cur].current && in->mot.dirty) {
			set_status("Primero guarda (+) los cambios de Pantallas: la grilla reemplaza el system.def.");
		} else if (ns && (kdown & HidNpadButton_A) && !sf[cur].current) {
			char from[800], bak[530];
			snprintf(from, sizeof from, "%s/%s.def", dir, sf[cur].name);
			snprintf(bak, sizeof bak, "%s.bak", in->motifpath);
			if (!file_exists(bak)) copy_file(in->motifpath, bak);
			if (copy_file(from, in->motifpath)) {
				for (int i = 0; i < ns; i++) sf[i].current = i == cur;
				tf_load(&in->mot, in->motifpath); // keep Pantallas in sync
				set_status("Grilla '%s' aplicada.", sf[cur].name);
			} else {
				set_status("ERROR al copiar la grilla.");
			}
		}
	}
}

#include "pages.h"

static void install_menu(Install *in) {
	if (!load_install(in)) {
		set_status("No se pudo leer %s/save/config.ini", in->dir);
		return;
	}
	const char *items[] = { "Arcade (rivales y jefes)", "Roster (personajes)", "Escenarios y musica",
		"Reglas de pelea", "Video y sonido", "Rendimiento", "Pantallas (system.def)",
		"Grilla de seleccion (slots)", "Editor de archivos", "Guardar cambios", "Volver" };
	const int count = 11, SAVE = 9;
	int cur = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		header(in->folder);
		printf("\x1b[4;2H\x1b[37m%s\x1b[0m", in->title[0] ? in->title : in->folder);
		printf("\x1b[5;2H\x1b[30;1mconfig: %s/save/config.ini\x1b[0m", in->dir);
		printf("\x1b[6;2H\x1b[30;1mselect: %s\x1b[0m", in->selpath[0] ? in->selpath : "(no encontrado)");
		for (int i = 0; i < count; i++) {
			const char *v = (i == SAVE && dirty(in)) ? "* hay cambios *" : "";
			draw_row(8 + i, i == cur, items[i], v);
		}
		footer("Arriba/Abajo: elegir   A: entrar   B: volver   +: guardar", "Los cambios se aplican la proxima vez que abras el juego.");
		consoleUpdate(NULL);
		u64 k = input();
		if (UP(k)) cur = (cur + count - 1) % count;
		if (DOWN(k)) cur = (cur + 1) % count;
		bool back = kdown & HidNpadButton_B;
		if (kdown & HidNpadButton_Plus) set_status(save_all(in) ? "Guardado." : "ERROR al guardar");
		if (kdown & HidNpadButton_A) {
			switch (cur) {
			case 0: arcade_page(in); break;
			case 1: roster_page(in); break;
			case 2: stages_page(in); break;
			case 3: settings_page(in, &in->cfg, "Reglas de pelea", rules, sizeof rules / sizeof *rules); break;
			case 4: settings_page(in, &in->cfg, "Video y sonido", media, sizeof media / sizeof *media); break;
			case 5: settings_page(in, &in->cfg, "Rendimiento", perf, sizeof perf / sizeof *perf); break;
			case 6: screens_page(in); break;
			case 7: slots_page(in); break;
			case 8: editor_menu(in); break;
			case 9: set_status(save_all(in) ? "Guardado." : "ERROR al guardar"); break;
			case 10: back = true; break;
			}
		}
		if (back && confirm_leave(in)) break;
	}
	tf_free(&in->cfg);
	tf_free(&in->sel);
	tf_free(&in->mot);
}

#ifdef SELFTEST
#include "selftest.h"
#else
int main(int argc, char **argv) {
	consoleInit(NULL);
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	scan_installs();
	// Read each install's title for the list.
	for (int i = 0; i < ninst; i++) {
		if (load_install(&inst[i])) {
			tf_free(&inst[i].cfg);
			tf_free(&inst[i].sel);
			tf_free(&inst[i].mot);
		}
	}
	int cur = 0;
	while (appletMainLoop()) {
		header(NULL);
		if (!ninst) {
			printf("\x1b[6;2HNo se encontraron instalaciones de Ikemen en sdmc:/switch/.");
			printf("\x1b[7;2H(Se busca save/config.ini y external/script/main.lua en cada carpeta.)");
		} else {
			printf("\x1b[4;2H\x1b[37mElige el juego a configurar:\x1b[0m");
			for (int i = 0; i < ninst; i++) {
				char label[200];
				snprintf(label, sizeof label, "%s", inst[i].title[0] ? inst[i].title : inst[i].folder);
				char val[160];
				snprintf(val, sizeof val, "switch/%s", inst[i].folder);
				draw_row(6 + i, i == cur, label, val);
			}
		}
		footer("Arriba/Abajo: elegir   A: entrar   +: salir", "Edita config.ini y select.def. La primera vez que guarda deja copias .bak.");
		consoleUpdate(NULL);
		u64 k = input();
		if (kdown & HidNpadButton_Plus) break;
		if (!ninst) continue;
		if (UP(k)) cur = (cur + ninst - 1) % ninst;
		if (DOWN(k)) cur = (cur + 1) % ninst;
		if (kdown & HidNpadButton_A) install_menu(&inst[cur]);
	}
	consoleExit(NULL);
	return 0;
}
#endif
