// Updates for the Ikemen NX programs, shared by the engine (app/main.c),
// Ikemen NX Config and Ikemen NX Browser.
//
// The latest versions are listed in versions.ini in the GitHub repository,
// read from raw.githubusercontent.com:
//   [ikemen] / [nxconfig] / [nxbrowser]
//   version = 1.2.3
//   url     = https://github.com/.../releases/download/.../file.nro
//   notes   = one line shown before updating
// The installed version is the display version in the .nro's NACP, so any
// copy (kofa.nro, "Mi juego.nro", ...) can be checked. Engine updates keep
// the old .nro's icon and name: only the code part is replaced.
//
// Needs sockets (socketInitializeDefault) and curl (-lcurl -lz).
#pragma once
#include <switch.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define NXU_MANIFEST "https://raw.githubusercontent.com/juanp9180-cmyk/ikemen-nx/main/versions.ini"
#define NXU_CONFIG_DIR "sdmc:/config/ikemen-nx"
#define NXU_CONFIG NXU_CONFIG_DIR "/update.ini"

typedef struct {
	char version[32], url[512], notes[256];
} NxuInfo;

typedef struct {
	char *p;
	size_t n, cap;
} NxuBuf;

static size_t nxu_mem_write(char *data, size_t size, size_t n, void *ud) {
	NxuBuf *b = ud;
	size_t len = size * n;
	if (b->n + len + 1 > b->cap) return 0; // manifest too big
	memcpy(b->p + b->n, data, len);
	b->n += len;
	b->p[b->n] = 0;
	return len;
}

// Downloads the manifest into out. False if offline or on error.
static bool nxu_fetch_manifest(char *out, size_t n) {
	NxuBuf b = { out, 0, n };
	out[0] = 0;
	CURL *c = curl_easy_init();
	if (!c) return false;
	curl_easy_setopt(c, CURLOPT_URL, NXU_MANIFEST);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "IkemenNX-Updater/1.0");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 4L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, nxu_mem_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
	CURLcode r = curl_easy_perform(c);
	curl_easy_cleanup(c);
	return r == CURLE_OK && b.n > 0;
}

static void nxu_trim(char *s) {
	char *p = s;
	while (*p == ' ' || *p == '\t') p++;
	memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

// Reads [app] from the manifest.
static bool nxu_lookup(const char *manifest, const char *app, NxuInfo *info) {
	memset(info, 0, sizeof *info);
	char sec[64] = "";
	const char *p = manifest;
	while (*p) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		char line[700];
		snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), p);
		nxu_trim(line);
		if (line[0] == '[') {
			char *end = strchr(line, ']');
			if (end) { *end = 0; snprintf(sec, sizeof sec, "%s", line + 1); }
		} else if (line[0] != ';' && !strcasecmp(sec, app)) {
			char *eq = strchr(line, '=');
			if (eq) {
				*eq = 0;
				char *k = line, *v = eq + 1;
				nxu_trim(k);
				nxu_trim(v);
				if (!strcasecmp(k, "version")) snprintf(info->version, sizeof info->version, "%s", v);
				else if (!strcasecmp(k, "url")) snprintf(info->url, sizeof info->url, "%s", v);
				else if (!strcasecmp(k, "notes")) snprintf(info->notes, sizeof info->notes, "%s", v);
			}
		}
		if (!e) break;
		p = e + 1;
	}
	return info->version[0] && info->url[0];
}

// <0, 0, >0 like strcmp, for dotted numbers ("0.3.10" > "0.3.9").
static int nxu_cmp(const char *a, const char *b) {
	while (*a || *b) {
		long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
		if (x != y) return x < y ? -1 : 1;
		while (*a && *a != '.') a++;
		while (*b && *b != '.') b++;
		if (*a == '.') a++;
		if (*b == '.') b++;
	}
	return 0;
}

// NRO layout: header with the code size at 0x18; after the code, the
// "ASET" block (icon, NACP, romfs) with offsets relative to its start.
static bool nxu_nro_assets(FILE *f, u32 *code_size, u64 *nacp_off) {
	u8 aset[0x38];
	if (fseek(f, 0x18, SEEK_SET) || fread(code_size, 4, 1, f) != 1) return false;
	if (fseek(f, *code_size, SEEK_SET) || fread(aset, sizeof aset, 1, f) != 1 || memcmp(aset, "ASET", 4)) return false;
	u64 size;
	memcpy(nacp_off, aset + 0x18, 8);
	memcpy(&size, aset + 0x20, 8);
	return size >= 0x4000;
}

// Display version stored in an .nro's NACP ("" if unreadable).
static bool nxu_nro_version(const char *path, char *out, size_t n) {
	out[0] = 0;
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	u32 code;
	u64 nacp;
	char v[17] = {0};
	bool ok = nxu_nro_assets(f, &code, &nacp) && !fseek(f, code + nacp + 0x3060, SEEK_SET) && fread(v, 16, 1, f) == 1;
	fclose(f);
	if (ok) snprintf(out, n, "%s", v);
	return ok && out[0];
}

static bool nxu_nro_valid(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	char magic[4];
	u32 code;
	u64 nacp;
	bool ok = !fseek(f, 0x10, SEEK_SET) && fread(magic, 4, 1, f) == 1 && !memcmp(magic, "NRO0", 4) &&
		nxu_nro_assets(f, &code, &nacp);
	fclose(f);
	return ok;
}

typedef void (*NxuProgress)(double now, double total);

static int nxu_xfer(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) {
	(void)ut; (void)un;
	if (ud) ((NxuProgress)ud)((double)dn, (double)dt);
	return 0;
}

static size_t nxu_file_write(char *data, size_t size, size_t n, void *fp) {
	return fwrite(data, size, n, fp);
}

static bool nxu_download(const char *url, const char *dest, NxuProgress progress) {
	FILE *fp = fopen(dest, "wb");
	if (!fp) return false;
	CURL *c = curl_easy_init();
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "IkemenNX-Updater/1.0");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, nxu_file_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, nxu_xfer);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, (void *)progress);
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 256L * 1024);
	CURLcode r = curl_easy_perform(c);
	curl_easy_cleanup(c);
	bool ok = fclose(fp) == 0 && r == CURLE_OK;
	if (!ok) remove(dest);
	return ok;
}

static bool nxu_copy_range(FILE *from, long start, long len, FILE *to) {
	static char buf[64 * 1024];
	if (fseek(from, start, SEEK_SET)) return false;
	while (len > 0) {
		size_t k = fread(buf, 1, len < (long)sizeof buf ? (size_t)len : sizeof buf, from);
		if (!k || fwrite(buf, 1, k, to) != k) return false;
		len -= k;
	}
	return true;
}

// Replaces target with the downloaded .nro. keep_assets: keep target's
// icon and name (engine copies such as kofa.nro), with `version` written
// as the new display version.
static bool nxu_install_nro(const char *downloaded, const char *target, bool keep_assets, const char *version) {
	if (!nxu_nro_valid(downloaded)) return false;
	char tmp[700];
	snprintf(tmp, sizeof tmp, "%s.new", target);
	bool ok;
	if (!keep_assets || !nxu_nro_valid(target)) {
		remove(tmp);
		ok = rename(downloaded, tmp) == 0;
	} else {
		FILE *nf = fopen(downloaded, "rb"), *of = fopen(target, "rb"), *out = fopen(tmp, "wb");
		u32 ncode, ocode;
		u64 onacp, nnacp;
		ok = nf && of && out && nxu_nro_assets(nf, &ncode, &nnacp) && nxu_nro_assets(of, &ocode, &onacp);
		if (ok) {
			fseek(of, 0, SEEK_END);
			long oend = ftell(of);
			// New code, then the old ASET block with the new version.
			ok = nxu_copy_range(nf, 0, ncode, out) && nxu_copy_range(of, ocode, oend - ocode, out);
			if (ok && version) {
				char v[16] = {0};
				snprintf(v, sizeof v, "%s", version);
				ok = !fseek(out, ncode + onacp + 0x3060, SEEK_SET) && fwrite(v, sizeof v, 1, out) == 1;
			}
		}
		if (nf) fclose(nf);
		if (of) fclose(of);
		if (out && fclose(out)) ok = false;
		remove(downloaded);
	}
	if (!ok || !nxu_nro_valid(tmp)) { remove(tmp); return false; }
	char old[720];
	snprintf(old, sizeof old, "%s.old", target);
	remove(old);
	if (rename(target, old) != 0) { remove(tmp); return false; }
	if (rename(tmp, target) != 0) { rename(old, target); return false; }
	remove(old);
	return true;
}

// sdmc:/config/ikemen-nx/update.ini: "auto = 0" turns the automatic check
// off; "last_check" (unix time) limits the engine to one check a day.
static long nxu_config_get(const char *key, long def) {
	FILE *f = fopen(NXU_CONFIG, "r");
	if (!f) return def;
	char line[200];
	long v = def;
	size_t kl = strlen(key);
	while (fgets(line, sizeof line, f)) {
		nxu_trim(line);
		if (!strncasecmp(line, key, kl)) {
			char *eq = strchr(line, '=');
			if (eq) v = strtol(eq + 1, NULL, 10);
		}
	}
	fclose(f);
	return v;
}

static void nxu_config_set_checked(long auto_on) {
	mkdir("sdmc:/config", 0777);
	mkdir(NXU_CONFIG_DIR, 0777);
	FILE *f = fopen(NXU_CONFIG, "w");
	if (!f) return;
	fprintf(f, "; Ikemen NX: actualizaciones automaticas (auto = 0 para desactivarlas)\nauto = %ld\nlast_check = %ld\n",
		auto_on, (long)time(NULL));
	fclose(f);
}
