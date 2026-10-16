// Self-test build (-DSELFTEST): edits a copy of an install under
// sdmc:/nxtest without any input, logs to sdmc:/nxtest/result.txt and
// exits, so the file handling can be checked in the emulator.
static FILE *tlog;

static void tlogf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(tlog, fmt, ap);
	va_end(ap);
	fflush(tlog);
}

static const Setting *find_setting(const Setting *set, int n, const char *key) {
	for (int i = 0; i < n; i++) if (!strcmp(set[i].key, key)) return &set[i];
	return NULL;
}

static void show(Install *in, const Setting *s) {
	char v[96];
	setting_text(&in->cfg, s, v, sizeof v);
	tlogf("  %s = %s\n", s->key, v);
}

int main(int argc, char **argv) {
	tlog = fopen("sdmc:/nxtest/result.txt", "w");
	if (!tlog) return 1;
	scan_root = "sdmc:/nxtest";
	scan_installs();
	tlogf("installs: %d\n", ninst);
	for (int i = 0; i < ninst; i++) {
		Install *in = &inst[i];
		if (!load_install(in)) { tlogf("load failed %s\n", in->dir); continue; }
		tlogf("[%s] title='%s' select='%s' cfg lines=%d sel lines=%d\n", in->folder, in->title, in->selpath, in->cfg.n, in->sel.n);

		const Setting *dif = find_setting(rules, sizeof rules / sizeof *rules, "Difficulty");
		const Setting *tim = find_setting(rules, sizeof rules / sizeof *rules, "Time");
		const Setting *asp = find_setting(media, sizeof media / sizeof *media, "FightAspectWidth");
		const Setting *bgm = find_setting(media, sizeof media / sizeof *media, "BGMVolume");
		const Setting *blm = find_setting(perf, sizeof perf / sizeof *perf, "BootLoadingMode");
		tlogf("before:\n"); show(in, dif); show(in, tim); show(in, asp); show(in, bgm); show(in, blm);
		setting_change(&in->cfg, dif, 1); setting_change(&in->cfg, dif, 1);
		for (int k = 0; k < 20; k++) setting_change(&in->cfg, dif, 1); // clamps at 8
		setting_change(&in->cfg, tim, 1);
		setting_change(&in->cfg, asp, 1);
		setting_change(&in->cfg, bgm, -1);
		setting_change(&in->cfg, blm, 1);
		ini_set(&in->cfg, "Options", "NxTestKey", "5");
		ini_set(&in->cfg, "NxTestSection", "Hello", "world");
		tlogf("after:\n"); show(in, dif); show(in, tim); show(in, asp); show(in, bgm); show(in, blm);

		if (in->sel.loaded) tlogf("roster cells: %d\n", roster_cells(&in->sel));
		if (in->sel.loaded) {
			scan_chars(in);
			read_maxmatches(in);
			tlogf("chars=%d orders:", nchars);
			for (int o = 1; o < 24; o++) if (order_count[o]) tlogf(" %d:%d", o, order_count[o]);
			tlogf("\nmaxmatches:");
			for (int o = 0; o < ORDERS; o++) tlogf(" %d", mm[o]);
			mm[0] = 1; mm[9] = 2;
			write_maxmatches(in);
			int toggled = 0;
			for (int c = 0; c < nchars && toggled < 2; c++) {
				if (is_boss(&chars[c])) {
					tlogf("\nboss off: %s (line %d)", chars[c].name, chars[c].line + 1);
					set_char_order(in, &chars[c], BOSS_OFF);
					toggled++;
				}
			}
			tlogf("\n");

			// Roster: disable kyo, enable it again, disable iori; move cell 2 after 3.
			scan_cells(&in->sel);
			tlogf("cells=%d\n", ncells);
			for (int c = 0; c < ncells; c++) {
				if (!strcasecmp(cells[c].name, "kyo") || !strcasecmp(cells[c].name, "duck")) {
					toggle_cell(&in->sel, &cells[c]); scan_cells(&in->sel);
					tlogf("toggle %s -> off=%d: %s\n", cells[c].name, cells[c].off, in->sel.l[cells[c].start]);
				}
			}
			for (int c = 0; c < ncells; c++) {
				if (!strcasecmp(cells[c].name, "kyo")) {
					toggle_cell(&in->sel, &cells[c]); scan_cells(&in->sel);
					tlogf("toggle back -> off=%d: %s\n", cells[c].off, in->sel.l[cells[c].start]);
				}
			}
			int a = -1;
			for (int c = 0; c < ncells; c++) if (!strcasecmp(cells[c].name, "brian")) a = c;
			if (a >= 0 && a + 1 < ncells) {
				tlogf("swap %s <-> %s\n", cells[a].name, cells[a + 1].name);
				swap_cells(&in->sel, &cells[a], &cells[a + 1]);
				scan_cells(&in->sel);
				tlogf("now %s, %s\n", cells[a].name, cells[a + 1].name);
			}
			// A slot cell next to a normal one.
			for (int c = 0; c + 1 < ncells; c++) {
				if (cells[c].slot) {
					tlogf("slot cell %d: %s (lines %d-%d)\n", c + 1, cells[c].name, cells[c].start + 1, cells[c].end + 1);
					swap_cells(&in->sel, &cells[c], &cells[c + 1]);
					scan_cells(&in->sel);
					swap_cells(&in->sel, &cells[c], &cells[c + 1]);
					scan_cells(&in->sel);
					tlogf("swapped twice: %s\n", cells[c].name);
					break;
				}
			}
			// New characters and stages from the test folders.
			int n;
			NewItem *ni = find_new_chars(in, &n);
			tlogf("new chars: %d\n", n);
			AddCharCtx ctx = { in, 0, "" };
			for (int k = 0; k < n; k++) tlogf("  %s -> %s\n", ni[k].ref, add_char(&ctx, ni[k].ref));
			free(ni);
			ni = find_new_stages(in, &n);
			tlogf("new stages: %d\n", n);
			for (int k = 0; k < n; k++) tlogf("  %s\n", add_stage(&in->sel, ni[k].ref));
			free(ni);
			scan_stages(&in->sel);
			tlogf("stages=%d first=%s music=%s\n", nstg, nstg ? stg[0].def : "", nstg ? stg[0].music : "");
			if (nstg) {
				set_stage_music(&in->sel, &stg[0], "sound/test song.ogg");
				tlogf("music set: %s\n", in->sel.l[stg[0].line]);
				set_stage_music(&in->sel, &stg[0], "");
				tlogf("music cleared: %s\n", in->sel.l[stg[0].line]);
			}
			int mn;
			char **mus = list_music(in, &mn);
			tlogf("music files: %d%s%s\n", mn, mn ? " first=" : "", mn ? mus[0] : "");
			for (int k = 0; k < mn; k++) free(mus[k]);
			free(mus);
		}
		if (in->mot.loaded) {
			const Setting *vs = find_setting(screens, sizeof screens / sizeof *screens, "time");
			show(in, vs); // reads cfg: just to have the line count right below
			char v[64];
			setting_text(&in->mot, vs, v, sizeof v);
			tlogf("motif VS time %s\n", v);
			setting_change(&in->mot, vs, 1);
			setting_text(&in->mot, vs, v, sizeof v);
			tlogf("motif VS time after +1: %s\n", v);
		}
		tlogf("save: %s\n", save_all(in) ? "ok" : "FAILED");
		tf_free(&in->cfg);
		tf_free(&in->sel);
		tf_free(&in->mot);
	}
	tlogf("done\n");
	fclose(tlog);
	return 0;
}
