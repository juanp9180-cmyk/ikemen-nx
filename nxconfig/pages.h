// Editors for select.def (roster and stages), the motif's system.def
// (screens) and any text file (line editor). Included by main.c.
//
// Disabled entries are kept in the file so they can be enabled again:
// a character becomes "dummyslot ;nxoff: <original line>" (the cell stays,
// so the rest of the grid does not move), and a stage or a slot member is
// commented out as ";nxoff: <original line>".

#define NXOFF ";nxoff: "
#define NXHIDE ";nxhide: "

// ---------------------------------------------------------------------------
// Small helpers.

// Software keyboard. Returns false if cancelled.
static bool kbd_input(const char *guide, const char *initial, char *out, size_t n) {
	SwkbdConfig kbd;
	if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
	swkbdConfigMakePresetDefault(&kbd);
	swkbdConfigSetGuideText(&kbd, guide);
	if (initial) swkbdConfigSetInitialText(&kbd, initial);
	swkbdConfigSetStringLenMax(&kbd, n > 1001 ? 1000 : n - 1);
	Result rc = swkbdShow(&kbd, out, n);
	swkbdClose(&kbd);
	return R_SUCCEEDED(rc);
}

static bool contains_ci(const char *hay, const char *needle) {
	size_t n = strlen(needle);
	if (!n) return true;
	for (; *hay; hay++)
		if (!strncasecmp(hay, needle, n)) return true;
	return false;
}

static const char *skip_ws(const char *s) {
	while (isspace((unsigned char)*s)) s++;
	return s;
}

static bool starts_off(const char *l) { return !strncmp(l, NXOFF, strlen(NXOFF)); }

// First comma-separated token of an entry, trimmed.
static void entry_name(const char *line, char *out, size_t n) {
	line = skip_ws(line);
	trim_into(out, n, line, strcspn(line, ",;"));
}

static bool is_placeholder(const char *name) {
	return !strcasecmp(name, "blank") || !strcasecmp(name, "dummyslot") || !strcasecmp(name, "emptyslot") ||
		!strcasecmp(name, "skipslot") || !strcasecmp(name, "randomselect");
}

static bool is_empty_cell_name(const char *name) {
	return is_placeholder(name) && strcasecmp(name, "randomselect");
}

static bool has_ext(const char *name, const char *ext) {
	size_t a = strlen(name), b = strlen(ext);
	return a > b && !strcasecmp(name + a - b, ext);
}

// d_type may be DT_UNKNOWN on some file systems: fall back to stat.
static bool is_dir(const char *parent, const struct dirent *e) {
	if (e->d_type == DT_DIR) return true;
	if (e->d_type != DT_UNKNOWN) return false;
	char p[900];
	struct stat st;
	snprintf(p, sizeof p, "%s/%s", parent, e->d_name);
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

// Lines [first, last) of a select.def section, or false if it is missing.
static bool section_range(const TextFile *f, const char *sec, int *first, int *last) {
	char cur[128];
	*first = -1;
	for (int i = 0; i < f->n; i++) {
		if (!line_section(f->l[i], cur, sizeof cur)) continue;
		if (*first >= 0) { *last = i; return true; }
		if (!strcasecmp(cur, sec)) *first = i + 1;
	}
	*last = f->n;
	return *first >= 0;
}

// Scrolling list window: keeps cur visible in `visible` rows.
static int list_top(int cur, int top, int visible) {
	if (cur < top) top = cur;
	if (cur >= top + visible) top = cur - visible + 1;
	return top < 0 ? 0 : top;
}

// Up/down by one, L/R by a page. Returns the new cursor.
static int list_move(u64 k, int cur, int count, int page) {
	if (!count) return 0;
	if (UP(k)) cur = (cur + count - 1) % count;
	if (DOWN(k)) cur = (cur + 1) % count;
	if (kdown & HidNpadButton_L) cur = cur - page < 0 ? 0 : cur - page;
	if (kdown & HidNpadButton_R) cur = cur + page >= count ? count - 1 : cur + page;
	return cur;
}

static void show_busy(const char *title, const char *msg) {
	header(title);
	printf("\x1b[8;4H\x1b[33;1m%s\x1b[0m", msg);
	consoleUpdate(NULL);
}

static int cmp_str(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

typedef struct {
	char ref[200];   // what goes in select.def
	bool added;
} NewItem;

// List of new items (characters or stages) to add, one at a time.
// add() returns a status message, or NULL if it failed.
static void new_items_page(const char *title, const char *info, NewItem *items, int n,
	const char *(*add)(void *ctx, const char *ref), void *ctx, TextFile *save) {
	int cur = 0, top = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		header(title);
		printf("\x1b[4;2H\x1b[37m%d %s\x1b[0m", n, info);
		top = list_top(cur, top, 30);
		for (int r = top; r < n && r < top + 30; r++)
			draw_row(6 + r - top, r == cur, items[r].ref, items[r].added ? "agregado" : "");
		if (!n) printf("\x1b[7;4HNo hay nada nuevo para agregar.");
		footer("A: agregar   L/R: pagina   B: volver   +: guardar", NULL);
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, n, 20);
		if ((kdown & HidNpadButton_A) && n && !items[cur].added) {
			const char *msg = add(ctx, items[cur].ref);
			items[cur].added = msg != NULL;
			set_status("%s", msg ? msg : "No se pudo agregar.");
			if (msg && cur + 1 < n) cur++;
		}
		if (kdown & HidNpadButton_Plus) set_status(tf_save(save) ? "Guardado." : "ERROR al guardar");
		if (kdown & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------
// Roster: the cells of [Characters].

typedef struct {
	int start, end;     // lines [start, end] of the entry (a slot spans several)
	bool slot, off;
	char name[96];
} Cell;

static Cell *cells;
static int ncells, cells_cap;

static void scan_cells(const TextFile *sel) {
	ncells = 0;
	int first, last;
	if (!section_range(sel, "Characters", &first, &last)) return;
	for (int i = first; i < last; i++) {
		const char *l = skip_ws(sel->l[i]);
		if (!*l || *l == ';') continue;
		if (ncells == cells_cap) {
			cells_cap = cells_cap ? cells_cap * 2 : 512;
			cells = realloc(cells, cells_cap * sizeof *cells);
		}
		Cell *c = &cells[ncells++];
		memset(c, 0, sizeof *c);
		c->start = c->end = i;
		if (!strncasecmp(l, "slot", 4) && strchr(l, '{')) {
			c->slot = true;
			int nm = 0;
			for (int j = i + 1; j < last; j++) {
				const char *m = skip_ws(sel->l[j]);
				c->end = j;
				if (*m == '}') break;
				if (!*m || *m == ';') continue;
				char nmb[64];
				entry_name(m, nmb, sizeof nmb);
				size_t len = strlen(c->name);
				snprintf(c->name + len, sizeof c->name - len, "%s%s", nm++ ? " + " : "", nmb);
			}
			if (!nm) snprintf(c->name, sizeof c->name, "(grupo vacio)");
			i = c->end;
			continue;
		}
		const char *off = strstr(l, NXOFF);
		if (off && !strncasecmp(l, "dummyslot", 9)) {
			c->off = true;
			entry_name(off + strlen(NXOFF), c->name, sizeof c->name);
		} else {
			entry_name(l, c->name, sizeof c->name);
		}
	}
}

static void toggle_cell(TextFile *sel, Cell *c) {
	char line[1100];
	const char *l = sel->l[c->start];
	if (c->off) {
		tf_replace(sel, c->start, strstr(l, NXOFF) + strlen(NXOFF));
		set_status("%s activado.", c->name);
	} else if (is_placeholder(c->name)) {
		set_status("Casilla vacia: usa - para poner aqui un personaje nuevo.");
	} else {
		snprintf(line, sizeof line, "dummyslot " NXOFF "%s", skip_ws(l));
		tf_replace(sel, c->start, line);
		set_status("%s desactivado (la casilla queda vacia).", c->name);
	}
}

// Swaps cells a and b (a before b): lines between them stay in place.
static void swap_cells(TextFile *sel, const Cell *a, const Cell *b) {
	int s = a->start, e = b->end, n = e - s + 1, k = 0;
	char **tmp = malloc(n * sizeof *tmp);
	for (int i = b->start; i <= b->end; i++) tmp[k++] = sel->l[i];
	for (int i = a->end + 1; i < b->start; i++) tmp[k++] = sel->l[i];
	for (int i = a->start; i <= a->end; i++) tmp[k++] = sel->l[i];
	memcpy(&sel->l[s], tmp, n * sizeof *tmp);
	free(tmp);
	sel->dirty = true;
}

// Members of a slot group: toggle each one (at least one stays on).
static void slot_page(Install *in, int ci) {
	int cur = 0, top = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		scan_cells(&in->sel);
		if (ci >= ncells || !cells[ci].slot) return;
		Cell c = cells[ci];
		int lines[64], n = 0, on = 0;
		for (int j = c.start + 1; j < c.end && n < 64; j++) {
			const char *m = skip_ws(in->sel.l[j]);
			bool off = starts_off(m);
			if (!*m || (*m == ';' && !off)) continue;
			lines[n++] = j;
			on += !off;
		}
		if (cur >= n) cur = n ? n - 1 : 0;
		header("Roster > grupo");
		printf("\x1b[4;2H\x1b[37mCasilla %d: comparten la casilla (L/ZL los cambian en el juego).\x1b[0m", ci + 1);
		top = list_top(cur, top, 30);
		for (int r = top; r < n && r < top + 30; r++) {
			const char *m = skip_ws(in->sel.l[lines[r]]);
			bool off = starts_off(m);
			char nm[64];
			entry_name(off ? m + strlen(NXOFF) : m, nm, sizeof nm);
			draw_row(6 + r - top, r == cur, nm, off ? "< Fuera >" : "< Activo >");
		}
		footer("A: activar/quitar   B: volver   +: guardar", "Los quitados quedan como comentario en el select.def.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, n, 20);
		if ((kdown & HidNpadButton_A) && n) {
			const char *m = skip_ws(in->sel.l[lines[cur]]);
			char line[1100];
			if (starts_off(m)) {
				tf_replace(&in->sel, lines[cur], m + strlen(NXOFF));
			} else if (on <= 1) {
				set_status("Debe quedar al menos un personaje activo en la casilla.");
			} else {
				snprintf(line, sizeof line, NXOFF "%s", m);
				tf_replace(&in->sel, lines[cur], line);
			}
		}
		if (kdown & HidNpadButton_Plus) set_status(tf_save(&in->sel) ? "Guardado." : "ERROR al guardar select.def");
		if (kdown & HidNpadButton_B) return;
	}
}

// Lower-case folder part of a char reference ("kfm", "kfm/kfm720.def").
static void ref_folder(const char *name, char *out, size_t n) {
	size_t len = strcspn(name, "/\\");
	if (len >= n) len = n - 1;
	for (size_t i = 0; i < len; i++) out[i] = tolower((unsigned char)name[i]);
	out[len] = 0;
	// "kfm.def" without a folder: the folder is the file name.
	if (!strchr(name, '/') && has_ext(out, ".def")) out[len - 4] = 0;
}

// Character folders in chars/ that select.def does not mention.
static NewItem *find_new_chars(Install *in, int *count) {
	int first, last, nref = 0, capref = 512;
	char **refs = malloc(capref * sizeof *refs);
	if (section_range(&in->sel, "Characters", &first, &last)) {
		for (int i = first; i < last; i++) {
			const char *l = skip_ws(in->sel.l[i]);
			const char *off = strstr(l, NXOFF);
			if (off) l = off + strlen(NXOFF);
			if (!*l || *l == ';' || *l == '}' || (!strncasecmp(l, "slot", 4) && strchr(l, '{'))) continue;
			char nm[200], f[200];
			entry_name(l, nm, sizeof nm);
			ref_folder(nm, f, sizeof f);
			if (nref == capref) refs = realloc(refs, (capref *= 2) * sizeof *refs);
			refs[nref++] = strdup(f);
		}
	}
	qsort(refs, nref, sizeof *refs, cmp_str);
	char dir[400];
	snprintf(dir, sizeof dir, "%s/chars", in->dir);
	NewItem *items = NULL;
	int n = 0, cap = 0;
	DIR *d = opendir(dir);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.' || !is_dir(dir, e)) continue;
		char low[200], *key = low;
		ref_folder(e->d_name, low, sizeof low);
		if (bsearch(&key, refs, nref, sizeof *refs, cmp_str)) continue;
		char path[800], ref[200] = "";
		snprintf(path, sizeof path, "%s/%s/%s.def", dir, e->d_name, e->d_name);
		if (file_exists(path)) {
			snprintf(ref, sizeof ref, "%s", e->d_name);
		} else {
			// Any .def in the folder.
			char sub[600];
			snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
			DIR *sd = opendir(sub);
			struct dirent *f;
			while (sd && (f = readdir(sd))) {
				if (has_ext(f->d_name, ".def")) {
					snprintf(ref, sizeof ref, "%s/%s", e->d_name, f->d_name);
					break;
				}
			}
			if (sd) closedir(sd);
		}
		if (!*ref) continue;
		if (n == cap) items = realloc(items, (cap = cap ? cap * 2 : 64) * sizeof *items);
		memset(&items[n], 0, sizeof *items);
		snprintf(items[n].ref, sizeof items[n].ref, "%s", ref);
		n++;
	}
	if (d) closedir(d);
	for (int i = 0; i < nref; i++) free(refs[i]);
	free(refs);
	*count = n;
	return items;
}

typedef struct {
	Install *in;
	int from;   // cell where the roster cursor was
	char msg[200];
} AddCharCtx;

// Puts a new character in the first empty cell from the cursor on (wrapping
// around), or at the end of the roster (before a trailing randomselect).
static const char *add_char(void *vctx, const char *ref) {
	AddCharCtx *ctx = vctx;
	TextFile *sel = &ctx->in->sel;
	char line[300];
	snprintf(line, sizeof line, "%s, random", ref);
	scan_cells(sel);
	for (int k = 0; k < ncells; k++) {
		int i = (ctx->from + k) % ncells;
		Cell *c = &cells[i];
		if (!c->slot && !c->off && is_empty_cell_name(c->name)) {
			tf_replace(sel, c->start, line);
			snprintf(ctx->msg, sizeof ctx->msg, "%s agregado en la casilla %d.", ref, i + 1);
			ctx->from = i + 1;
			return ctx->msg;
		}
	}
	int at;
	if (ncells) {
		Cell *lc = &cells[ncells - 1];
		at = lc->end + 1;
		if (!lc->slot && !strcasecmp(lc->name, "randomselect")) at = lc->start;
	} else {
		int first, last;
		if (!section_range(sel, "Characters", &first, &last)) return NULL;
		at = first;
	}
	tf_insert(sel, at, line);
	sel->dirty = true;
	snprintf(ctx->msg, sizeof ctx->msg, "%s agregado al final (casilla %d).", ref, ncells + 1);
	return ctx->msg;
}

static void roster_page(Install *in) {
	if (!in->sel.loaded) { set_status("No se encontro select.def."); return; }
	int cur = 0, top = 0, grabbed = -1;
	char search[128] = "";
	status[0] = 0;
	while (appletMainLoop()) {
		scan_cells(&in->sel);
		if (cur >= ncells) cur = ncells ? ncells - 1 : 0;
		int on = 0;
		for (int i = 0; i < ncells; i++) on += !cells[i].off && !is_placeholder(cells[i].name);
		header("Roster");
		printf("\x1b[4;2H\x1b[37m%d casillas, %d con personaje activo.\x1b[0m", ncells, on);
		top = list_top(cur, top, 30);
		for (int r = top; r < ncells && r < top + 30; r++) {
			Cell *c = &cells[r];
			char label[120], val[40];
			snprintf(label, sizeof label, "%4d %s%s", r + 1, c->slot ? "[grupo] " : "", c->name);
			snprintf(val, sizeof val, "%s", r == grabbed ? "<< moviendo >>" : c->off ? "Fuera" :
				is_placeholder(c->name) ? "" : c->slot ? "A: ver grupo" : "Activo");
			if (c->off && r != cur) printf("\x1b[31m");
			draw_row(6 + r - top, r == cur, label, val);
		}
		if (grabbed >= 0)
			footer("Arriba/Abajo: mover 1   L/R: mover 10   Y o B: soltar", "Mueve el personaje a otra casilla de la grilla.");
		else
			footer("A:on/off Y:mover X:buscar -:nuevos L/R:pagina B:volver +:guardar",
				"Quitar deja la casilla vacia. '-' pone personajes nuevos desde esta casilla.");
		consoleUpdate(NULL);
		u64 k = input();
		if (grabbed >= 0) {
			int steps = UP(k) ? -1 : DOWN(k) ? 1 : (kdown & HidNpadButton_L) ? -10 : (kdown & HidNpadButton_R) ? 10 : 0;
			for (; steps < 0 && grabbed > 0; steps++) {
				swap_cells(&in->sel, &cells[grabbed - 1], &cells[grabbed]);
				scan_cells(&in->sel);
				grabbed--;
			}
			for (; steps > 0 && grabbed + 1 < ncells; steps--) {
				swap_cells(&in->sel, &cells[grabbed], &cells[grabbed + 1]);
				scan_cells(&in->sel);
				grabbed++;
			}
			cur = grabbed;
			if (kdown & (HidNpadButton_Y | HidNpadButton_B)) grabbed = -1;
			continue;
		}
		cur = list_move(k, cur, ncells, 20);
		if ((kdown & HidNpadButton_A) && ncells) {
			if (cells[cur].slot) slot_page(in, cur);
			else toggle_cell(&in->sel, &cells[cur]);
		}
		if ((kdown & HidNpadButton_Y) && ncells) { grabbed = cur; status[0] = 0; }
		if ((kdown & HidNpadButton_X) && ncells && kbd_input("Buscar personaje", search, search, sizeof search)) {
			for (int i = 1; i <= ncells; i++) {
				int j = (cur + i) % ncells;
				if (contains_ci(cells[j].name, search)) { cur = j; break; }
			}
		}
		if (kdown & HidNpadButton_Minus) {
			show_busy("Roster > personajes nuevos", "Buscando en chars/ personajes que no estan en el select.def...");
			int n;
			NewItem *items = find_new_chars(in, &n);
			AddCharCtx ctx = { in, cur, "" };
			new_items_page("Roster > personajes nuevos", "carpetas en chars/ que no estan en el roster.",
				items, n, add_char, &ctx, &in->sel);
			free(items);
		}
		if (kdown & HidNpadButton_Plus) set_status(tf_save(&in->sel) ? "Guardado." : "ERROR al guardar select.def");
		if (kdown & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------
// Stages: [ExtraStages].

typedef struct {
	int line;
	bool off;
	char def[200], music[200];
} StageEntry;

static StageEntry *stg;
static int nstg, stg_cap;

static void scan_stages(const TextFile *sel) {
	nstg = 0;
	int first, last;
	if (!section_range(sel, "ExtraStages", &first, &last)) return;
	for (int i = first; i < last; i++) {
		const char *l = skip_ws(sel->l[i]);
		bool off = starts_off(l);
		if (off) l += strlen(NXOFF);
		else if (*l == ';') continue;
		if (!*l) continue;
		if (nstg == stg_cap) stg = realloc(stg, (stg_cap = stg_cap ? stg_cap * 2 : 128) * sizeof *stg);
		StageEntry *s = &stg[nstg++];
		memset(s, 0, sizeof *s);
		s->line = i;
		s->off = off;
		entry_name(l, s->def, sizeof s->def);
		size_t body = strcspn(l, ";");
		for (const char *m = l; (m = strstr(m, "music")) && (size_t)(m - l) < body; m += 5) {
			const char *q = skip_ws(m + 5);
			if (*q != '=') continue;
			q = skip_ws(q + 1);
			trim_into(s->music, sizeof s->music, q, strcspn(q, ",;"));
			break;
		}
	}
}

// Rewrites a stage line with a new music= (empty: none).
static void set_stage_music(TextFile *sel, const StageEntry *s, const char *music) {
	const char *l = sel->l[s->line];
	char body[1100], comment[600] = "", kept[1100] = "", out[1800];
	const char *sc = strchr(l, ';');
	if (sc) snprintf(comment, sizeof comment, " %s", sc);
	trim_into(body, sizeof body, l, sc ? (size_t)(sc - l) : strlen(l));
	// Keep every parameter except music=.
	int nt = 0;
	for (char *tok = strtok(body, ","); tok; tok = strtok(NULL, ",")) {
		char t[600];
		trim_into(t, sizeof t, tok, strlen(tok));
		if (!strncasecmp(t, "music", 5) && strchr(t, '=')) continue;
		size_t kl = strlen(kept);
		snprintf(kept + kl, sizeof kept - kl, "%s%s", nt++ ? ", " : "", t);
	}
	if (*music) snprintf(out, sizeof out, "%s, music=%s%s", kept, music, comment);
	else snprintf(out, sizeof out, "%s%s", kept, comment);
	tf_replace(sel, s->line, out);
}

static void add_path(char ***list, int *n, int *cap, const char *s) {
	if (*n == *cap) *list = realloc(*list, (*cap = *cap ? *cap * 2 : 64) * sizeof **list);
	(*list)[(*n)++] = strdup(s);
}

// Files under sound/ (and one folder level), as "sound/..." paths.
static char **list_music(Install *in, int *count) {
	char **out = NULL;
	int n = 0, cap = 0;
	char base[400], rel[700];
	snprintf(base, sizeof base, "%s/sound", in->dir);
	DIR *d = opendir(base);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (!is_dir(base, e)) {
			snprintf(rel, sizeof rel, "sound/%s", e->d_name);
			add_path(&out, &n, &cap, rel);
			continue;
		}
		char p[700];
		snprintf(p, sizeof p, "%s/%s", base, e->d_name);
		DIR *sd = opendir(p);
		struct dirent *f;
		while (sd && (f = readdir(sd))) {
			if (f->d_name[0] == '.' || is_dir(p, f)) continue;
			snprintf(rel, sizeof rel, "sound/%s/%s", e->d_name, f->d_name);
			add_path(&out, &n, &cap, rel);
		}
		if (sd) closedir(sd);
	}
	if (d) closedir(d);
	if (n) qsort(out, n, sizeof *out, cmp_str);
	*count = n;
	return out;
}

// Puts the chosen path in out ("" = no music=); false if cancelled.
static bool music_chooser(Install *in, const char *current, char *out, size_t outn) {
	show_busy("Escenarios > musica", "Leyendo sound/...");
	int n;
	char **files = list_music(in, &n);
	int cur = 0, top = 0;
	for (int i = 0; i < n; i++) if (!strcasecmp(files[i], current)) cur = i + 1;
	bool ok = false;
	while (appletMainLoop()) {
		header("Escenarios > musica");
		printf("\x1b[4;2H\x1b[37mActual: %.70s\x1b[0m", *current ? current : "(la del escenario)");
		top = list_top(cur, top, 30);
		for (int r = top; r <= n && r < top + 30; r++)
			draw_row(6 + r - top, r == cur, r ? files[r - 1] : "(sin music=: la del escenario)", "");
		footer("A: elegir   L/R: pagina   B: cancelar", "Archivos de sound/ y sus subcarpetas.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, n + 1, 20);
		if (kdown & HidNpadButton_A) {
			snprintf(out, outn, "%s", cur ? files[cur - 1] : "");
			ok = true;
			break;
		}
		if (kdown & HidNpadButton_B) break;
	}
	for (int i = 0; i < n; i++) free(files[i]);
	free(files);
	return ok;
}

static bool has_stageinfo(const char *path) {
	FILE *fp = fopen(path, "rb");
	if (!fp) return false;
	char buf[4097];
	size_t n = fread(buf, 1, 4096, fp);
	fclose(fp);
	buf[n] = 0;
	return contains_ci(buf, "[stageinfo]");
}

// Stage .def files in stages/ (and one folder level) not in [ExtraStages].
static NewItem *find_new_stages(Install *in, int *count) {
	scan_stages(&in->sel);
	char **cands = NULL;
	int nc = 0, cc = 0;
	char base[400], rel[700];
	snprintf(base, sizeof base, "%s/stages", in->dir);
	DIR *d = opendir(base);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (!is_dir(base, e)) {
			if (has_ext(e->d_name, ".def")) {
				snprintf(rel, sizeof rel, "stages/%s", e->d_name);
				add_path(&cands, &nc, &cc, rel);
			}
			continue;
		}
		char p[700];
		snprintf(p, sizeof p, "%s/%s", base, e->d_name);
		DIR *sd = opendir(p);
		struct dirent *f;
		while (sd && (f = readdir(sd))) {
			if (has_ext(f->d_name, ".def")) {
				snprintf(rel, sizeof rel, "stages/%s/%s", e->d_name, f->d_name);
				add_path(&cands, &nc, &cc, rel);
			}
		}
		if (sd) closedir(sd);
	}
	if (d) closedir(d);
	NewItem *items = NULL;
	int n = 0, cap = 0;
	for (int c = 0; c < nc; c++) {
		bool known = false;
		for (int i = 0; i < nstg && !known; i++) known = !strcasecmp(stg[i].def, cands[c]);
		char full[900];
		snprintf(full, sizeof full, "%s/%s", in->dir, cands[c]);
		if (!known && has_stageinfo(full)) {
			if (n == cap) items = realloc(items, (cap = cap ? cap * 2 : 64) * sizeof *items);
			memset(&items[n], 0, sizeof *items);
			snprintf(items[n].ref, sizeof items[n].ref, "%s", cands[c]);
			n++;
		}
		free(cands[c]);
	}
	free(cands);
	*count = n;
	return items;
}

static const char *add_stage(void *vctx, const char *rel) {
	TextFile *sel = vctx;
	static char msg[300];
	int first, last;
	if (!section_range(sel, "ExtraStages", &first, &last)) {
		tf_insert(sel, sel->n, "");
		tf_insert(sel, sel->n, "[ExtraStages]");
		first = last = sel->n;
	}
	int at = first;
	for (int i = first; i < last; i++) if (*skip_ws(sel->l[i])) at = i + 1;
	tf_insert(sel, at, rel);
	sel->dirty = true;
	snprintf(msg, sizeof msg, "%s agregado.", rel);
	return msg;
}

static void stages_page(Install *in) {
	if (!in->sel.loaded) { set_status("No se encontro select.def."); return; }
	int cur = 0, top = 0;
	char search[128] = "";
	status[0] = 0;
	while (appletMainLoop()) {
		scan_stages(&in->sel);
		if (cur >= nstg) cur = nstg ? nstg - 1 : 0;
		int on = 0;
		for (int i = 0; i < nstg; i++) on += !stg[i].off;
		header("Escenarios");
		printf("\x1b[4;2H\x1b[37m%d escenarios, %d activos.\x1b[0m", nstg, on);
		top = list_top(cur, top, 30);
		for (int r = top; r < nstg && r < top + 30; r++) {
			const char *mb = strrchr(stg[r].music, '/');
			char val[64];
			snprintf(val, sizeof val, "%s", stg[r].off ? "Fuera" : *stg[r].music ? (mb ? mb + 1 : stg[r].music) : "");
			if (stg[r].off && r != cur) printf("\x1b[31m");
			draw_row(6 + r - top, r == cur, stg[r].def, val);
		}
		if (!nstg) printf("\x1b[7;4HNo hay escenarios en [ExtraStages]. Usa - para agregar.");
		footer("A:on/off Y:musica X:buscar -:nuevos L/R:pagina B:volver +:guardar",
			!nstg ? "" : *stg[cur].music ? stg[cur].music : "Sin music=: suena la musica propia del escenario.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, nstg, 20);
		if ((kdown & HidNpadButton_A) && nstg) {
			const char *l = skip_ws(in->sel.l[stg[cur].line]);
			char line[1100];
			if (stg[cur].off) {
				tf_replace(&in->sel, stg[cur].line, l + strlen(NXOFF));
			} else {
				snprintf(line, sizeof line, NXOFF "%s", l);
				tf_replace(&in->sel, stg[cur].line, line);
			}
		}
		if ((kdown & HidNpadButton_Y) && nstg && !stg[cur].off) {
			char music[300];
			if (music_chooser(in, stg[cur].music, music, sizeof music)) {
				set_stage_music(&in->sel, &stg[cur], music);
				set_status("Musica: %s", *music ? music : "la del escenario");
			}
		}
		if ((kdown & HidNpadButton_X) && nstg && kbd_input("Buscar escenario", search, search, sizeof search)) {
			for (int i = 1; i <= nstg; i++) {
				int j = (cur + i) % nstg;
				if (contains_ci(stg[j].def, search)) { cur = j; break; }
			}
		}
		if (kdown & HidNpadButton_Minus) {
			show_busy("Escenarios > nuevos", "Buscando en stages/ escenarios que no estan en el select.def...");
			int n;
			NewItem *items = find_new_stages(in, &n);
			new_items_page("Escenarios > nuevos", "escenarios en stages/ que no estan en [ExtraStages].",
				items, n, add_stage, &in->sel, &in->sel);
			free(items);
		}
		if (kdown & HidNpadButton_Plus) set_status(tf_save(&in->sel) ? "Guardado." : "ERROR al guardar select.def");
		if (kdown & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------
// Screens: common options of the motif's system.def, plus the title menu
// items ([Title Info] menu.itemname.*), which can be hidden and shown.

static const Setting screens[] = {
	{ "Pantalla VS: duracion (frames)", "60 frames = 1 segundo. 0 la salta.", "VS Screen", "time", 0, 600, 15 },
	{ "Pantalla VS: fundido de entrada", "Frames del fundido al entrar a la pantalla VS.", "VS Screen", "fadein.time", 0, 120, 5 },
	{ "Pantalla de victoria", "La pantalla con la frase del ganador despues de cada pelea.", "Victory Screen", "enabled", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Pantalla de continuar", "Preguntar si continuar al perder en arcade.", "Continue Screen", "enabled", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Modo demo", "Peleas de la CPU cuando el titulo queda quieto.", "Demo Mode", "enabled", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Espera antes del demo (frames)", "Cuanto espera el titulo antes de empezar el demo.", "Demo Mode", "title.waittime", 60, 3600, 60 },
	{ "Grilla de seleccion: filas", "Filas de casillas en la seleccion de personajes.", "Select Info", "rows", 1, 100, 1 },
	{ "Grilla de seleccion: columnas", "Columnas de casillas en la seleccion de personajes.", "Select Info", "columns", 1, 100, 1 },
	{ "Mostrar casillas vacias", "Dibujar el fondo de las casillas sin personaje.", "Select Info", "showemptyboxes", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
	{ "Cursor pasa por casillas vacias", "Si es No, el cursor salta las casillas vacias.", "Select Info", "moveoveremptyboxes", 0, 0, 0,
		2, { "No", "Si" }, { "0", "1" } },
};

static void screens_page(Install *in) {
	if (!in->mot.loaded) { set_status("No se pudo leer el system.def del screenpack."); return; }
	const int ns = sizeof screens / sizeof *screens;
	int cur = 0, top = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		// Title menu items present in the file.
		int items[64], ni = 0;
		char cursec[128] = "", key[128];
		for (int i = 0; i < in->mot.n && ni < 64; i++) {
			if (line_section(in->mot.l[i], cursec, sizeof cursec)) continue;
			if (!strcasecmp(cursec, "Title Info") && line_key(in->mot.l[i], key, sizeof key) &&
				!strncasecmp(key, "menu.itemname.", 14)) items[ni++] = i;
		}
		int rows = ns + ni;
		header("Pantallas (system.def)");
		printf("\x1b[4;2H\x1b[37m%.76s\x1b[0m", in->motifpath);
		top = list_top(cur, top, 30);
		for (int r = top; r < rows && r < top + 30; r++) {
			char label[128], val[64], v[128];
			if (r < ns) {
				setting_text(&in->mot, &screens[r], val, sizeof val);
				draw_row(6 + r - top, r == cur, screens[r].label, val);
				continue;
			}
			const char *l = in->mot.l[items[r - ns]];
			line_key(l, key, sizeof key);
			line_value(l, v, sizeof v);
			snprintf(label, sizeof label, "Menu: %s", key + 14);
			if (*v) snprintf(val, sizeof val, "< %s >", v);
			else snprintf(val, sizeof val, "< Oculto >");
			draw_row(6 + r - top, r == cur, label, val);
		}
		footer("Izq/Der: cambiar   A: texto del menu   B: volver   +: guardar",
			cur < ns ? screens[cur].help : "Izq/Der: mostrar u ocultar este item del menu principal.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, rows, 20);
		int d = LEFT(k) ? -1 : RIGHT(k) ? 1 : 0;
		if (cur < ns) {
			if (d) { setting_change(&in->mot, &screens[cur], d); status[0] = 0; }
		} else if (d || (kdown & HidNpadButton_A)) {
			int li = items[cur - ns];
			const char *l = in->mot.l[li];
			int keylen = (int)(strchr(l, '=') - l);
			char v[256], line[700];
			line_value(l, v, sizeof v);
			if (d && *v) {
				// Hide: empty value, the old one kept in a comment.
				snprintf(line, sizeof line, "%.*s= " NXHIDE "%s", keylen, l, v);
				tf_replace(&in->mot, li, line);
			} else {
				char name[256] = "";
				const char *h = strstr(l, NXHIDE);
				if (*v) snprintf(name, sizeof name, "%s", v);
				else if (h) trim_into(name, sizeof name, h + strlen(NXHIDE), strlen(h + strlen(NXHIDE)));
				bool ask = !*name || (kdown & HidNpadButton_A);
				if (!*name) {
					line_key(l, key, sizeof key);
					snprintf(name, sizeof name, "\"%s\"", key + 14);
				}
				if (!ask || kbd_input("Texto del item (entre comillas)", name, name, sizeof name)) {
					snprintf(line, sizeof line, "%.*s= %s", keylen, l, name);
					tf_replace(&in->mot, li, line);
				}
			}
		}
		if (kdown & HidNpadButton_Plus) set_status(tf_save(&in->mot) ? "Guardado." : "ERROR al guardar system.def");
		if (kdown & HidNpadButton_B) return;
	}
}

// ---------------------------------------------------------------------------
// Line editor for any text file of the install.

// standalone: the file is not one of the install's (its changes are lost
// on leaving without saving, so leaving asks first).
static void line_editor(TextFile *f, const char *title, bool standalone) {
	int cur = 0, top = 0, del_armed = -1;
	bool leave_armed = false;
	char search[128] = "";
	const int visible = 32;
	status[0] = 0;
	while (appletMainLoop()) {
		if (cur >= f->n) cur = f->n ? f->n - 1 : 0;
		header(title);
		printf("\x1b[4;2H\x1b[37m%.62s  (%d lineas)%s\x1b[0m", f->path, f->n, f->dirty ? " *cambios*" : "");
		top = list_top(cur, top, visible);
		for (int r = top; r < f->n && r < top + visible; r++) {
			printf("\x1b[%d;1H", 6 + r - top);
			if (r == cur) printf("\x1b[30;46m");
			const char *l = f->l[r];
			printf("%5d %-72.72s%s\x1b[0m", r + 1, l, strlen(l) > 72 ? ">" : " ");
		}
		footer("A:editar Y:nueva -:borrar X:buscar L/R:pagina B:volver +:guardar",
			del_armed >= 0 ? "Pulsa - otra vez para borrar la linea." : "Las lineas largas se ven completas al editarlas.");
		consoleUpdate(NULL);
		u64 k = input();
		int prev = cur;
		cur = list_move(k, cur, f->n, visible);
		if (cur != prev) del_armed = -1;
		if ((kdown & HidNpadButton_A) && f->n) {
			char buf[1024], guide[64];
			snprintf(guide, sizeof guide, "Linea %d", cur + 1);
			if (kbd_input(guide, f->l[cur], buf, sizeof buf)) tf_replace(f, cur, buf);
		}
		if (kdown & HidNpadButton_Y) {
			char buf[1024] = "";
			if (kbd_input("Linea nueva", "", buf, sizeof buf)) {
				tf_insert(f, f->n ? cur + 1 : 0, buf);
				f->dirty = true;
				if (f->n > 1) cur++;
			}
		}
		if ((kdown & HidNpadButton_Minus) && f->n) {
			if (del_armed == cur) { tf_delete(f, cur); del_armed = -1; set_status("Linea borrada."); }
			else del_armed = cur;
		}
		if ((kdown & HidNpadButton_X) && f->n && kbd_input("Buscar texto", search, search, sizeof search)) {
			for (int i = 1; i <= f->n; i++) {
				int j = (cur + i) % f->n;
				if (contains_ci(f->l[j], search)) { cur = j; break; }
			}
		}
		if (kdown & HidNpadButton_Plus) { set_status(tf_save(f) ? "Guardado." : "ERROR al guardar"); leave_armed = false; }
		if (kdown & HidNpadButton_B) {
			if (!standalone || !f->dirty || leave_armed) return;
			leave_armed = true;
			set_status("Hay cambios sin guardar: + guarda, B otra vez sale sin guardar.");
		}
	}
}

// Files offered by the line editor: config.ini, system.def, select.def and
// the other .def files next to system.def.
static void editor_menu(Install *in) {
	enum { MAXF = 40 };
	static char names[MAXF][128], paths[MAXF][600];
	int n = 0;
	snprintf(names[n], sizeof names[0], "config.ini");
	snprintf(paths[n++], sizeof paths[0], "%s", in->cfg.path);
	if (in->mot.loaded) {
		snprintf(names[n], sizeof names[0], "system.def (screenpack)");
		snprintf(paths[n++], sizeof paths[0], "%s", in->mot.path);
	}
	if (in->sel.loaded) {
		snprintf(names[n], sizeof names[0], "select.def");
		snprintf(paths[n++], sizeof paths[0], "%s", in->sel.path);
	}
	char dir[600];
	snprintf(dir, sizeof dir, "%s", in->motifpath);
	char *slash = strrchr(dir, '/');
	if (slash) *slash = 0;
	DIR *d = opendir(dir);
	struct dirent *e;
	while (d && (e = readdir(d)) && n < MAXF) {
		if (!has_ext(e->d_name, ".def")) continue;
		char p[600];
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		bool dup = false;
		for (int i = 0; i < n; i++) dup = dup || !strcasecmp(paths[i], p);
		if (dup) continue;
		snprintf(names[n], sizeof names[0], "%s", e->d_name);
		snprintf(paths[n++], sizeof paths[0], "%s", p);
	}
	if (d) closedir(d);
	int cur = 0;
	status[0] = 0;
	while (appletMainLoop()) {
		header("Editor de archivos");
		printf("\x1b[4;2H\x1b[37mElige el archivo a editar linea por linea:\x1b[0m");
		for (int i = 0; i < n; i++) draw_row(6 + i, i == cur, names[i], "");
		footer("A: abrir   B: volver", "Se edita con el teclado de la Switch. Al guardar deja una copia .bak.");
		consoleUpdate(NULL);
		u64 k = input();
		cur = list_move(k, cur, n, 10);
		if (kdown & HidNpadButton_B) return;
		if (!(kdown & HidNpadButton_A)) continue;
		// The install's own files are edited in place, so other pages see it.
		TextFile *f = NULL, tmp = {0};
		if (!strcmp(paths[cur], in->cfg.path)) f = &in->cfg;
		else if (in->mot.loaded && !strcmp(paths[cur], in->mot.path)) f = &in->mot;
		else if (in->sel.loaded && !strcmp(paths[cur], in->sel.path)) f = &in->sel;
		else if (tf_load(&tmp, paths[cur])) f = &tmp;
		if (!f) { set_status("No se pudo abrir %s", names[cur]); continue; }
		line_editor(f, names[cur], f == &tmp);
		if (f == &tmp) tf_free(&tmp);
	}
}
