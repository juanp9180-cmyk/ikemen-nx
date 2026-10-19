// Import: installs characters, stages, music, screenpacks, lifebars and
// full MUGEN/Ikemen games from zip/rar/7z files in sdmc:/ikemen-import
// (where Ikemen NX Browser saves its downloads). Included by main.c after
// pages.h. Also the screenpack and lifebar selectors of an install.

#include <archive.h>
#include <archive_entry.h>

#define IMPORT_DIR "sdmc:/ikemen-import"

typedef enum { IT_CHAR, IT_STAGE, IT_MOTIF, IT_LIFEBAR, IT_MUSIC, IT_GAME } ItemType;

static const char *type_name[] = { "Personaje", "Escenario", "Screenpack", "Lifebars", "Musica", "Juego completo" };

typedef struct {
	ItemType type;
	char root[400];   // archive folder holding the item ("" = archive root)
	char def[200];    // .def file name inside root
	char name[128];   // destination folder / display name
	int nfiles;
	bool on;
} ImpItem;

typedef struct {
	char *path;       // normalized: '/' separators, no leading "./"
	long long size;
	bool dir;
} Ent;

static Ent *ents;
static int nents, ents_cap;
static ImpItem items_[256];
static int nitems;

// ---------------------------------------------------------------------------
// Paths and files.

static const char *strcasestr_ci(const char *hay, const char *needle) {
	size_t n = strlen(needle);
	for (; *hay; hay++) if (!strncasecmp(hay, needle, n)) return hay;
	return NULL;
}

static bool ends_with_ci(const char *s, const char *end) {
	size_t a = strlen(s), b = strlen(end);
	return b && a >= b && !strcasecmp(s + a - b, end);
}

static void norm_path(char *p) {
	for (char *c = p; *c; c++) if (*c == '\\') *c = '/';
	while (p[0] == '.' && p[1] == '/') memmove(p, p + 2, strlen(p + 2) + 1);
	while (p[0] == '/') memmove(p, p + 1, strlen(p));
	size_t n = strlen(p);
	while (n && p[n - 1] == '/') p[--n] = 0;
}

static void dir_of(const char *path, char *out, size_t n) {
	const char *s = strrchr(path, '/');
	snprintf(out, n, "%.*s", s ? (int)(s - path) : 0, path);
}

static const char *base_of(const char *path) {
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

// "a/b/c" under root "a/b" -> "c"; NULL if not under it.
static const char *under(const char *path, const char *root) {
	if (!*root) return path;
	size_t n = strlen(root);
	if (strncasecmp(path, root, n) || path[n] != '/') return NULL;
	return path + n + 1;
}

static void mkdir_p(const char *path) {
	char tmp[800];
	snprintf(tmp, sizeof tmp, "%s", path);
	for (char *p = tmp + 6; *p; p++) { // skip "sdmc:/"
		if (*p != '/') continue;
		*p = 0;
		mkdir(tmp, 0777);
		*p = '/';
	}
	mkdir(tmp, 0777);
}

static void archive_base(const char *file, char *out, size_t n) {
	snprintf(out, n, "%s", base_of(file));
	char *dot = strrchr(out, '.');
	if (dot) *dot = 0;
	for (char *p = out; *p; p++)
		if (strchr("\\/:*?\"<>|", *p)) *p = '_';
}

static bool is_audio(const char *p) {
	static const char *ext[] = { ".mp3", ".ogg", ".wav", ".flac", ".mod", ".xm", ".s3m", ".it", ".opus" };
	for (unsigned i = 0; i < sizeof ext / sizeof *ext; i++) if (has_ext(p, ext[i])) return true;
	return false;
}

static bool skip_always(const char *p) {
	return has_ext(p, ".exe") || has_ext(p, ".dll") || has_ext(p, ".lnk") || has_ext(p, ".url") ||
		!strcasecmp(base_of(p), "thumbs.db") || !strcasecmp(base_of(p), "desktop.ini") || contains_ci(p, "__MACOSX");
}

static struct archive *open_archive(const char *path) {
	struct archive *a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_zip(a);
	archive_read_support_format_rar(a);
	archive_read_support_format_rar5(a);
	archive_read_support_format_7zip(a);
	archive_read_support_format_tar(a);
	if (archive_read_open_filename(a, path, 64 * 1024) != ARCHIVE_OK) {
		set_status("No se pudo abrir: %s", archive_error_string(a));
		archive_read_free(a);
		return NULL;
	}
	return a;
}

static void entry_path(struct archive_entry *e, char *out, size_t n) {
	const char *p = archive_entry_pathname_utf8(e);
	if (!p) p = archive_entry_pathname(e);
	snprintf(out, n, "%s", p ? p : "");
	norm_path(out);
}

// ---------------------------------------------------------------------------
// Scan: list entries and classify the .def files by their sections.

static int find_item(ItemType t, const char *root) {
	for (int i = 0; i < nitems; i++) if (items_[i].type == t && !strcasecmp(items_[i].root, root)) return i;
	return -1;
}

static ImpItem *new_item(ItemType t, const char *root, const char *def, const char *fallback) {
	if (nitems >= 256) return NULL;
	ImpItem *it = &items_[nitems++];
	memset(it, 0, sizeof *it);
	it->type = t;
	it->on = true;
	snprintf(it->root, sizeof it->root, "%s", root);
	snprintf(it->def, sizeof it->def, "%s", def);
	const char *nm = *root ? base_of(root) : fallback;
	// "data" or "stages" folders say nothing: use the archive's name.
	if (!strcasecmp(nm, "data") || !strcasecmp(nm, "stages") || !strcasecmp(nm, "chars")) nm = fallback;
	snprintf(it->name, sizeof it->name, "%s", nm);
	return it;
}

static bool scan_archive(const char *file) {
	for (int i = 0; i < nents; i++) free(ents[i].path);
	nents = nitems = 0;
	struct archive *a = open_archive(file);
	if (!a) return false;
	char abase[200];
	archive_base(file, abase, sizeof abase);
	struct archive_entry *e;
	static char buf[256 * 1024 + 1];
	int r;
	while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
		char p[600];
		entry_path(e, p, sizeof p);
		if (!*p) continue;
		if (nents == ents_cap) ents = realloc(ents, (ents_cap = ents_cap ? ents_cap * 2 : 1024) * sizeof *ents);
		Ent *en = &ents[nents++];
		en->path = strdup(p);
		en->size = archive_entry_size(e);
		en->dir = archive_entry_filetype(e) == AE_IFDIR;
		if (en->dir || !has_ext(p, ".def") || en->size > 256 * 1024) {
			archive_read_data_skip(a);
			continue;
		}
		ssize_t got = 0, k;
		while (got < (ssize_t)sizeof buf - 1 && (k = archive_read_data(a, buf + got, sizeof buf - 1 - got)) > 0) got += k;
		buf[got] = 0;
		char root[400];
		dir_of(p, root, sizeof root);
		ItemType t;
		if (contains_ci(buf, "[stageinfo]")) t = IT_STAGE;
		else if (contains_ci(buf, "[title info]") || contains_ci(buf, "[select info]")) t = IT_MOTIF;
		else if (contains_ci(buf, "[lifebar]")) t = IT_LIFEBAR;
		else if (contains_ci(buf, "[files]") && contains_ci(buf, "cmd")) t = IT_CHAR;
		else continue;
		if (t == IT_STAGE) { new_item(t, root, base_of(p), abase); continue; }
		int ex = find_item(t, root);
		if (ex >= 0) {
			// Several .def files in one folder: prefer the one named like it.
			char want[200];
			snprintf(want, sizeof want, "%s.def", base_of(root));
			if (!strcasecmp(base_of(p), want)) snprintf(items_[ex].def, sizeof items_[ex].def, "%s", base_of(p));
			continue;
		}
		new_item(t, root, base_of(p), abase);
	}
	bool ok = r == ARCHIVE_EOF;
	if (!ok) set_status("Error leyendo el archivo: %s", archive_error_string(a));
	archive_read_free(a);

	// A full game: a folder with chars/ and data/ (with a system.def or mugen.cfg).
	for (int i = 0; i < nents; i++) {
		const char *b = base_of(ents[i].path);
		if (strcasecmp(b, "mugen.cfg") && strcasecmp(b, "system.def")) continue;
		char d[400], g[400];
		dir_of(ents[i].path, d, sizeof d);
		if (strcasecmp(base_of(d), "data")) continue;
		dir_of(d, g, sizeof g);
		char chars[500];
		snprintf(chars, sizeof chars, "%s%schars/", g, *g ? "/" : "");
		bool has_chars = false;
		for (int j = 0; j < nents && !has_chars; j++) has_chars = !strncasecmp(ents[j].path, chars, strlen(chars));
		if (!has_chars) continue;
		nitems = 0; // the game includes everything else
		ImpItem *it = new_item(IT_GAME, g, "", abase);
		if (it) for (int j = 0; j < nents; j++) it->nfiles += !ents[j].dir && under(ents[j].path, g) != NULL;
		return ok;
	}

	// Loose audio files not inside a character's or stage's folder: music.
	int music = 0;
	for (int i = 0; i < nents; i++) {
		if (ents[i].dir || !is_audio(ents[i].path)) continue;
		bool inside = false;
		for (int k = 0; k < nitems && !inside; k++)
			inside = items_[k].type == IT_CHAR && under(ents[i].path, items_[k].root);
		music += !inside;
	}
	if (music) {
		ImpItem *it = new_item(IT_MUSIC, "", "", abase);
		if (it) { it->nfiles = music; snprintf(it->name, sizeof it->name, "%d archivos de musica", music); }
	}
	for (int k = 0; k < nitems; k++) {
		if (items_[k].type == IT_MUSIC) continue; // counted above
		for (int i = 0; i < nents; i++) items_[k].nfiles += !ents[i].dir && under(ents[i].path, items_[k].root) != NULL;
	}
	return ok;
}

// ---------------------------------------------------------------------------
// Where each archive entry goes for each item (NULL: not part of it).


// Folder above "data/" when the item lives in data/<...>, else "".
static bool data_base(const char *root, char *base, size_t n) {
	const char *d = NULL;
	for (const char *p = root; (p = strcasestr_ci(p, "data")); p += 4) {
		if ((p == root || p[-1] == '/') && (p[4] == '/' || !p[4])) d = p;
	}
	if (!d) return false;
	snprintf(base, n, "%.*s", d == root ? 0 : (int)(d - root - 1), root);
	return true;
}

static bool dest_for(const ImpItem *it, const char *path, char *out, size_t n) {
	const char *rel;
	if (skip_always(path)) return false;
	switch (it->type) {
	case IT_CHAR:
		if (!(rel = under(path, it->root))) return false;
		snprintf(out, n, "chars/%s/%s", it->name, rel);
		return true;
	case IT_STAGE: {
		char sound[500], parent[400];
		dir_of(it->root, parent, sizeof parent);
		snprintf(sound, sizeof sound, "%s%ssound", parent, *parent ? "/" : "");
		if ((rel = under(path, sound)) && is_audio(path)) { snprintf(out, n, "sound/%s", rel); return true; }
		if (!(rel = under(path, it->root))) return false;
		// Loose stage files (archive root or a "stages" folder) go straight
		// into stages/, where their own paths ("stages/x.sff") expect them.
		if (!*it->root || !strcasecmp(base_of(it->root), "stages")) snprintf(out, n, "stages/%s", rel);
		else snprintf(out, n, "stages/%s/%s", base_of(it->root), rel);
		return true;
	}
	case IT_MOTIF: case IT_LIFEBAR: {
		const char *b = base_of(path);
		if (!strcasecmp(b, "select.def") || !strcasecmp(b, "mugen.cfg")) return false;
		char base[400];
		if (data_base(it->root, base, sizeof base)) {
			// Game-style layout: base/data/..., base/font/..., base/sound/...
			if (!(rel = under(path, base))) return false;
			if (!strncasecmp(rel, "chars/", 6) || !strncasecmp(rel, "stages/", 7)) return false;
			// A pack replacing data/system.def or data/fight.def moves its
			// data/ files to data/<name>/ so the game's own stay.
			if (!strncasecmp(rel, "data/", 5)) {
				const char *drel = rel + 5;
				char probe[400];
				const char *r2 = under(it->root, base);
				snprintf(probe, sizeof probe, "%s", r2 ? r2 : "");
				if (!strcasecmp(probe, "data")) { snprintf(out, n, "data/%s/%s", it->name, drel); return true; }
			}
			snprintf(out, n, "%s", rel);
			return true;
		}
		if (!(rel = under(path, it->root))) return false;
		snprintf(out, n, "data/%s/%s", it->name, rel);
		return true;
	}
	case IT_MUSIC:
		if (!is_audio(path)) return false;
		for (int k = 0; k < nitems; k++)
			if (items_[k].type == IT_CHAR && under(path, items_[k].root)) return false;
		snprintf(out, n, "sound/%s", base_of(path));
		return true;
	case IT_GAME:
		if (!(rel = under(path, it->root))) return false;
		if (!strcasecmp(base_of(path), "data.zip")) return false;
		snprintf(out, n, "%s", rel);
		return true;
	}
	return false;
}

// Destination of the item's main .def, relative to the game folder.
static void def_dest(const ImpItem *it, char *out, size_t n) {
	char p[700];
	snprintf(p, sizeof p, "%s%s%s", it->root, *it->root ? "/" : "", it->def);
	if (!dest_for(it, p, out, n)) out[0] = 0;
}

// ---------------------------------------------------------------------------
// Extraction.

static bool extract(const char *file, const char *gamedir, int *written) {
	struct archive *a = open_archive(file);
	if (!a) return false;
	struct archive_entry *e;
	static char buf[256 * 1024];
	int r, done = 0;
	*written = 0;
	u64 last = 0;
	while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
		char p[600], rel[700], dst[1200];
		entry_path(e, p, sizeof p);
		done++;
		bool take = false;
		if (archive_entry_filetype(e) != AE_IFDIR)
			for (int k = 0; k < nitems && !take; k++) take = items_[k].on && dest_for(&items_[k], p, rel, sizeof rel);
		if (!take) { archive_read_data_skip(a); continue; }
		snprintf(dst, sizeof dst, "%s/%s", gamedir, rel);
		char d[1200];
		dir_of(dst, d, sizeof d);
		mkdir_p(d);
		FILE *fp = fopen(dst, "wb");
		if (!fp) { archive_read_data_skip(a); continue; }
		ssize_t k;
		while ((k = archive_read_data(a, buf, sizeof buf)) > 0) fwrite(buf, 1, k, fp);
		fclose(fp);
		(*written)++;
		u64 t = armGetSystemTick();
		if (t - last > armGetSystemTickFreq() / 4) {
			last = t;
			printf("\x1b[12;4H\x1b[37mExtrayendo %d de %d archivos...            \x1b[0m", done, nents);
			printf("\x1b[13;4H\x1b[30;1m%-74.74s\x1b[0m", rel);
			consoleUpdate(NULL);
		}
	}
	bool ok = r == ARCHIVE_EOF;
	if (!ok) set_status("Error al extraer: %s", archive_error_string(a));
	archive_read_free(a);
	return ok;
}

// Copies src to dst unless dst exists (files) or recursively (dirs).
static int copy_missing(const char *src, const char *dst, bool recursive) {
	struct stat st;
	if (stat(src, &st) != 0) return 0;
	if (!S_ISDIR(st.st_mode)) {
		if (file_exists(dst)) return 0;
		return copy_file(src, dst) ? 1 : 0;
	}
	mkdir_p(dst);
	int n = 0;
	DIR *d = opendir(src);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char s[900], t[900];
		snprintf(s, sizeof s, "%s/%s", src, e->d_name);
		snprintf(t, sizeof t, "%s/%s", dst, e->d_name);
		if (is_dir(src, e)) { if (recursive) n += copy_missing(s, t, true); }
		else n += copy_missing(s, t, false);
	}
	if (d) closedir(d);
	return n;
}

// ---------------------------------------------------------------------------
// Known differences between MUGEN and Ikemen in full games' system.def:
// - duplicate keys: MUGEN keeps the last one, Ikemen the first -> comment
//   out the earlier ones;
// - WinMUGEN-only fade times in the thousands (a white screen for minutes);
// - winquote.textwrap = l (letter wrap) does not exist in Ikemen -> w.
static int fix_motif(TextFile *f) {
	int fixes = 0;
	char sec[128] = "", key[128], key2[128], s2[128];
	for (int i = 0; i < f->n; i++) {
		if (line_section(f->l[i], sec, sizeof sec)) continue;
		if (!line_key(f->l[i], key, sizeof key)) continue;
		char v[128], line[1200];
		line_value(f->l[i], v, sizeof v);
		// Only within this section: repeated section names are separate
		// background elements ("[VictoryBG background]" x5), and for other
		// repeated sections it is not known which one MUGEN uses.
		bool dup = false;
		for (int j = i + 1; j < f->n && !dup; j++) {
			if (line_section(f->l[j], s2, sizeof s2)) break;
			dup = line_key(f->l[j], key2, sizeof key2) && !strcasecmp(key, key2);
		}
		if (dup || ((!strcasecmp(key, "fadein.time") || !strcasecmp(key, "fadeout.time")) && atoi(v) >= 1000)) {
			snprintf(line, sizeof line, ";%s ;nxfix: %s", f->l[i], dup ? "duplicada (MUGEN usa la ultima)" : "solo WinMUGEN");
			tf_replace(f, i, line);
			fixes++;
		} else if (!strcasecmp(key, "winquote.textwrap") && !strcasecmp(v, "l")) {
			snprintf(line, sizeof line, "%.*s= w ;nxfix: era l (no existe en Ikemen)", (int)(strchr(f->l[i], '=') - f->l[i]), f->l[i]);
			tf_replace(f, i, line);
			fixes++;
		}
	}
	return fixes;
}

static const char *game_config =
	"; Ikemen NX: juego creado o importado por Ikemen NX Config.\n"
	"[Config]\n"
	"Motif = %s\n"
	"BootLoadingMode = 0\n"
	"\n[Video]\n"
	"RenderMode = OpenGL ES 3.2\n"
	"FightAspectWidth = 0\n"
	"FightAspectHeight = 0\n"
	"\n[Sound]\n"
	"BGMRAMBuffer = 0\n";

// Builds sdmc:/switch/<name> from the extracted game plus the engine files
// of an existing install. Returns a status message.
static void finish_game(const char *gamedir, const char *name, const Install *tpl, char *msg, size_t n) {
	// The engine: .nro, external/, the engine's own data and fonts.
	char s[900], d[900];
	int copied = 0;
	DIR *dd = opendir(tpl->dir);
	struct dirent *e;
	while (dd && (e = readdir(dd))) {
		if (!has_ext(e->d_name, ".nro")) continue;
		snprintf(s, sizeof s, "%s/%s", tpl->dir, e->d_name);
		snprintf(d, sizeof d, "%s/%s.nro", gamedir, name);
		copied += copy_file(s, d);
		break;
	}
	if (dd) closedir(dd);
	snprintf(s, sizeof s, "%s/external", tpl->dir); snprintf(d, sizeof d, "%s/external", gamedir);
	copied += copy_missing(s, d, true);
	snprintf(s, sizeof s, "%s/data", tpl->dir); snprintf(d, sizeof d, "%s/data", gamedir);
	copied += copy_missing(s, d, false);  // top-level engine files only (zss, common.*)
	snprintf(s, sizeof s, "%s/data/gofx", tpl->dir); snprintf(d, sizeof d, "%s/data/gofx", gamedir);
	copied += copy_missing(s, d, true);
	snprintf(s, sizeof s, "%s/font", tpl->dir); snprintf(d, sizeof d, "%s/font", gamedir);
	copied += copy_missing(s, d, true);
	// Config: the game's motif plus the Switch defaults.
	char motif[400] = "data/system.def";
	snprintf(s, sizeof s, "%s/save", gamedir);
	mkdir_p(s);
	snprintf(s, sizeof s, "%s/save/config.ini", gamedir);
	if (!file_exists(s)) {
		FILE *fp = fopen(s, "w");
		if (fp) { fprintf(fp, game_config, motif); fclose(fp); }
	}
	// MUGEN-only quirks of the game's screens.
	int fixes = 0;
	TextFile m = {0};
	snprintf(s, sizeof s, "%s/%s", gamedir, motif);
	if (tf_load(&m, s)) {
		fixes = fix_motif(&m);
		if (fixes) tf_save(&m);
		tf_free(&m);
	}
	snprintf(msg, n, "Juego listo en %s (%d archivos del motor, %d correcciones). Abrelo desde el menu homebrew.",
		gamedir + 5, copied, fixes);
}

// After extracting into an install: adds the characters and stages to its
// select.def (saved) and reports the screenpack / lifebars it got (paths
// relative to the game folder, "" if none).
static void register_items(Install *in, int *nc, int *ns, char *last_motif, char *last_life) {
	AddCharCtx ctx = { in, 0, "" };
	for (int k = 0; k < nitems; k++) {
		ImpItem *it = &items_[k];
		if (!it->on) continue;
		char rel[700];
		def_dest(it, rel, sizeof rel);
		if (!*rel) continue;
		if (it->type == IT_CHAR && in->sel.loaded) {
			// "kfm" if chars/kfm/kfm.def, else "kfm/other.def".
			char ref[300], want[300];
			snprintf(want, sizeof want, "chars/%s/%s.def", it->name, it->name);
			if (!strcasecmp(rel, want)) snprintf(ref, sizeof ref, "%s", it->name);
			else snprintf(ref, sizeof ref, "%s", rel + 6);
			*nc += add_char(&ctx, ref) != NULL;
		} else if (it->type == IT_STAGE && in->sel.loaded) {
			scan_stages(&in->sel);
			bool known = false;
			for (int i = 0; i < nstg && !known; i++) known = !strcasecmp(stg[i].def, rel);
			if (!known) { add_stage(&in->sel, rel); (*ns)++; }
		} else if (it->type == IT_MOTIF) {
			snprintf(last_motif, 400, "%s", rel);
		} else if (it->type == IT_LIFEBAR) {
			snprintf(last_life, 400, "%s", rel);
		}
	}
	if (in->sel.dirty) tf_save(&in->sel);
}

// ---------------------------------------------------------------------------
// New game from scratch: the clean Ikemen GO base bundled in this app's
// romfs (engine data, default "ikemen1" screenpack, Kung Fu Man) plus the
// .nro of an existing install, renamed for the homebrew menu.

// Writes `title` as the app name of every language in an .nro's NACP.
static bool nro_set_title(const char *path, const char *title) {
	FILE *f = fopen(path, "r+b");
	if (!f) return false;
	u32 nro_size = 0;
	u8 aset[0x38];
	bool ok = fseek(f, 0x18, SEEK_SET) == 0 && fread(&nro_size, 4, 1, f) == 1 &&
		fseek(f, nro_size, SEEK_SET) == 0 && fread(aset, sizeof aset, 1, f) == 1 && !memcmp(aset, "ASET", 4);
	if (ok) {
		u64 nacp_off, nacp_size;
		memcpy(&nacp_off, aset + 0x18, 8);
		memcpy(&nacp_size, aset + 0x20, 8);
		ok = nacp_size >= 0x3000;
		char name[0x200] = {0};
		snprintf(name, sizeof name, "%s", title);
		for (int lang = 0; ok && lang < 16; lang++) {
			ok = fseek(f, nro_size + nacp_off + lang * 0x300, SEEK_SET) == 0 && fwrite(name, sizeof name, 1, f) == 1;
		}
	}
	fclose(f);
	return ok;
}

// .nro of an install, preferring a plain ikemen.nro.
static bool find_engine_nro(char *out, size_t n) {
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < ninst; i++) {
			DIR *d = opendir(inst[i].dir);
			struct dirent *e;
			bool found = false;
			while (d && (e = readdir(d)) && !found) {
				if (!has_ext(e->d_name, ".nro")) continue;
				if (pass == 0 && strcasecmp(e->d_name, "ikemen.nro")) continue;
				snprintf(out, n, "%s/%s", inst[i].dir, e->d_name);
				found = true;
			}
			if (d) closedir(d);
			if (found) return true;
		}
	}
	return false;
}

static void create_game(const char *name);

static void create_game_page(void) {
	char name[128] = "Mi juego";
	if (kbd_input("Nombre del juego nuevo", name, name, sizeof name) && *name) create_game(name);
}

// Creates sdmc:/switch/<name>; the result goes to the status line.
static void create_game(const char *name) {
	char folder[128];
	snprintf(folder, sizeof folder, "%s", name);
	for (char *p = folder; *p; p++) if (strchr("\\/:*?\"<>|", *p)) *p = '_';
	char gamedir[400], nro[600], dst[700];
	snprintf(gamedir, sizeof gamedir, "%s/%s", scan_root, folder);
	if (file_exists(gamedir)) { set_status("Ya existe %s: elige otro nombre.", gamedir + 5); return; }
	if (!find_engine_nro(nro, sizeof nro)) {
		set_status("No encontre el motor (.nro) en ninguna instalacion. Instala el paquete de Ikemen NX.");
		return;
	}
	if (!file_exists("romfs:/base/data/ikemen1/system.def")) { set_status("Falta la base de Ikemen dentro de la app."); return; }
	show_busy("Crear juego nuevo", "Copiando la base de Ikemen GO (screenpack por defecto, Kung Fu Man)...");
	mkdir_p(gamedir);
	int copied = copy_missing("romfs:/base", gamedir, true);
	snprintf(dst, sizeof dst, "%s/%s.nro", gamedir, folder);
	bool ok = copy_file(nro, dst) && nro_set_title(dst, name);
	char cfg[500];
	snprintf(cfg, sizeof cfg, "%s/save", gamedir);
	mkdir_p(cfg);
	snprintf(cfg, sizeof cfg, "%s/save/config.ini", gamedir);
	FILE *fp = fopen(cfg, "w");
	if (fp) { fprintf(fp, game_config, "data/ikemen1/system.def"); fclose(fp); }
	if (!ok || !fp) set_status("ERROR creando %s (%d archivos copiados).", gamedir + 5, copied);
	else set_status("Listo: '%s' en %s. Importa contenido y aparece en el menu homebrew.", name, gamedir + 5);
}

// ---------------------------------------------------------------------------
// UI.

static int choose_install(const char *title, const char *what) {
	int cur = 0;
	while (appletMainLoop()) {
		header(title);
		printf("\x1b[4;2H\x1b[37m%s\x1b[0m", what);
		for (int i = 0; i < ninst; i++) draw_row(6 + i, i == cur, inst[i].title[0] ? inst[i].title : inst[i].folder, inst[i].folder);
		if (!ninst) printf("\x1b[7;4HNo hay instalaciones de Ikemen en sdmc:/switch/.");
		footer("A: elegir   B: cancelar", NULL);
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, ninst, 10);
		if ((kdown & HidNpadButton_A) && ninst) return cur;
		if (kdown & HidNpadButton_B) return -1;
	}
	return -1;
}

static void import_file(const char *file) {
	show_busy("Importar", "Leyendo el archivo comprimido...");
	if (!scan_archive(file)) return;
	if (!nitems) { set_status("No se encontro nada de Ikemen/MUGEN en %s.", base_of(file)); return; }
	bool game = items_[0].type == IT_GAME;
	int cur = 0, top = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		header("Importar");
		printf("\x1b[4;2H\x1b[37m%.70s  (%d archivos)\x1b[0m", base_of(file), nents);
		top = list_top(cur, top, 28);
		for (int r = top; r < nitems && r < top + 28; r++) {
			char label[200], val[64];
			snprintf(label, sizeof label, "[%s] %s", type_name[items_[r].type], items_[r].name);
			snprintf(val, sizeof val, "%s  %d arch.", items_[r].on ? "Si" : "No", items_[r].nfiles);
			draw_row(6 + r - top, r == cur, label, val);
		}
		footer(game ? "A: cambiar nombre   +: instalar como juego nuevo   B: cancelar" :
			"A: incluir/omitir   Y: renombrar   +: instalar en un juego   B: cancelar",
			game ? "Se crea switch/<nombre>/ con el motor de otra instalacion tuya." :
			"Personajes y escenarios se agregan al select.def del juego elegido.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, nitems, 20);
		if (kdown & HidNpadButton_B) return;
		if (((kdown & HidNpadButton_A) && game) || ((kdown & HidNpadButton_Y) && !game)) {
			char nm[128];
			snprintf(nm, sizeof nm, "%s", items_[cur].name);
			if (kbd_input("Nombre de la carpeta", nm, nm, sizeof nm) && *nm) {
				for (char *p = nm; *p; p++) if (strchr("\\/:*?\"<>|", *p)) *p = '_';
				snprintf(items_[cur].name, sizeof items_[cur].name, "%s", nm);
			}
		} else if (kdown & HidNpadButton_A) {
			items_[cur].on = !items_[cur].on;
		}
		if (kdown & HidNpadButton_Plus) break;
	}

	if (game) {
		ImpItem *g = &items_[0];
		int t = choose_install("Importar juego completo", "Elige de que instalacion copiar el motor (ikemen.nro y sus datos):");
		if (t < 0) return;
		char gamedir[400];
		snprintf(gamedir, sizeof gamedir, "%s/%s", scan_root, g->name);
		if (file_exists(gamedir)) { set_status("Ya existe %s: cambia el nombre (A).", gamedir + 5); return; }
		header("Importar juego completo");
		printf("\x1b[8;4H\x1b[33;1mExtrayendo %s en %s\x1b[0m", g->name, gamedir + 5);
		consoleUpdate(NULL);
		mkdir_p(gamedir);
		int written;
		if (!extract(file, gamedir, &written)) return;
		printf("\x1b[15;4H\x1b[37mCopiando el motor...\x1b[0m");
		consoleUpdate(NULL);
		char msg[300];
		finish_game(gamedir, g->name, &inst[t], msg, sizeof msg);
		set_status("%s", msg);
		ninst = 0;
		scan_installs();
		return;
	}

	int t = choose_install("Importar", "Elige el juego donde instalar:");
	if (t < 0) return;
	Install *in = &inst[t];
	if (!load_install(in)) { set_status("No se pudo leer el juego elegido."); return; }
	header("Importar");
	printf("\x1b[8;4H\x1b[33;1mInstalando en %s...\x1b[0m", in->folder);
	consoleUpdate(NULL);
	int written, nc = 0, ns = 0;
	char last_motif[400] = "", last_life[400] = "";
	bool ok = extract(file, in->dir, &written);
	if (ok) register_items(in, &nc, &ns, last_motif, last_life);
	if (!ok) { tf_free(&in->cfg); tf_free(&in->sel); tf_free(&in->mot); return; }
	// Offer to switch to an imported screenpack / lifebars right away.
	bool set_m = false, set_l = false;
	while ((*last_motif || *last_life) && appletMainLoop()) {
		header("Importar");
		printf("\x1b[6;4H\x1b[37m%d archivos instalados. %d personajes y %d escenarios agregados.\x1b[0m", written, nc, ns);
		if (*last_motif) printf("\x1b[9;4HA: usar el screenpack nuevo (%.50s)", last_motif);
		if (*last_life) printf("\x1b[10;4HX: usar las lifebars nuevas (%.50s)", last_life);
		printf("\x1b[12;4HB: dejar todo como estaba");
		footer("A / X: activar   B: terminar", NULL);
		consoleUpdate(NULL);
		input();
		if ((kdown & HidNpadButton_A) && *last_motif) set_m = true;
		if ((kdown & HidNpadButton_X) && *last_life) set_l = true;
		if (kdown & (HidNpadButton_A | HidNpadButton_X | HidNpadButton_B)) break;
	}
	if (set_m) { ini_set(&in->cfg, "Config", "Motif", last_motif); tf_save(&in->cfg); }
	if (set_l && in->mot.loaded) { ini_set(&in->mot, "Files", "fight", last_life); tf_save(&in->mot); }
	set_status("Listo: %d archivos, %d personajes, %d escenarios%s%s.", written, nc, ns,
		set_m ? ", screenpack activado" : "", set_l ? ", lifebars activadas" : "");
	tf_free(&in->cfg);
	tf_free(&in->sel);
	tf_free(&in->mot);
}

static void import_page(void) {
	int cur = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		char names[200][256];
		long long sizes[200];
		int n = 0;
		mkdir(IMPORT_DIR, 0777);
		DIR *d = opendir(IMPORT_DIR);
		struct dirent *e;
		while (d && (e = readdir(d)) && n < 200) {
			const char *nm = e->d_name;
			if (!has_ext(nm, ".zip") && !has_ext(nm, ".rar") && !has_ext(nm, ".7z") && !has_ext(nm, ".tar") && !has_ext(nm, ".gz")) continue;
			char p[600];
			struct stat st;
			snprintf(p, sizeof p, IMPORT_DIR "/%s", nm);
			if (stat(p, &st) != 0) continue;
			snprintf(names[n], sizeof names[0], "%s", nm);
			sizes[n++] = st.st_size;
		}
		if (d) closedir(d);
		if (cur >= n) cur = n ? n - 1 : 0;
		header("Importar descargas");
		printf("\x1b[4;2H\x1b[37m%s  (zip, rar, 7z)\x1b[0m", IMPORT_DIR);
		int top = list_top(cur, 0, 30);
		for (int r = top; r < n && r < top + 30; r++) {
			char sz[32];
			snprintf(sz, sizeof sz, "%.1f MB", sizes[r] / 1048576.0);
			draw_row(6 + r - top, r == cur, names[r], sz);
		}
		if (!n) printf("\x1b[7;4HNo hay archivos. Descargalos con Ikemen NX Browser o copialos a esa carpeta.");
		footer("A: importar   B: volver", "Personajes, escenarios, musica, screenpacks, lifebars y juegos completos.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, n, 20);
		if (kdown & HidNpadButton_B) return;
		if ((kdown & HidNpadButton_A) && n) {
			char p[600];
			snprintf(p, sizeof p, IMPORT_DIR "/%s", names[cur]);
			import_file(p);
		}
	}
}

// ---------------------------------------------------------------------------
// Screenpack and lifebar selectors of an install: every system.def /
// fight.def under data/ (two folder levels).

static int find_defs(const Install *in, const char *want, char out[][300], int max) {
	int n = 0;
	char base[400];
	snprintf(base, sizeof base, "%s/data", in->dir);
	char lvl1[64][200];
	int n1 = 0;
	snprintf(lvl1[n1++], sizeof lvl1[0], "%s", "");
	DIR *d = opendir(base);
	struct dirent *e;
	while (d && (e = readdir(d)) && n1 < 64)
		if (e->d_name[0] != '.' && is_dir(base, e)) snprintf(lvl1[n1++], sizeof lvl1[0], "%s", e->d_name);
	if (d) closedir(d);
	for (int i = 0; i < n1 && n < max; i++) {
		char dir[700], p[900];
		snprintf(dir, sizeof dir, "%s%s%s", base, *lvl1[i] ? "/" : "", lvl1[i]);
		// The file itself, and one more level down.
		snprintf(p, sizeof p, "%s/%s", dir, want);
		if (file_exists(p)) snprintf(out[n++], 300, "data/%s%s%s", lvl1[i], *lvl1[i] ? "/" : "", want);
		if (!*lvl1[i]) continue;
		DIR *sd = opendir(dir);
		struct dirent *f;
		while (sd && (f = readdir(sd)) && n < max) {
			if (f->d_name[0] == '.' || !is_dir(dir, f)) continue;
			snprintf(p, sizeof p, "%s/%s/%s", dir, f->d_name, want);
			if (file_exists(p)) snprintf(out[n++], 300, "data/%s/%s/%s", lvl1[i], f->d_name, want);
		}
		if (sd) closedir(sd);
	}
	return n;
}

// lifebars: false = screenpack (config.ini Motif), true = lifebars
// (system.def [Files] fight).
static void selector_page(Install *in, bool lifebars) {
	static char found[64][300];
	int n = find_defs(in, lifebars ? "fight.def" : "system.def", found, 64);
	int cur = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		char now[300] = "";
		if (lifebars) ini_get(&in->mot, "Files", "fight", now, sizeof now);
		else ini_get(&in->cfg, "Config", "Motif", now, sizeof now);
		header(lifebars ? "Lifebars" : "Screenpack");
		printf("\x1b[4;2H\x1b[37mActual: %.66s\x1b[0m", *now ? now : "(por defecto)");
		for (int i = 0; i < n; i++) {
			bool is_now = !strcasecmp(found[i], now) || ends_with_ci(found[i], now);
			draw_row(6 + i, i == cur, found[i], is_now ? "[actual]" : "");
		}
		if (!n) printf("\x1b[7;4HNo se encontraron %s en data/.", lifebars ? "fight.def" : "system.def");
		footer("A: usar este   B: volver", lifebars ? "Cambia [Files] fight en el system.def del screenpack activo." :
			"Cambia Motif en save/config.ini. Usa Importar para agregar screenpacks.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, n, 20);
		if (kdown & HidNpadButton_B) return;
		if ((kdown & HidNpadButton_A) && n) {
			if (lifebars) {
				if (!in->mot.loaded) { set_status("No se pudo leer el system.def."); continue; }
				ini_set(&in->mot, "Files", "fight", found[cur]);
				set_status(tf_save(&in->mot) ? "Lifebars cambiadas." : "ERROR al guardar system.def");
			} else {
				ini_set(&in->cfg, "Config", "Motif", found[cur]);
				if (!tf_save(&in->cfg)) { set_status("ERROR al guardar config.ini"); continue; }
				// The motif decides select.def and system.def: reload them.
				tf_free(&in->sel);
				tf_free(&in->mot);
				find_select(in);
				if (in->selpath[0]) tf_load(&in->sel, in->selpath);
				tf_load(&in->mot, in->motifpath);
				set_status("Screenpack cambiado.");
			}
		}
	}
}
