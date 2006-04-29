/*****************************************************************************
 * Free42 -- an HP-42S calculator simulator
 * Copyright (C) 2004-2006  Thomas Okken
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *****************************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_skin.h"
#include "shell_loadimage.h"
#include "core_main.h"


/**************************/
/* Skin description stuff */
/**************************/

typedef struct {
	int x, y;
} SkinPoint;

typedef struct {
	int x, y, width, height;
} SkinRect;

typedef struct {
	int code, shifted_code;
	SkinRect sens_rect;
	SkinRect disp_rect;
	SkinPoint src;
} SkinKey;

#define SKIN_MAX_MACRO_LENGTH 31

typedef struct _SkinMacro {
	int code;
	unsigned char macro[SKIN_MAX_MACRO_LENGTH + 1];
	struct _SkinMacro *next;
} SkinMacro;

typedef struct {
	SkinRect disp_rect;
	SkinPoint src;
} SkinAnnunciator;

static SkinRect skin;
static SkinPoint display_loc;
static SkinPoint display_scale;
static COLORREF display_bg, display_fg;
static SkinKey *keylist = NULL;
static int nkeys = 0;
static int keys_cap = 0;
static SkinMacro *macrolist = NULL;
static SkinAnnunciator annunciators[7];

static FILE *external_file;
static long builtin_length;
static long builtin_pos;
static const unsigned char *builtin_file;

static int skin_type;
static int skin_width, skin_height;
static int skin_ncolors;
static const SkinColor *skin_colors = NULL;
static int skin_y;
static unsigned char *skin_bitmap = NULL;
static int skin_bytesperline;
static BITMAPINFOHEADER *skin_header = NULL;
static HBITMAP skin_dib = NULL;
static unsigned char *disp_bitmap = NULL;
static int disp_bytesperline;
static bool landscape;

static keymap_entry *keymap = NULL;
static int keymap_length = 0;

// Used while loading a skin; specifies how much to enlarge it.
// This is necessary to force low-res skins to fill high-res screens.
static int magnification;


/**********************************************************/
/* Linked-in skins; defined in the skins.c, which in turn */
/* is generated by skin2c.c under control of skin2c.conf  */
/**********************************************************/

extern int skin_count;
extern const TCHAR *skin_name[];
extern long skin_layout_size[];
extern unsigned char *skin_layout_data[];
extern long skin_bitmap_size[];
extern unsigned char *skin_bitmap_data[];


/*****************/
/* Keymap parser */
/*****************/

keymap_entry *parse_keymap_entry(char *line, int lineno) {
	char *p;
	static keymap_entry entry;

	p =  strchr(line, '#');
	if (p != NULL)
		*p = 0;
	p = strchr(line, '\n');
	if (p != NULL)
		*p = 0;
	p = strchr(line, '\r');
	if (p != NULL)
		*p = 0;

	p = strchr(line, ':');
	if (p != NULL) {
		char *val = p + 1;
		char *tok;
		bool ctrl = false;
		bool alt = false;
		bool shift = false;
		bool cshift = false;
		int keycode = 0;
		int done = 0;
		unsigned char macro[KEYMAP_MAX_MACRO_LENGTH + 1];
		int macrolen = 0;

		/* Parse keycode */
		*p = 0;
		tok = strtok(line, " \t");
		while (tok != NULL) {
			if (done) {
				fprintf(stderr, "Keymap, line %d: Excess tokens in key spec.\n", lineno);
				return NULL;
			}
			if (_stricmp(tok, "ctrl") == 0)
				ctrl = true;
			else if (_stricmp(tok, "alt") == 0)
				alt = true;
			else if (_stricmp(tok, "shift") == 0)
				shift = true;
			else if (_stricmp(tok, "cshift") == 0)
				cshift = true;
			else {
				char *endptr;
				long k = strtol(tok, &endptr, 10);
				if (k < 1 || *endptr != 0) {
					fprintf(stderr, "Keymap, line %d: Bad keycode.\n", lineno);
					return NULL;
				}
				keycode = k;
				done = 1;
			}
			tok = strtok(NULL, " \t");
		}
		if (!done) {
			fprintf(stderr, "Keymap, line %d: Unrecognized keycode.\n", lineno);
			return NULL;
		}

		/* Parse macro */
		tok = strtok(val, " \t");
		while (tok != NULL) {
			char *endptr;
			long k = strtol(tok, &endptr, 10);
			if (*endptr != 0 || k < 1 || k > 255) {
				fprintf(stderr, "Keymap, line %d: Bad value (%s) in macro.\n", lineno, tok);
				return NULL;
			} else if (macrolen == KEYMAP_MAX_MACRO_LENGTH) {
				fprintf(stderr, "Keymap, line %d: Macro too long (max=%d).\n", lineno, KEYMAP_MAX_MACRO_LENGTH);
				return NULL;
			} else
				macro[macrolen++] = (unsigned char) k;
			tok = strtok(NULL, " \t");
		}
		macro[macrolen] = 0;

		entry.ctrl = ctrl;
		entry.alt = alt;
		entry.shift = shift;
		entry.cshift = cshift;
		entry.keycode = keycode;
		strcpy(entry.macro, macro);
		return &entry;
	} else
		return NULL;
}


/*******************/
/* Local functions */
/*******************/

static int skin_open(const TCHAR *skinname, const TCHAR *basedir, int open_layout);
static int skin_gets(char *buf, int buflen);
static void skin_close();


static int skin_open(const TCHAR *skinname, const TCHAR *basedir, int open_layout) {
	int i;
	TCHAR namebuf[1024];

	/* Look for built-in skin first */
	for (i = 0; i < skin_count; i++) {
		if (_tcscmp(skinname, skin_name[i]) == 0) {
			external_file = NULL;
			builtin_pos = 0;
			if (open_layout) {
				builtin_length = skin_layout_size[i];
				builtin_file = skin_layout_data[i];
			} else {
				builtin_length = skin_bitmap_size[i];
				builtin_file = skin_bitmap_data[i];
			}
			return 1;
		}
	}

	/* name did not match a built-in skin; look for file */
	_stprintf(namebuf, _T("%s\\%s.%s"), basedir, skinname,
										open_layout ? _T("layout") : _T("gif"));
	external_file = _tfopen(namebuf, _T("rb"));
	return external_file != NULL;
}

int skin_getchar() {
	if (external_file != NULL)
		return fgetc(external_file);
	else if (builtin_pos < builtin_length)
		return builtin_file[builtin_pos++];
	else
		return EOF;
}

static int skin_gets(char *buf, int buflen) {
	int p = 0;
	int eof = -1;
	int comment = 0;
	while (p < buflen - 1) {
		int c = skin_getchar();
		if (eof == -1)
			eof = c == EOF;
		if (c == EOF || c == '\n' || c == '\r')
			break;
		/* Remove comments */
		if (c == '#')
			comment = 1;
		if (comment)
			continue;
		/* Suppress leading spaces */
		if (p == 0 && isspace(c))
			continue;
		buf[p++] = c;
	}
	buf[p++] = 0;
	return p > 1 || !eof;
}

void skin_rewind() {
	if (external_file != NULL)
		fseek(external_file, 0, SEEK_SET);
	else
		builtin_pos = 0;
}

static void skin_close() {
	if (external_file != NULL)
		fclose(external_file);
}

void skin_load(TCHAR *skinname, const TCHAR *basedir, int width, int height) {
	char line[1024];
	int success;
	int size;
	int kmcap = 0;
	int lineno = 0;
	bool prev_landscape = landscape;

	if (skinname[0] == 0) {
		fallback_on_1st_builtin_skin:
		_tcscpy(skinname, skin_name[0]);
	}

	/*************************/
	/* Load skin description */
	/*************************/

	if (!skin_open(skinname, basedir, 1))
		goto fallback_on_1st_builtin_skin;

	if (keylist != NULL)
		free(keylist);
	keylist = NULL;
	nkeys = 0;
	keys_cap = 0;

	while (macrolist != NULL) {
		SkinMacro *m = macrolist->next;
		free(macrolist);
		macrolist = m;
	}

	if (keymap != NULL)
	    free(keymap);
	keymap = NULL;
	keymap_length = 0;

	landscape = false;

	while (skin_gets(line, 1024)) {
		lineno++;
		if (*line == 0)
			continue;
		if (_strnicmp(line, "skin:", 5) == 0) {
			int x, y, width, height;
			if (sscanf(line + 5, " %d,%d,%d,%d", &x, &y, &width, &height) == 4){
				skin.x = x;
				skin.y = y;
				skin.width = width;
				skin.height = height;
			}
		} else if (_strnicmp(line, "display:", 8) == 0) {
			int x, y, xscale, yscale;
			unsigned long bg, fg;
			if (sscanf(line + 8, " %d,%d %d %d %lx %lx", &x, &y,
											&xscale, &yscale, &bg, &fg) == 6) {
				display_loc.x = x;
				display_loc.y = y;
				display_scale.x = xscale;
				display_scale.y = yscale;
				display_bg = ((bg >> 16) & 255) | (bg & 0x0FF00) | ((bg & 255) << 16);
				display_fg = ((fg >> 16) & 255) | (fg & 0x0FF00) | ((fg & 255) << 16);
			}
		} else if (_strnicmp(line, "key:", 4) == 0) {
			char keynumbuf[20];
			int keynum, shifted_keynum;
			int sens_x, sens_y, sens_width, sens_height;
			int disp_x, disp_y, disp_width, disp_height;
			int act_x, act_y;
			if (sscanf(line + 4, " %s %d,%d,%d,%d %d,%d,%d,%d %d,%d",
								 &keynum,
								 &sens_x, &sens_y, &sens_width, &sens_height,
								 &disp_x, &disp_y, &disp_width, &disp_height,
								 &act_x, &act_y) == 11) {
				int n = sscanf(keynumbuf, "%d,%d", &keynum, &shifted_keynum);
				if (n > 0) {
					if (n == 1)
						shifted_keynum = keynum;
					SkinKey *key;
					if (nkeys == keys_cap) {
						keys_cap += 50;
						keylist = (SkinKey *) realloc(keylist, keys_cap * sizeof(SkinKey));
					}
					key = keylist + nkeys;
					key->code = keynum;
					key->shifted_code = shifted_keynum;
					key->sens_rect.x = sens_x;
					key->sens_rect.y = sens_y;
					key->sens_rect.width = sens_width;
					key->sens_rect.height = sens_height;
					key->disp_rect.x = disp_x;
					key->disp_rect.y = disp_y;
					key->disp_rect.width = disp_width;
					key->disp_rect.height = disp_height;
					key->src.x = act_x;
					key->src.y = act_y;
					nkeys++;
				}
			}
		} else if (_strnicmp(line, "landscape:", 10) == 0) {
			int ls;
			if (sscanf(line + 10, " %d", &ls) == 1)
				landscape = ls != 0;
		} else if (_strnicmp(line, "macro:", 6) == 0) {
			char *tok = strtok(line + 6, " ");
			int len = 0;
			SkinMacro *macro = NULL;
			while (tok != NULL) {
				char *endptr;
				long n = strtol(tok, &endptr, 10);
				if (*endptr != 0) {
					/* Not a proper number; ignore this macro */
					if (macro != NULL) {
						free(macro);
						macro = NULL;
						break;
					}
				}
				if (macro == NULL) {
					if (n < 38 || n > 255)
						/* Macro code out of range; ignore this macro */
						break;
					macro = (SkinMacro *) malloc(sizeof(SkinMacro));
					macro->code = n;
				} else if (len < SKIN_MAX_MACRO_LENGTH) {
					if (n < 1 || n > 37) {
						/* Key code out of range; ignore this macro */
						free(macro);
						macro = NULL;
						break;
					}
					macro->macro[len++] = (unsigned char) n;
				}
				tok = strtok(NULL, " ");
			}
			if (macro != NULL) {
				macro->macro[len++] = 0;
				macro->next = macrolist;
				macrolist = macro;
			}
		} else if (_strnicmp(line, "annunciator:", 12) == 0) {
			int annnum;
			int disp_x, disp_y, disp_width, disp_height;
			int act_x, act_y;
			if (sscanf(line + 12, " %d %d,%d,%d,%d %d,%d",
								  &annnum,
								  &disp_x, &disp_y, &disp_width, &disp_height,
								  &act_x, &act_y) == 7) {
				if (annnum >= 1 && annnum <= 7) {
					SkinAnnunciator *ann = annunciators + (annnum - 1);
					ann->disp_rect.x = disp_x;
					ann->disp_rect.y = disp_y;
					ann->disp_rect.width = disp_width;
					ann->disp_rect.height = disp_height;
					ann->src.x = act_x;
					ann->src.y = act_y;
				}
			}
		} else if (strchr(line, ':') != 0) {
			keymap_entry *entry = parse_keymap_entry(line, lineno);
			if (entry != NULL) {
				if (keymap_length == kmcap) {
					kmcap += 50;
					keymap = (keymap_entry *) realloc(keymap, kmcap * sizeof(keymap_entry));
				}
				memcpy(keymap + (keymap_length++), entry, sizeof(keymap_entry));
			}
		}
	}

	if (display_scale.x == 0)
		landscape = false;

	skin_close();

	/********************************************************************/
	/* Compute optimum magnification level, and adjust skin description */
	/********************************************************************/

	int xs = width / skin.width;
	int ys = height / skin.height;
	magnification = xs < ys ? xs : ys;
	if (magnification < 1)
		magnification = 1;
	else if (magnification > 1) {
		if (magnification > 4)
			// In order to support magnifications of more than 4,
			// the monochrome pixel-replication code in skin_put_pixels()
			// needs to be modified; currently, it only supports magnifications
			// or 1, 2, 3, and 4.
			magnification = 4;
		skin.x *= magnification;
		skin.y *= magnification;
		skin.width *= magnification;
		skin.height *= magnification;
		display_loc.x *= magnification;
		display_loc.y *= magnification;
		if (display_scale.x == 0)
			// This is the special hack to get the most out of the QVGA's
			// 240-pixel width by doubling 4 out of every 6 pixels; if we're
			// on a larger screen, fall back on a tidy integral scale factor
			// and just leave some screen space unused.
			display_scale.x = (int) (magnification * 1.67);
		else
			display_scale.x *= magnification;
		display_scale.y *= magnification;
		int i;
		for (i = 0; i < nkeys; i++) {
			SkinKey *key = keylist + i;
			key->sens_rect.x *= magnification;
			key->sens_rect.y *= magnification;
			key->sens_rect.width *= magnification;
			key->sens_rect.height *= magnification;
			key->disp_rect.x *= magnification;
			key->disp_rect.y *= magnification;
			key->disp_rect.width *= magnification;
			key->disp_rect.height *= magnification;
			key->src.x *= magnification;
			key->src.y *= magnification;
		}
		for (i = 0; i < 7; i++) {
			SkinAnnunciator *ann = annunciators + i;
			ann->disp_rect.x *= magnification;
			ann->disp_rect.y *= magnification;
			ann->disp_rect.width *= magnification;
			ann->disp_rect.height *= magnification;
			ann->src.x *= magnification;
			ann->src.y *= magnification;
		}
	}

	/********************/
	/* Load skin bitmap */
	/********************/

	if (!skin_open(skinname, basedir, 0))
		goto fallback_on_1st_builtin_skin;

	/* shell_loadimage() calls skin_getchar() and skin_rewind() to load the
	 * image from the compiled-in or on-disk file; it calls skin_init_image(),
	 * skin_put_pixels(), and skin_finish_image() to create the in-memory
	 * representation.
	 */
	success = shell_loadimage();
	skin_close();

	if (!success)
		goto fallback_on_1st_builtin_skin;

	/********************************/
	/* (Re)build the display bitmap */
	/********************************/

	if (disp_bitmap != NULL)
		free(disp_bitmap);

	int lcd_w, lcd_h;
	if (landscape) {
		lcd_w = 16;
		lcd_h = 131;
	} else {
		lcd_w = 131;
		lcd_h = 16;
	}
	if (display_scale.x == 0)
		disp_bytesperline = 28;
	else
		disp_bytesperline = ((lcd_w * display_scale.x + 15) >> 3) & ~1;
	size = disp_bytesperline * lcd_h * display_scale.y;
	disp_bitmap = (unsigned char *) malloc(size);
	memset(disp_bitmap, 255, size);
}

int skin_init_image(int type, int ncolors, const SkinColor *colors,
					int width, int height) {
	if (skin_bitmap != NULL) {
		free(skin_bitmap);
		skin_bitmap = NULL;
	}
	if (skin_dib != NULL) {
		DeleteObject(skin_dib);
		skin_dib = NULL;
	}
	if (skin_header != NULL) {
		free(skin_header);
		skin_header = NULL;
	}

	skin_type = type;
	skin_ncolors = ncolors;
	skin_colors = colors;

	width *= magnification;
	height *= magnification;
	
	switch (type) {
		case IMGTYPE_MONO:
			skin_bytesperline = ((width + 15) >> 3) & ~1;
			break;
		case IMGTYPE_GRAY:
		case IMGTYPE_COLORMAPPED:
			skin_bytesperline = (width + 3) & ~3;
			break;
		case IMGTYPE_TRUECOLOR:
			skin_bytesperline = (width * 3 + 3) & ~3;
			break;
		default:
			return 0;
	}

	skin_bitmap = (unsigned char *) malloc(skin_bytesperline * height);
	skin_width = width;
	skin_height = height;
	skin_y = 0;
	return skin_bitmap != NULL;
}

void skin_put_pixels(unsigned const char *data) {
	unsigned char *dst = skin_bitmap + skin_y * skin_bytesperline;
	if (magnification == 1) {
		if (skin_type == IMGTYPE_MONO) {
			for (int i = 0; i < skin_bytesperline; i++) {
				unsigned char c = data[i];
				c = (c >> 7) | ((c >> 5) & 2) | ((c >> 3) & 4) | ((c >> 1) & 8)
					| ((c << 1) & 16) | ((c << 3) & 32) | ((c << 5) & 64) | (c << 7);
				dst[i] = c;
			}
		} else if (skin_type == IMGTYPE_TRUECOLOR) {
			for (int i = 0; i < skin_width; i++) {
				data++;
				*dst++ = *data++;
				*dst++ = *data++;
				*dst++ = *data++;
			}
		} else
			memcpy(dst, data, skin_bytesperline);
		skin_y++;
	} else {
		if (skin_type == IMGTYPE_MONO) {
			if (magnification == 2) {
				int i = 0;
				while (true) {
					unsigned char c = *data++;
					dst[i++] = (((c << 6) & 64) | ((c << 3) & 16) | (c & 4) | ((c >> 3) & 1)) * 3;
					if (i == skin_bytesperline)
						break;
					dst[i++] = (((c << 2) & 64) | ((c >> 1) & 16) | ((c >> 4) & 4) | (c >> 7)) * 3;
					if (i == skin_bytesperline)
						break;
				}
			} else if (magnification == 3) {
				int i = 0;
				while (true) {
					unsigned char c = *data++;
					dst[i++] = (((c << 5) & 32) | ((c << 1) & 4)) * 7 + ((c >> 2) & 1) * 3;
					if (i == skin_bytesperline)
						break;
					dst[i++] = ((c << 5) & 128) + (((c << 1) & 16) | ((c >> 3) & 2)) * 7 + ((c >> 5) & 1);
					if (i == skin_bytesperline)
						break;
					dst[i++] = (((c << 1) & 64) | ((c >> 3) & 8) | (c >> 7)) * 7;
					if (i == skin_bytesperline)
						break;
				}
			} else {
				// magnification must be 4 now; it's the highest value this bitmap
				// scaling code supports.
				int i = 0, j = 0;
				while (true) {
					unsigned char c = *data++;
					dst[i++] = (((c << 4) & 16) | ((c >> 1) & 1)) * 15;
					if (i == skin_bytesperline)
						break;
					dst[i++] = (((c << 2) & 16) | ((c >> 3) & 1)) * 15;
					if (i == skin_bytesperline)
						break;
					dst[i++] = ((c & 16) | ((c >> 5) & 1)) * 15;
					if (i == skin_bytesperline)
						break;
					dst[i++] = (((c >> 2) & 16) | ((c >> 7) & 1)) * 15;
					if (i == skin_bytesperline)
						break;
				}
			}
		} else if (skin_type == IMGTYPE_TRUECOLOR) {
			unsigned char *p = dst;
			for (int i = 0; i < skin_width; i++) {
				unsigned char r, g, b;
				data++;
				r = *data++;
				g = *data++;
				b = *data++;
				for (int j = 0; j < magnification; j++) {
					*p++ = r;
					*p++ = g;
					*p++ = b;
				}
			}
		} else {
			unsigned char *p = dst;
			for (int i = 0; i < skin_width; i++) {
				unsigned char c = *data++;
				for (int j = 0; j < magnification; j++)
					*p++ = c;
			}
		}
		for (int i = 1; i < magnification; i++) {
			unsigned char *p = dst + skin_bytesperline;
			memcpy(p, dst, skin_bytesperline);
			dst = p;
		}
		skin_y += magnification;
	}
}

void skin_finish_image() {
	BITMAPINFOHEADER *bh;
	
	if (skin_type == IMGTYPE_MONO) {
		skin_dib = CreateBitmap(skin_width, skin_height, 1, 1, skin_bitmap);
		skin_header = NULL;
		return;
	}

	if (skin_type == IMGTYPE_COLORMAPPED) {
		RGBQUAD *cmap;
		int i;
		bh = (BITMAPINFOHEADER *) malloc(sizeof(BITMAPINFOHEADER) + skin_ncolors * sizeof(RGBQUAD));
		cmap = (RGBQUAD *) (bh + 1);
		for (i = 0; i < skin_ncolors; i++) {
			cmap[i].rgbRed = skin_colors[i].r;
			cmap[i].rgbGreen = skin_colors[i].g;
			cmap[i].rgbBlue = skin_colors[i].b;
			cmap[i].rgbReserved = 0;
		}
	} else if (skin_type == IMGTYPE_GRAY) {
		RGBQUAD *cmap;
		int i;
		bh = (BITMAPINFOHEADER *) malloc(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
		cmap = (RGBQUAD *) (bh + 1);
		for (i = 0; i < 256; i++) {
			cmap[i].rgbRed = cmap[i].rgbGreen = cmap[i].rgbBlue = i;
			cmap[i].rgbReserved = 0;
		}
	} else
		bh = (BITMAPINFOHEADER *) malloc(sizeof(BITMAPINFOHEADER));

	bh->biSize = sizeof(BITMAPINFOHEADER);
	bh->biWidth = skin_width;
	bh->biHeight = -skin_height;
	bh->biPlanes = 1;
	switch (skin_type) {
		case IMGTYPE_MONO:
			bh->biBitCount = 1;
			bh->biClrUsed = 0;
			break;
		case IMGTYPE_GRAY:
			bh->biBitCount = 8;
			bh->biClrUsed = 256;
			break;
		case IMGTYPE_COLORMAPPED:
			bh->biBitCount = 8;
			bh->biClrUsed = skin_ncolors;
			break;
		case IMGTYPE_TRUECOLOR:
			bh->biBitCount = 24;
			bh->biClrUsed = 0;
			break;
	}
	bh->biCompression = BI_RGB;
	bh->biSizeImage = skin_bytesperline * skin_height;
	bh->biXPelsPerMeter = 2835;
	bh->biYPelsPerMeter = 2835;
	bh->biClrImportant = 0;
	
	skin_header = bh;
}

static int make_dib(HDC hdc) {
	void *bits;
	if (skin_type == IMGTYPE_MONO) {
		/* Not using a DIB, but a regular monochrome bitmap;
		 * this bitmap was already created in skin_finish_image(),
		 * since, unlike a DIB, no DC is required to create it.
		 */
		return 1;
	}
	if (skin_header == NULL)
		return 0;
	if (skin_dib == NULL) {
		skin_dib = CreateDIBSection(hdc, (BITMAPINFO *) skin_header, DIB_RGB_COLORS, &bits, NULL, 0);
		if (skin_dib == NULL)
			return 0;
		memcpy(bits, skin_bitmap, skin_bytesperline * skin_height);
		free(skin_bitmap);
		skin_bitmap = NULL;
	}
	return 1;
}

void skin_repaint(HDC hdc, HDC memdc) {
	COLORREF old_bg, old_fg;
	if (!make_dib(memdc))
		return;
	SelectObject(memdc, skin_dib);
	if (skin_type == IMGTYPE_MONO) {
		old_bg = SetBkColor(hdc, 0x00ffffff);
		old_fg = SetTextColor(hdc, 0x00000000);
	}
	BitBlt(hdc, 0, 0, skin.width, skin.height, memdc, skin.x, skin.y, SRCCOPY);
	if (skin_type == IMGTYPE_MONO) {
		SetBkColor(hdc, old_bg);
		SetTextColor(hdc, old_fg);
	}
}

void skin_repaint_annunciator(HDC hdc, HDC memdc, int which, int state) {
	SkinAnnunciator *ann = annunciators + (which - 1);
	COLORREF old_bg, old_fg;
	if (!make_dib(memdc))
		return;
	SelectObject(memdc, skin_dib);
	if (skin_type == IMGTYPE_MONO) {
		old_bg = SetBkColor(hdc, 0x00ffffff);
		old_fg = SetTextColor(hdc, 0x00000000);
	}
	if (state)
		BitBlt(hdc, ann->disp_rect.x, ann->disp_rect.y, ann->disp_rect.width, ann->disp_rect.height,
			   memdc, ann->src.x, ann->src.y, SRCCOPY);
	else
		BitBlt(hdc, ann->disp_rect.x, ann->disp_rect.y, ann->disp_rect.width, ann->disp_rect.height,
			   memdc, ann->disp_rect.x, ann->disp_rect.y, SRCCOPY);
	if (skin_type == IMGTYPE_MONO) {
		SetBkColor(hdc, old_bg);
		SetTextColor(hdc, old_fg);
	}
}

void skin_find_key(int x, int y, bool cshift, int *skey, int *ckey) {
	int i;
	if (core_menu()) {
		if (landscape) {
			if (x >= display_loc.x + 9 * display_scale.x
					&& x < display_loc.x + 16 * display_scale.x
					&& y >= display_loc.y
					&& y < display_loc.y + 131 * display_scale.y) {
				int softkey = 6 - (y - display_loc.y) / (22 * display_scale.y);
				*skey = -1 - softkey;
				*ckey = softkey;
				return;
			}
		} else {
			int dw = display_scale.x == 0 ? 219 : 131 * display_scale.x;
			if (x >= display_loc.x
					&& x < display_loc.x + dw
					&& y >= display_loc.y + 9 * display_scale.y
					&& y < display_loc.y + 16 * display_scale.y) {
				int softkey;
				if (display_scale.x == 0)
					softkey = ((x - display_loc.x + 37) * 3) / 110;
				else
					softkey = (x - display_loc.x) / (22 * display_scale.x) + 1;
				*skey = -1 - softkey;
				*ckey = softkey;
				return;
			}
		}
	}
	for (i = 0; i < nkeys; i++) {
		SkinKey *k = keylist + i;
		int rx = x - k->sens_rect.x;
		int ry = y - k->sens_rect.y;
		if (rx >= 0 && rx < k->sens_rect.width
				&& ry >= 0 && ry < k->sens_rect.height) {
			*skey = i;
			*ckey = cshift ? k->shifted_code : k->code;
			return;
		}
	}
	*skey = -1;
	*ckey = 0;
}

int skin_find_skey(int ckey) {
	int i;
	for (i = 0; i < nkeys; i++)
		if (keylist[i].code == ckey || keylist[i].shifted_code == ckey)
			return i;
	return -1;
}

unsigned char *skin_find_macro(int ckey) {
	SkinMacro *m = macrolist;
	while (m != NULL) {
		if (m->code == ckey)
			return m->macro;
		m = m->next;
	}
	return NULL;
}

unsigned char *skin_keymap_lookup(int keycode, bool ctrl, bool alt, bool shift, bool cshift, bool *exact) {
	int i;
	unsigned char *macro = NULL;
	for (i = 0; i < keymap_length; i++) {
		keymap_entry *entry = keymap + i;
		if (keycode == entry->keycode
				&& ctrl == entry->ctrl
				&& alt == entry->alt
				&& shift == entry->shift) {
			macro = entry->macro;
			if (cshift == entry->cshift) {
				*exact = true;
				return macro;
			}
		}
	}
	*exact = false;
	return macro;
}

void skin_repaint_key(HDC hdc, HDC memdc, int key, int state) {
	SkinKey *k;
	COLORREF old_bg, old_fg;

	if (key >= -7 && key <= -2) {
		/* Soft key */
		int x, y, w, h;
		HBITMAP bitmap;
		if (state) {
			old_bg = SetBkColor(hdc, display_fg);
			old_fg = SetTextColor(hdc, display_bg);
		} else {
			old_bg = SetBkColor(hdc, display_bg);
			old_fg = SetTextColor(hdc, display_fg);
		}
		key = -1 - key;
		if (landscape) {
			x = 9 * display_scale.x;
			y = (6 - key) * 22 * display_scale.y;
			w = 16 * display_scale.x;
			h = 131 * display_scale.y;
		} else {
			if (display_scale.x == 0) {
				x = ((key + 1) * 110) / 3 - 73;
				w = 219;
			} else {
				x = (key - 1) * 22 * display_scale.x;
				w = 131 * display_scale.x;
			}
			y = 9 * display_scale.y;
			h = 16 * display_scale.y;
		}
		bitmap = CreateBitmap(w, h, 1, 1, disp_bitmap);
		SelectObject(memdc, bitmap);
		BitBlt(hdc, display_loc.x + x, display_loc.y + y,
			   display_scale.x == 0
						? key == 2 || key == 5 ? 36 : 35
						: (landscape ? 7 : 21) * display_scale.x,
			   (landscape ? 21 : 7) * display_scale.y,
			   memdc, x, y, SRCCOPY);
		SetBkColor(hdc, old_bg);
		SetTextColor(hdc, old_fg);
		DeleteObject(bitmap);
		return;
	}

	if (key < 0 || key >= nkeys)
		return;
	if (!make_dib(memdc))
		return;
	SelectObject(memdc, skin_dib);
	k = keylist + key;
	if (skin_type == IMGTYPE_MONO) {
		old_bg = SetBkColor(hdc, 0x00ffffff);
		old_fg = SetTextColor(hdc, 0x00000000);
	}
	if (state)
		BitBlt(hdc, k->disp_rect.x, k->disp_rect.y, k->disp_rect.width, k->disp_rect.height,
			   memdc, k->src.x, k->src.y, SRCCOPY);
	else
		BitBlt(hdc, k->disp_rect.x, k->disp_rect.y, k->disp_rect.width, k->disp_rect.height,
			   memdc, k->disp_rect.x, k->disp_rect.y, SRCCOPY);
	if (skin_type == IMGTYPE_MONO) {
		SetBkColor(hdc, old_bg);
		SetTextColor(hdc, old_fg);
	}
}

void skin_display_blitter(HDC hdc, const char *bits, int bytesperline, int x, int y,
									 int width, int height) {
	int h, v, hh, vv;
	int sx = display_scale.x;
	int sy = display_scale.y;
	HDC memdc;
	HBITMAP bitmap;
	COLORREF old_bg, old_fg;
	int disp_w, disp_h;

	if (landscape) {
		for (v = y; v < y + height; v++)
			for (h = x; h < x + width; h++) {
				int pixel = (bits[v * bytesperline + (h >> 3)] & (1 << (h & 7))) == 0;
				for (vv = (130 - h) * sy; vv < (131 - h) * sy; vv++)
					for (hh = v * sx; hh < (v + 1) * sx; hh++)
						if (pixel)
							disp_bitmap[vv * disp_bytesperline + (hh >> 3)] |= 128 >> (hh & 7);
						else
							disp_bitmap[vv * disp_bytesperline + (hh >> 3)] &= ~(128 >> (hh & 7));
			}
		disp_w = 16;
		disp_h = 131;
		int tmp = x;
		x = y;
		y = 131 - x - width;
		tmp = width;
		width = height;
		height = tmp;
	} else {
		for (v = y; v < y + height; v++)
			for (h = x; h < x + width; h++) {
				int pixel = (bits[v * bytesperline + (h >> 3)] & (1 << (h & 7))) == 0;
				for (vv = v * sy; vv < (v + 1) * sy; vv++) {
					int hhmax;
					if (sx == 0) {
						int a = h / 6;
						int b = h - (a << 1) - (a << 2);
						hh = a * 10 + (b << 1);
						if (b > 1)
							hh--;
						hhmax = hh + (b == 1 || b == 5 ? 1 : 2);
					} else {
						hh = h * sx;
						hhmax = hh + sx;
					}
					for (; hh < hhmax; hh++)
						if (pixel)
							disp_bitmap[vv * disp_bytesperline + (hh >> 3)] |= 128 >> (hh & 7);
						else
							disp_bitmap[vv * disp_bytesperline + (hh >> 3)] &= ~(128 >> (hh & 7));
				}
			}
		disp_w = 131;
		disp_h = 16;
	}
	
	memdc = CreateCompatibleDC(hdc);
	bitmap = CreateBitmap(sx == 0 ? 219 : (disp_w * sx), disp_h * sy, 1, 1, disp_bitmap);
	SelectObject(memdc, bitmap);

	old_bg = SetBkColor(hdc, display_bg);
	old_fg = SetTextColor(hdc, display_fg);
	int bx, bw;
	if (sx == 0) {
		int a = x / 6;
		int b = x - (a << 1) - (a << 2);
		bx = a * 10 + (b << 1);
		if (b > 1)
			bx--;
		width += x;
		a = width / 6;
		b = width - (a << 1) - (a << 2);
		bw = a * 10 + (b << 1);
		if (b > 1)
			bw--;
		bw -= bx;
	} else {
		bx = x * sx;
		bw = width * sx;
	}
	BitBlt(hdc, display_loc.x + bx, display_loc.y + y * sy,
				bw, height * sy, memdc, bx, y * sy, SRCCOPY);
	SetBkColor(hdc, old_bg);
	SetTextColor(hdc, old_fg);

	DeleteDC(memdc);
	DeleteObject(bitmap);
}

void skin_repaint_display(HDC hdc, HDC memdc) {
	int w, h;
	if (landscape) {
		w = 16 * display_scale.x;
		h = 131 * display_scale.y;
	} else {
		if (display_scale.x == 0)
			w = 219;
		else
			w = 131 * display_scale.x;
		h = 16 * display_scale.y;
	}
	HBITMAP bitmap = CreateBitmap(w, h, 1, 1, disp_bitmap);
	COLORREF old_bg, old_fg;

	SelectObject(memdc, bitmap);

	old_bg = SetBkColor(hdc, display_bg);
	old_fg = SetTextColor(hdc, display_fg);
	BitBlt(hdc, display_loc.x, display_loc.y, w, h, memdc, 0, 0, SRCCOPY);
	SetBkColor(hdc, old_bg);
	SetTextColor(hdc, old_fg);

	DeleteObject(bitmap);
}
