/*****************************************************************************
 * Free42 -- an HP-42S calculator simulator
 * Copyright (C) 2004-2022  Thomas Okken
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
 * along with this program; if not, see http://www.gnu.org/licenses/.
 *****************************************************************************/

#ifndef CORE_HELPERS_H
#define CORE_HELPERS_H 1


#include "free42.h"
#include "core_phloat.h"
#include "core_globals.h"


/*********************/
/* Utility functions */
/*********************/

int resolve_ind_arg(arg_struct *arg, char *buf = NULL, int *len = NULL);
int arg_to_num(arg_struct *arg, int4 *num);
int recall_result_silently(vartype *v);
int recall_result(vartype *v);
int recall_two_results(vartype *x, vartype *y);
void unary_result(vartype *x);
int unary_two_results(vartype *x, vartype *y);
int binary_result(vartype *x);
void binary_two_results(vartype *x, vartype *y);
int ternary_result(vartype *x);
bool ensure_stack_capacity(int n);
void shrink_stack();
phloat rad_to_angle(phloat x);
phloat rad_to_deg(phloat x);
phloat deg_to_rad(phloat x);
void append_alpha_char(char c);
void append_alpha_string(const char *buf, int buflen, int reverse);

void string_copy(char *dst, int *dstlen, const char *src, int srclen);
bool string_equals(const char *s1, int s1len, const char *s2, int s2len);
int string_pos(const char *ntext, int nlen, const vartype *hs, int startpos);
bool vartype_equals(const vartype *v1, const vartype *v2);
int anum(const char *text, int len, phloat *res);

#define FLAGOP_SF 0
#define FLAGOP_CF 1
#define FLAGOP_FS_T 2
#define FLAGOP_FC_T 3
#define FLAGOP_FSC_T 4
#define FLAGOP_FCC_T 5

int virtual_flag_handler(int flagop, int flagnum);

int get_base();
void set_base(int base, bool a_thru_f = false);
int get_base_param(const vartype *v, int8 *n);
int base_range_check(int8 *n, bool force_wrap);
int effective_wsize();
phloat base2phloat(int8 n);
bool phloat2base(phloat p, int8 *n);

void print_text(const char *text, int length, bool left_justified);
void print_lines(const char *text, int length, bool left_justified);
void print_right(const char *left, int leftlen,
                 const char *right, int rightlen);
void print_wide(const char *left, int leftlen,
                const char *right, int rightlen);
void print_command(int cmd, const arg_struct *arg);
void print_trace();
void print_stack_trace();

void generic_r2p(phloat re, phloat im, phloat *r, phloat *phi);
void generic_p2r(phloat r, phloat phi, phloat *re, phloat *im);

phloat sin_deg(phloat x);
phloat sin_grad(phloat x);
phloat cos_deg(phloat x);
phloat cos_grad(phloat x);

/***********************/
/* Miscellaneous stuff */
/***********************/

int dimension_array(const char *name, int namelen, int4 rows, int4 columns, bool check_matedit);
int dimension_array_ref(vartype *matrix, int4 rows, int4 columns);

phloat fix_hms(phloat x);

void char2buf(char *buf, int buflen, int *bufptr, char c);
void string2buf(char *buf, int buflen, int *bufptr, const char *s, int slen);
void cmdnam2buf(char *buf, int buflen, int *bufptr, const char *s, int slen);
int uint2string(uint4 n, char *buf, int buflen);
int int2string(int4 n, char *buf, int buflen);
int ulong2string(uint8 n, char *buf, int buflen);
int vartype2string(const vartype *v, char *buf, int buflen, int max_mant_digits = 12);
const char *phloat2program(phloat d);
int easy_phloat2string(phloat d, char *buf, int buflen, int base_mode);
int ip2revstring(phloat d, char *buf, int buflen);

#ifdef ARM
#define mallocU(x) unguarded_malloc(x,__FILE__,__LINE__)
#define reallocU(x,y) unguarded_realloc(x,y,__FILE__,__LINE__)
void* unguarded_malloc(size_t size, const char* file, int line);
void* unguarded_realloc(void *ptr, size_t size, const char* file, int line);

extern "C" {
// ------------
// Map filesystem functions to statefile open/read/write/etc.
// read/write return int to avoid warnings in free42 code
#define FILE int
int statefile_read(void *ptr, size_t size, size_t nmemb, FILE *stream);
int statefile_write(const void *ptr, size_t size, size_t nmemb, FILE *stream);
FILE *statefile_open(const char *pathname, const char *mode);
int statefile_close(FILE *stream);
int statefile_seek(FILE *stream, long offset, int whence);
int statefile_getc(FILE *stream);
int statefile_ungetc(int c, FILE *stream);
long statefile_tell(FILE *stream);
int statefile_putc(int c, FILE *stream);

#define fread  statefile_read
#define fwrite statefile_write
#define fopen  statefile_open
#define fclose statefile_close
#define fseek  statefile_seek 
#define fgetc  statefile_getc
#define ungetc statefile_ungetc
#define ftell  statefile_tell
#define fputc  statefile_putc
// ------------
}

#else
#define mallocU  malloc
#define reallocU realloc
#endif

#endif
