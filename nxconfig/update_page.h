// Updates from Ikemen NX Config: this app, Ikemen NX Browser and the
// engine .nro of every install (keeping each game's icon and name).
// Included by main.c after import.h. See common/nxupdate.h.

typedef struct {
	char label[96], path[600], app[16], cur[32];
	NxuInfo info;
	bool keep_assets, self;
} UpdItem;

static void upd_progress(double now, double total) {
	if (total > 0) printf("\x1b[20;4H\x1b[37m%.1f de %.1f MB     \x1b[0m", now / 1048576.0, total / 1048576.0);
	else printf("\x1b[20;4H\x1b[37m%.1f MB     \x1b[0m", now / 1048576.0);
	consoleUpdate(NULL);
}

static void upd_add(UpdItem *u, int *n, int max, const char *manifest, const char *app,
	const char *label, const char *path, bool keep, bool self) {
	if (*n >= max || !file_exists(path)) return;
	UpdItem *it = &u[*n];
	memset(it, 0, sizeof *it);
	if (!nxu_lookup(manifest, app, &it->info) || !nxu_nro_version(path, it->cur, sizeof it->cur)) return;
	if (nxu_cmp(it->info.version, it->cur) <= 0) return;
	snprintf(it->label, sizeof it->label, "%s", label);
	snprintf(it->path, sizeof it->path, "%s", path);
	snprintf(it->app, sizeof it->app, "%s", app);
	it->keep_assets = keep;
	it->self = self;
	(*n)++;
}

// automatic: called at start (silent when there is nothing new, offers to
// turn the check off). Returns true if this app's .nro was replaced.
static bool updates_page(const char *self, bool automatic) {
	if (automatic && !nxu_config_get("auto", 1)) return false;
	if (!automatic) show_busy("Actualizaciones", "Buscando versiones nuevas en GitHub...");
	static char manifest[16 * 1024];
	if (!nxu_fetch_manifest(manifest, sizeof manifest)) {
		if (!automatic) set_status("Sin conexion o GitHub no responde: no se pudo buscar actualizaciones.");
		return false;
	}
	UpdItem u[40];
	int n = 0;
	upd_add(u, &n, 40, manifest, "nxconfig", "Ikemen NX Config", self, false, true);
	upd_add(u, &n, 40, manifest, "nxbrowser", "Ikemen NX Browser", "sdmc:/switch/nxbrowser/nxbrowser.nro", false, false);
	for (int i = 0; i < ninst; i++) {
		DIR *d = opendir(inst[i].dir);
		struct dirent *e;
		while (d && (e = readdir(d))) {
			if (!has_ext(e->d_name, ".nro")) continue;
			char p[700], label[96];
			snprintf(p, sizeof p, "%s/%s", inst[i].dir, e->d_name);
			snprintf(label, sizeof label, "Motor de %s", inst[i].title[0] ? inst[i].title : inst[i].folder);
			upd_add(u, &n, 40, manifest, "ikemen", label, p, true, false);
		}
		if (d) closedir(d);
	}
	if (!n) {
		if (!automatic) set_status("Todo esta al dia.");
		return false;
	}
	status[0] = 0;
	while (appletMainLoop()) {
		header("Actualizaciones");
		printf("\x1b[4;2H\x1b[37mHay versiones nuevas en GitHub:\x1b[0m");
		for (int i = 0; i < n; i++) {
			char val[64];
			snprintf(val, sizeof val, "%s -> %s", u[i].cur, u[i].info.version);
			draw_row(6 + i, false, u[i].label, val);
		}
		if (u[0].info.notes[0]) printf("\x1b[%d;2H\x1b[37m%.78s\x1b[0m", 7 + n, u[0].info.notes);
		footer(automatic ? "A: actualizar todo   B: ahora no   X: no buscar al abrir" : "A: actualizar todo   B: volver",
			"Los juegos conservan su icono y su nombre. Hace falta internet.");
		consoleUpdate(NULL);
		input();
		if (kdown & HidNpadButton_B) return false;
		if (automatic && (kdown & HidNpadButton_X)) {
			nxu_config_set_checked(0);
			set_status("Busqueda automatica desactivada (puedes buscar desde el menu).");
			return false;
		}
		if (kdown & HidNpadButton_A) break;
	}
	// The app's own .nro last: its romfs must be closed before replacing it.
	int ok = 0, fail = 0;
	bool self_done = false;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < n; i++) {
			if (u[i].self != (pass == 1)) continue;
			printf("\x1b[18;4H\x1b[33;1mActualizando %-60.60s\x1b[0m", u[i].label);
			consoleUpdate(NULL);
			char tmp[700];
			snprintf(tmp, sizeof tmp, "%s.download", u[i].path);
			if (u[i].self) romfsExit();
			bool good = nxu_download(u[i].info.url, tmp, upd_progress) &&
				nxu_install_nro(tmp, u[i].path, u[i].keep_assets, u[i].info.version);
			remove(tmp);
			good ? ok++ : fail++;
			if (u[i].self) {
				if (good) self_done = true;
				else romfsInit();
			}
		}
	}
	nxu_config_set_checked(1);
	if (self_done) {
		header("Actualizaciones");
		printf("\x1b[6;4H\x1b[37m%d actualizados%s. Ikemen NX Config se actualizo:\x1b[0m", ok, fail ? ", algunos fallaron" : "");
		printf("\x1b[7;4H\x1b[37mvuelve a abrirla desde el menu homebrew.\x1b[0m");
		footer("+: salir", NULL);
		while (appletMainLoop()) {
			consoleUpdate(NULL);
			input();
			if (kdown & HidNpadButton_Plus) break;
		}
		return true;
	}
	set_status(fail ? "%d actualizados, %d fallaron (sin cambios en esos)." : "%d actualizados.", ok, fail);
	return false;
}
