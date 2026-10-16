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
		}
		tlogf("save: %s\n", save_all(in) ? "ok" : "FAILED");
		tf_free(&in->cfg);
		tf_free(&in->sel);
	}
	tlogf("done\n");
	fclose(tlog);
	return 0;
}
