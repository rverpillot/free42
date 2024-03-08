#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core_main.h"
#include "core_aux.h"

extern "C"
{

#include <main.h>
#include <dmcp.h>
#include <ini.h>

#include <dm42_menu.h>
#include <dm42_fns.h>

static const char *dm42_keys[] = {
    "SIGMA", "INV", "SQRT", "LOG", "LN", "XEQ",
    "STO", "RCL", "RDN", "SIN", "COS", "TAN",
    "ENTER", "SWAP", "CHS", "E", "BSP", 
    "UP", "7", "8", "9", "DIV", 
    "DOWN", "4", "5", "6", "MUL",
    "SHIFT", "1", "2", "3", "SUB",
    "EXIT", "0", "DOT", "RUN", "ADD",
    "F1", "F2", "F3", "F4", "F5", "F6",
};

const char *keycode2keyname(int keycode) {
    if (keycode < 0 || keycode >= MAX_FNKEY_NR) {
        return NULL;
    }
    return dm42_keys[keycode-1];
}

static int keyname2keycode(const char *keyname) {
    for (int i = 0; i < MAX_FNKEY_NR; i++) {
        if (strcasecmp(dm42_keys[i], keyname) == 0) {
            return i+1;
        }
    }
    return -1;
}

struct dm42_macro {
    char keyname[12];
    uint8_t keys[10];
    uint len;
    struct dm42_macro *next;
};

struct keymap {
    char name[16];
    struct dm42_macro *macros;    
    struct keymap *next;
};

static struct keymap *keymaps = NULL;
static struct keymap *current_keymap = NULL;

static void macro_free_keymaps() {
    struct keymap *km = keymaps;
    while (km != NULL) {
        struct keymap *next = km->next;
        struct dm42_macro *macro = km->macros;
        while (macro != NULL) {
            struct dm42_macro *next = macro->next;
            free(macro);
            macro = next;
        }
        free(km);
        km = next;
    }
    keymaps = NULL;
    current_keymap = NULL;
}

static int keymaps_load_ini_handler(void* user, const char* section, const char* name,const char* value) {
    if (keymaps == NULL || strcmp(current_keymap->name, section) != 0) {
        struct keymap *km = (struct keymap *)malloc(sizeof(struct keymap));
        if (km == NULL) {
            return 0;
        }
        strncpy(km->name, section, sizeof(km->name)-1);
        km->next = NULL;
        km->macros = NULL;
        if (keymaps == NULL) {
            keymaps = km;
        } else {
            current_keymap->next = km;
        }
        current_keymap = km;
    }
    // split value into keys
    struct dm42_macro *macro = (struct dm42_macro *)malloc(sizeof(struct dm42_macro));
    if (macro == NULL) {
        return 0;
    }
    strncpy(macro->keyname, name, 11);
    macro->len = 0;
    macro->next = NULL;
    char *p = (char *)value;
    // split value with space sperator using strtok
    char *token = strtok(p, " ");
    while (token != NULL) {
        int keycode = keyname2keycode(token);
        if (keycode >= 0) {
            macro->keys[macro->len++] = keycode;
        }
        token = strtok(NULL, " ");
    }
    if (current_keymap->macros == NULL) {
        current_keymap->macros = macro;
    } else {
        struct dm42_macro *m = current_keymap->macros;
        while (m->next != NULL) {
            m = m->next;
        }
        m->next = macro;
    }

    return 1;
}

int keymaps_load_callback(const char *fpath, const char *fname, void *data) {
    lcd_puts(t24, "Loading ...");
    lcd_puts(t24, fname);
    lcd_refresh();
    macro_free_keymaps();
    FIL file;
    if (f_open(&file, fpath, FA_READ) != FR_OK) {
        lcd_puts(t24, "Fail to open.");
        lcd_refresh();
        wait_for_key_press();
        return 0;
    }
    uint size = f_size(&file);
    char *buffer = (char *)malloc(size + 1);
    if (buffer == NULL) {
        lcd_puts(t24, "Fail to alloc.");
        lcd_refresh();
        wait_for_key_press();
        f_close(&file);
        return 0;
    }
    uint read;
    f_read(&file, buffer, size, &read);
    f_close(&file);
    buffer[read] = '\0';
    if (ini_parse_string(buffer, keymaps_load_ini_handler, NULL) < 0) {
        lcd_puts(t24, "Fail to parse.");
        lcd_refresh();
        wait_for_key_press();
    } else {
        lcd_puts(t24, "Success");
        lcd_refresh();
        sys_delay(1500);
    }
    free(buffer);
    return MRET_EXIT;
}

void macro_set_keymap(const char *keymap) {
    struct keymap *km = keymaps;
    while (km != NULL) {
        if (strcmp(km->name, keymap) == 0) {
            current_keymap = km;
            return;
        }
        km = km->next;
    }
}

const char *macro_get_keymap() {
    if (current_keymap == NULL) {
        return NULL;
    }
    return current_keymap->name;
}

bool macro_find_keymap(int num, char *keymap, int size) {
    struct keymap *km = keymaps;
    while (km != NULL) {
        if (num-- == 0) {
            strncpy(keymap, km->name, size);
            return true;
        }
        km = km->next;
    }
    return false;
}

int macro_get_keys(const char *keyname, uint8_t keys[], uint len) {
    if (current_keymap == NULL) {
        return 0;
    }
    struct dm42_macro *macro = current_keymap->macros;
    while (macro != NULL) {
        if (strcasecmp(macro->keyname, keyname) == 0) {
            if (macro->len > len) {
                return 0;
            }
            memcpy(keys, macro->keys, macro->len);
            return macro->len;
        }
        macro = macro->next;
    }
    return 0;
}

} // extern "C"
