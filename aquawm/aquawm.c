#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/wait.h>
#include <time.h>
#include <stdatomic.h>

#define TITLEBAR_HEIGHT 26
#define BUTTON_SIZE 16
#define BUTTON_SPACING 8
#define MIN_WIDTH 150
#define MIN_HEIGHT 100
#define RESIZE_HANDLE_SIZE 6
#define CONFIG_DIR ".config/aquawm"
#define CONFIG_FILE "conf.ini"
#define SOCKET_PORT 12345
#define MAX_COMMAND_LEN 256
#define MAX_WORKSPACES 6
#define MAX_PATH 1024
#define MAX_CLIENTS 512
#define MAX_KEYBINDINGS 20
#define MAX_THEMES 10

typedef struct {
    Window window;
    Window frame;
    int x, y;
    int width, height;
    int frame_x, frame_y;
    int orig_x, orig_y;
    int orig_width, orig_height;
    Bool mapped;
    Bool decorated;
    char *title;
    int ignore_unmap;
    int is_active;
    int is_maximized;
    int is_fullscreen;
    int is_dock;
    int dock_side;
    int strut_size;
    int workspace;
    int is_fixed;
    int urgent;
    int skip_taskbar;
    int skip_pager;
    int modal;
    Window transient_for;
    int gtk_left, gtk_right, gtk_top, gtk_bottom;
    int has_gtk_extents;
    int is_sdl_app;
    int is_override_redirect;
    int is_transient;
    int alt_move_active;
    int force_center;
    int minimized;
    int minimized_state;
} Client;

typedef struct {
    char keysym[32];
    char modifiers[32];
    char command[64];
} KeyBinding;

typedef struct {
    int border_width;
    int frame_width;
    char wallpaper[MAX_PATH];
    int gradient_steps;
    int show_grid;
    int enable_compositing;
    char font_name[MAX_PATH];
    int font_size;
    int focus_follows_mouse;
    int raise_on_focus;
    int double_click_time;
    int edge_resistance;
    int opaque_resize;
    int center_new_windows;
    int shadow_enabled;
    int shadow_size;
    unsigned int shadow_color;
    int titlebar_gradient;
    unsigned int active_title_color;
    unsigned int inactive_title_color;
    int window_animation;
    int animation_duration;
    KeyBinding keybindings[MAX_KEYBINDINGS];
    int keybinding_count;
    int screen_margin;
    int screen_top_margin;
    int screen_bottom_margin;
    int dock_padding;
    int smart_gaps;
    int gaps;
    char wm_theme[32];
} Config;

typedef struct {
    Display *display;
    int screen;
    Window root;
    Visual *visual;
    int depth;
    int screen_width;
    int screen_height;
    int workarea_x, workarea_y, workarea_width, workarea_height;

    Pixmap desktop_pixmap;
    GC desktop_gc;

    Atom wm_protocols;
    Atom wm_delete_window;
    Atom wm_take_focus;
    Atom net_wm_state;
    Atom net_wm_state_maximized_vert;
    Atom net_wm_state_maximized_horz;
    Atom net_wm_state_fullscreen;
    Atom net_active_window;
    Atom wm_transient_for;
    Atom utf8_string;
    Atom net_wm_window_type;
    Atom net_wm_window_type_dock;
    Atom net_wm_window_type_desktop;
    Atom net_wm_strut;
    Atom net_wm_strut_partial;
    Atom net_wm_state_above;
    Atom net_wm_state_stays_on_top;
    Atom net_wm_state_below;
    Atom net_wm_state_modal;
    Atom net_wm_window_type_dialog;
    Atom net_wm_window_type_normal;
    Atom net_wm_desktop;
    Atom net_wm_state_hidden;
    Atom net_wm_state_shaded;
    Atom net_wm_state_skip_taskbar;
    Atom net_wm_state_skip_pager;
    Atom net_wm_state_demands_attention;
    Atom net_wm_name;
    Atom net_supporting_wm_check;
    Atom net_supported;
    Atom net_number_of_desktops;
    Atom net_current_desktop;
    Atom net_client_list;
    Atom net_client_list_stacking;
    Atom net_wm_window_opacity;
    Atom net_wm_user_time;
    Atom motif_wm_hints;

    Client *clients[MAX_CLIENTS];
    int num_clients;
    Client *active_client;
    Client *moving_client;
    Client *grabbed_client;

    Cursor cursor_normal;
    Cursor cursor_resize;
    Cursor cursor_move;
    Cursor cursor_resize_hor;
    Cursor cursor_resize_ver;
    Cursor cursor_resize_diag1;
    Cursor cursor_resize_diag2;
    Cursor cursor_wait;
    Cursor cursor_crosshair;

    int drag_start_x;
    int drag_start_y;
    int is_moving;
    int is_resizing;
    int resize_edge;
    int move_start_x;
    int move_start_y;
    unsigned int drag_button;

    int mouse_grabbed;
    Window grab_window;

    GC frame_gc;
    GC button_gc;
    GC border_gc;
    GC highlight_gc;
    GC text_gc;
    GC titlebar_gc;
    GC shadow_gc;
    GC button_bg_gc;
    GC button_fg_gc;

    Config config;

    int socket_fd;
    atomic_int reload_requested;
    pthread_t socket_thread;
    atomic_int socket_active;

    int current_workspace;
    int should_exit;

    Window wm_window;
    Colormap colormap;

    int allow_input_focus;
    time_t last_click_time;
    Window last_click_window;

    int xrender_supported;
    int shape_supported;

    int compositing_active;
    int resize_guard;
    int suppress_configure;
    int force_sdl_decorations;
    int pending_configure;
    XFontStruct *font;
} AquaWm;

void keep_docks_on_top(AquaWm *wm);

char* trim_whitespace(char* str) {
    if (!str) return str;

    char *end;
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = 0;

    return str;
}

unsigned int parse_color(const char *color_str) {
    if (color_str[0] == '#') {
        unsigned int color;
        sscanf(color_str + 1, "%x", &color);
        return color;
    } else if (strstr(color_str, "0x") == color_str) {
        unsigned int color;
        sscanf(color_str + 2, "%x", &color);
        return color;
    }
    return 0xF0F0F0;
}

void save_default_config(void) {
    char config_path[MAX_PATH];
    char *home = getenv("HOME");
    if (!home) return;

    snprintf(config_path, sizeof(config_path), "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);

    char dir_path[MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", home, CONFIG_DIR);
    mkdir(dir_path, 0755);

    FILE *file = fopen(config_path, "w");
    if (!file) return;

    fprintf(file, "# AquaWM Configuration File\n");
    fprintf(file, "# =========================\n\n");
    fprintf(file, "# Appearance Settings\n");
    fprintf(file, "# Border width in pixels (0-5)\n");
    fprintf(file, "BORDER=2\n");
    fprintf(file, "# Frame width in pixels (1-10)\n");
    fprintf(file, "FRAME=3\n");
    fprintf(file, "# Wallpaper path (leave empty for no wallpaper)\n");
    fprintf(file, "WALLPAPER=/usr/share/wallpapers/aquawm/blue.png\n");
    fprintf(file, "# Gradient steps for titlebar (4-64)\n");
    fprintf(file, "GRADIENT_STEPS=12\n");
    fprintf(file, "# Show desktop grid (0=no, 1=yes)\n");
    fprintf(file, "SHOW_GRID=0\n");
    fprintf(file, "# Enable compositing (0=no, 1=yes)\n");
    fprintf(file, "ENABLE_COMPOSITING=0\n");
    fprintf(file, "# Font name (X11 font specification)\n");
    fprintf(file, "FONT_NAME=-*-clean-*-*-*-*-*-*-*-*-*-*-*\n");
    fprintf(file, "# Font size (8-24)\n");
    fprintf(file, "FONT_SIZE=11\n");
    fprintf(file, "# Window manager theme\n");
    fprintf(file, "# Available themes: classic, blue, red, green, purple, orange, osx, aero, cde, black, coolclean\n");
    fprintf(file, "WM_THEME=classic\n");
    fprintf(file, "# Active title color in hex\n");
    fprintf(file, "ACTIVE_TITLE_COLOR=#F0F0F0\n");
    fprintf(file, "# Inactive title color in hex\n");
    fprintf(file, "INACTIVE_TITLE_COLOR=#E0E0E0\n");
    fprintf(file, "# Titlebar gradient (0=flat, 1=gradient)\n");
    fprintf(file, "TITLEBAR_GRADIENT=1\n");
    fprintf(file, "# Enable shadows (0=no, 1=yes)\n");
    fprintf(file, "SHADOW_ENABLED=1\n");
    fprintf(file, "# Shadow size in pixels (0-10)\n");
    fprintf(file, "SHADOW_SIZE=4\n");
    fprintf(file, "# Shadow color in hex (with alpha)\n");
    fprintf(file, "SHADOW_COLOR=#40000000\n\n");
    fprintf(file, "# Behavior Settings\n");
    fprintf(file, "# Focus follows mouse (0=no, 1=yes)\n");
    fprintf(file, "FOCUS_FOLLOWS_MOUSE=1\n");
    fprintf(file, "# Raise window on focus (0=no, 1=yes)\n");
    fprintf(file, "RAISE_ON_FOCUS=1\n");
    fprintf(file, "# Double click time in ms (100-1000)\n");
    fprintf(file, "DOUBLE_CLICK_TIME=250\n");
    fprintf(file, "# Edge resistance when moving windows (0-100)\n");
    fprintf(file, "EDGE_RESISTANCE=20\n");
    fprintf(file, "# Opaque resize (0=wireframe, 1=opaque)\n");
    fprintf(file, "OPAQUE_RESIZE=1\n");
    fprintf(file, "# Center new windows (0=no, 1=yes)\n");
    fprintf(file, "CENTER_NEW_WINDOWS=1\n");
    fprintf(file, "# Window animation (0=no, 1=yes)\n");
    fprintf(file, "WINDOW_ANIMATION=0\n");
    fprintf(file, "# Animation duration in ms (0-1000)\n");
    fprintf(file, "ANIMATION_DURATION=200\n\n");
    fprintf(file, "# Layout Settings\n");
    fprintf(file, "# Screen margin (0-50)\n");
    fprintf(file, "SCREEN_MARGIN=0\n");
    fprintf(file, "# Top margin for workarea (0-50)\n");
    fprintf(file, "SCREEN_TOP_MARGIN=0\n");
    fprintf(file, "# Bottom margin for workarea (0-50)\n");
    fprintf(file, "SCREEN_BOTTOM_MARGIN=0\n");
    fprintf(file, "# Dock padding (0-10)\n");
    fprintf(file, "DOCK_PADDING=2\n");
    fprintf(file, "# Smart gaps (0=no, 1=yes)\n");
    fprintf(file, "SMART_GAPS=1\n");
    fprintf(file, "# Gap size in pixels (0-20)\n");
    fprintf(file, "GAPS=5\n\n");
    fprintf(file, "# Keybindings\n");
    fprintf(file, "# Format: KEYBINDING=Modifier+Key command\n");
    fprintf(file, "# Modifiers: Alt, Super (Windows key)\n");
    fprintf(file, "# Available commands:\n");
    fprintf(file, "#   close_window, cycle_windows, workspace_1-6,\n");
    fprintf(file, "#   toggle_fullscreen, cycle_windows_reverse, minimize_window,\n");
    fprintf(file, "#   unminimize_window, minimize_all_toggle, prog_start=<program>\n");
    fprintf(file, "KEYBINDING=Alt+F4 close_window\n");
    fprintf(file, "KEYBINDING=Alt+Tab cycle_windows\n");
    fprintf(file, "KEYBINDING=Alt+1 workspace_1\n");
    fprintf(file, "KEYBINDING=Alt+2 workspace_2\n");
    fprintf(file, "KEYBINDING=Alt+3 workspace_3\n");
    fprintf(file, "KEYBINDING=Alt+4 workspace_4\n");
    fprintf(file, "KEYBINDING=Alt+5 workspace_5\n");
    fprintf(file, "KEYBINDING=Alt+6 workspace_6\n");
    fprintf(file, "KEYBINDING=F11 toggle_fullscreen\n");
    fprintf(file, "KEYBINDING=Super+Tab cycle_windows_reverse\n");
    fprintf(file, "KEYBINDING=Super+D minimize_window\n");
    fprintf(file, "KEYBINDING=Super+Shift+D unminimize_window\n");
    fprintf(file, "KEYBINDING=Alt+A minimize_all_toggle\n");

    fclose(file);
}

void apply_theme_settings(AquaWm *wm) {
    if (strcmp(wm->config.wm_theme, "white") == 0) {
        wm->config.active_title_color = 0xF0F0F0;
        wm->config.inactive_title_color = 0xE0E0E0;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "blue") == 0) {
        wm->config.active_title_color = 0x4A90E2;
        wm->config.inactive_title_color = 0x7AB5E6;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "red") == 0) {
        wm->config.active_title_color = 0xE24A4A;
        wm->config.inactive_title_color = 0xE67A7A;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "green") == 0) {
        wm->config.active_title_color = 0x4AE24A;
        wm->config.inactive_title_color = 0x7AE67A;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "purple") == 0) {
        wm->config.active_title_color = 0x904AE2;
        wm->config.inactive_title_color = 0xB07AE6;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "orange") == 0) {
        wm->config.active_title_color = 0xE2904A;
        wm->config.inactive_title_color = 0xE6B07A;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "osx") == 0) {
        wm->config.active_title_color = 0xD0D0D0;
        wm->config.inactive_title_color = 0xC0C0C0;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "greenxp") == 0) {
        wm->config.active_title_color = 0x3A923A;
        wm->config.inactive_title_color = 0x8CB48C;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "aero") == 0) {
        wm->config.active_title_color = 0x70B4E6;
        wm->config.inactive_title_color = 0xA0D0F0;
        wm->config.titlebar_gradient = 1;
    } else if (strcmp(wm->config.wm_theme, "cde") == 0) {
        wm->config.active_title_color = 0xFF8C00;
        wm->config.inactive_title_color = 0xAAAAAA;
        wm->config.titlebar_gradient = 0;
    } else if (strcmp(wm->config.wm_theme, "black") == 0) {
        wm->config.active_title_color = 0x000000;
        wm->config.inactive_title_color = 0x333333;
        wm->config.titlebar_gradient = 0;
    } else if (strcmp(wm->config.wm_theme, "coolclean") == 0) {
        wm->config.active_title_color = 0x3A7BC8;
        wm->config.inactive_title_color = 0x7AAAE0;
        wm->config.titlebar_gradient = 1;
    } else {
        wm->config.active_title_color = 0xF0F0F0;
        wm->config.inactive_title_color = 0xE0E0E0;
        wm->config.titlebar_gradient = 1;
    }
}

void load_config(AquaWm *wm) {
    wm->config.border_width = 2;
    wm->config.frame_width = 3;
    strcpy(wm->config.wallpaper, "/usr/share/wallpapers/aquawm/blue.png");
    wm->config.gradient_steps = 12;
    wm->config.show_grid = 0;
    wm->config.enable_compositing = 0;
    strcpy(wm->config.font_name, "-*-clean-*-*-*-*-*-*-*-*-*-*-*");
    wm->config.font_size = 11;
    wm->config.focus_follows_mouse = 1;
    wm->config.raise_on_focus = 1;
    wm->config.double_click_time = 250;
    wm->config.edge_resistance = 20;
    wm->config.opaque_resize = 1;
    wm->config.center_new_windows = 1;
    wm->config.shadow_enabled = 1;
    wm->config.shadow_size = 4;
    wm->config.shadow_color = 0x40000000;
    wm->config.titlebar_gradient = 1;
    wm->config.active_title_color = 0xF0F0F0;
    wm->config.inactive_title_color = 0xE0E0E0;
    wm->config.window_animation = 0;
    wm->config.animation_duration = 200;
    wm->config.keybinding_count = 0;
    wm->config.screen_margin = 0;
    wm->config.screen_top_margin = 0;
    wm->config.screen_bottom_margin = 0;
    wm->config.dock_padding = 2;
    wm->config.smart_gaps = 1;
    wm->config.gaps = 5;
    strcpy(wm->config.wm_theme, "classic");

    for (int i = 0; i < MAX_KEYBINDINGS; i++) {
        memset(&wm->config.keybindings[i], 0, sizeof(KeyBinding));
    }

    char config_path[MAX_PATH];
    char *home = getenv("HOME");
    if (!home) return;

    snprintf(config_path, sizeof(config_path), "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);

    FILE *file = fopen(config_path, "r");
    if (!file) {
        save_default_config();
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *trimmed = trim_whitespace(line);
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        char *key = strtok(trimmed, "=");
        char *value = strtok(NULL, "=");

        if (key && value) {
            char *trimmed_key = trim_whitespace(key);
            char *trimmed_value = trim_whitespace(value);

            if (strcmp(trimmed_key, "BORDER") == 0) {
                wm->config.border_width = atoi(trimmed_value);
                if (wm->config.border_width < 0) wm->config.border_width = 0;
                if (wm->config.border_width > 5) wm->config.border_width = 5;
            } else if (strcmp(trimmed_key, "FRAME") == 0) {
                wm->config.frame_width = atoi(trimmed_value);
                if (wm->config.frame_width < 1) wm->config.frame_width = 1;
                if (wm->config.frame_width > 10) wm->config.frame_width = 10;
            } else if (strcmp(trimmed_key, "WALLPAPER") == 0) {
                strncpy(wm->config.wallpaper, trimmed_value, sizeof(wm->config.wallpaper) - 1);
                wm->config.wallpaper[sizeof(wm->config.wallpaper) - 1] = '\0';
            } else if (strcmp(trimmed_key, "GRADIENT_STEPS") == 0) {
                wm->config.gradient_steps = atoi(trimmed_value);
                if (wm->config.gradient_steps < 4) wm->config.gradient_steps = 4;
                if (wm->config.gradient_steps > 64) wm->config.gradient_steps = 64;
            } else if (strcmp(trimmed_key, "SHOW_GRID") == 0) {
                wm->config.show_grid = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "ENABLE_COMPOSITING") == 0) {
                wm->config.enable_compositing = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "FONT_NAME") == 0) {
                strncpy(wm->config.font_name, trimmed_value, sizeof(wm->config.font_name) - 1);
                wm->config.font_name[sizeof(wm->config.font_name) - 1] = '\0';
            } else if (strcmp(trimmed_key, "FONT_SIZE") == 0) {
                wm->config.font_size = atoi(trimmed_value);
                if (wm->config.font_size < 8) wm->config.font_size = 8;
                if (wm->config.font_size > 24) wm->config.font_size = 24;
            } else if (strcmp(trimmed_key, "FOCUS_FOLLOWS_MOUSE") == 0) {
                wm->config.focus_follows_mouse = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "RAISE_ON_FOCUS") == 0) {
                wm->config.raise_on_focus = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "DOUBLE_CLICK_TIME") == 0) {
                wm->config.double_click_time = atoi(trimmed_value);
                if (wm->config.double_click_time < 100) wm->config.double_click_time = 100;
                if (wm->config.double_click_time > 1000) wm->config.double_click_time = 1000;
            } else if (strcmp(trimmed_key, "EDGE_RESISTANCE") == 0) {
                wm->config.edge_resistance = atoi(trimmed_value);
                if (wm->config.edge_resistance < 0) wm->config.edge_resistance = 0;
                if (wm->config.edge_resistance > 100) wm->config.edge_resistance = 100;
            } else if (strcmp(trimmed_key, "OPAQUE_RESIZE") == 0) {
                wm->config.opaque_resize = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "CENTER_NEW_WINDOWS") == 0) {
                wm->config.center_new_windows = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "SHADOW_ENABLED") == 0) {
                wm->config.shadow_enabled = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "SHADOW_SIZE") == 0) {
                wm->config.shadow_size = atoi(trimmed_value);
                if (wm->config.shadow_size < 0) wm->config.shadow_size = 0;
                if (wm->config.shadow_size > 10) wm->config.shadow_size = 10;
            } else if (strcmp(trimmed_key, "SHADOW_COLOR") == 0) {
                wm->config.shadow_color = parse_color(trimmed_value);
            } else if (strcmp(trimmed_key, "TITLEBAR_GRADIENT") == 0) {
                wm->config.titlebar_gradient = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "ACTIVE_TITLE_COLOR") == 0) {
                wm->config.active_title_color = parse_color(trimmed_value);
            } else if (strcmp(trimmed_key, "INACTIVE_TITLE_COLOR") == 0) {
                wm->config.inactive_title_color = parse_color(trimmed_value);
            } else if (strcmp(trimmed_key, "WINDOW_ANIMATION") == 0) {
                wm->config.window_animation = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "ANIMATION_DURATION") == 0) {
                wm->config.animation_duration = atoi(trimmed_value);
                if (wm->config.animation_duration < 0) wm->config.animation_duration = 0;
                if (wm->config.animation_duration > 1000) wm->config.animation_duration = 1000;
            } else if (strcmp(trimmed_key, "SCREEN_MARGIN") == 0) {
                wm->config.screen_margin = atoi(trimmed_value);
                if (wm->config.screen_margin < 0) wm->config.screen_margin = 0;
                if (wm->config.screen_margin > 50) wm->config.screen_margin = 50;
            } else if (strcmp(trimmed_key, "SCREEN_TOP_MARGIN") == 0) {
                wm->config.screen_top_margin = atoi(trimmed_value);
                if (wm->config.screen_top_margin < 0) wm->config.screen_top_margin = 0;
                if (wm->config.screen_top_margin > 50) wm->config.screen_top_margin = 50;
            } else if (strcmp(trimmed_key, "SCREEN_BOTTOM_MARGIN") == 0) {
                wm->config.screen_bottom_margin = atoi(trimmed_value);
                if (wm->config.screen_bottom_margin < 0) wm->config.screen_bottom_margin = 0;
                if (wm->config.screen_bottom_margin > 50) wm->config.screen_bottom_margin = 50;
            } else if (strcmp(trimmed_key, "DOCK_PADDING") == 0) {
                wm->config.dock_padding = atoi(trimmed_value);
                if (wm->config.dock_padding < 0) wm->config.dock_padding = 0;
                if (wm->config.dock_padding > 10) wm->config.dock_padding = 10;
            } else if (strcmp(trimmed_key, "SMART_GAPS") == 0) {
                wm->config.smart_gaps = atoi(trimmed_value);
            } else if (strcmp(trimmed_key, "GAPS") == 0) {
                wm->config.gaps = atoi(trimmed_value);
                if (wm->config.gaps < 0) wm->config.gaps = 0;
                if (wm->config.gaps > 20) wm->config.gaps = 20;
            } else if (strcmp(trimmed_key, "WM_THEME") == 0) {
                strncpy(wm->config.wm_theme, trimmed_value, sizeof(wm->config.wm_theme) - 1);
                wm->config.wm_theme[sizeof(wm->config.wm_theme) - 1] = '\0';
            } else if (strcmp(trimmed_key, "KEYBINDING") == 0 && wm->config.keybinding_count < MAX_KEYBINDINGS) {
                char *keysym_str = strtok(trimmed_value, " ");
                char *command_str = strtok(NULL, "");

                if (keysym_str && command_str) {
                    strncpy(wm->config.keybindings[wm->config.keybinding_count].keysym,
                           trim_whitespace(keysym_str),
                           sizeof(wm->config.keybindings[wm->config.keybinding_count].keysym) - 1);

                    strncpy(wm->config.keybindings[wm->config.keybinding_count].modifiers,
                           "",
                           sizeof(wm->config.keybindings[wm->config.keybinding_count].modifiers) - 1);

                    strncpy(wm->config.keybindings[wm->config.keybinding_count].command,
                           trim_whitespace(command_str),
                           sizeof(wm->config.keybindings[wm->config.keybinding_count].command) - 1);

                    wm->config.keybinding_count++;
                }
            }
        }
    }

    fclose(file);
    apply_theme_settings(wm);
}

void load_wallpaper(AquaWm *wm) {
    if (wm->config.wallpaper[0] != '\0') {
        if (access(wm->config.wallpaper, F_OK) != 0) {
            return;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            execlp("feh", "feh", "--bg-fill", wm->config.wallpaper, NULL);
            exit(0);
        } else if (pid > 0) {
            waitpid(pid, NULL, 0);
        }
    }
}

void draw_workspace_indicator(AquaWm *wm) {
    char text[32];
    snprintf(text, sizeof(text), "Workspace %d", wm->current_workspace + 1);

    Pixmap temp_pixmap = XCreatePixmap(wm->display, wm->root,
                                      wm->screen_width, wm->screen_height, wm->depth);

    XSetForeground(wm->display, wm->desktop_gc, 0x2A4DA9);
    XFillRectangle(wm->display, temp_pixmap, wm->desktop_gc,
                  wm->screen_width - 200, wm->screen_height - 30,
                  180, 25);

    XSetForeground(wm->display, wm->desktop_gc, 0xFFFFFF);
    XDrawString(wm->display, temp_pixmap, wm->desktop_gc,
               wm->screen_width - 190, wm->screen_height - 15,
               text, strlen(text));

    XCopyArea(wm->display, temp_pixmap, wm->root, wm->desktop_gc,
             wm->screen_width - 200, wm->screen_height - 30,
             180, 25,
             wm->screen_width - 200, wm->screen_height - 30);

    XFreePixmap(wm->display, temp_pixmap);
}

void setup_wm_properties(AquaWm *wm) {
    wm->wm_window = XCreateSimpleWindow(wm->display, wm->root, 0, 0, 1, 1, 0, 0, 0);

    XChangeProperty(wm->display, wm->root, wm->net_supporting_wm_check,
                   XA_WINDOW, 32, PropModeReplace, (unsigned char*)&wm->wm_window, 1);

    XChangeProperty(wm->display, wm->wm_window, wm->net_supporting_wm_check,
                   XA_WINDOW, 32, PropModeReplace, (unsigned char*)&wm->wm_window, 1);

    XChangeProperty(wm->display, wm->wm_window, wm->net_wm_name,
                   XA_STRING, 8, PropModeReplace, (unsigned char*)"AquaWM", 6);

    Atom supported[] = {
        wm->wm_protocols,
        wm->wm_delete_window,
        wm->wm_take_focus,
        wm->net_wm_state,
        wm->net_wm_state_maximized_vert,
        wm->net_wm_state_maximized_horz,
        wm->net_wm_state_fullscreen,
        wm->net_active_window,
        wm->wm_transient_for,
        wm->utf8_string,
        wm->net_wm_window_type,
        wm->net_wm_window_type_dock,
        wm->net_wm_window_type_desktop,
        wm->net_wm_strut,
        wm->net_wm_strut_partial,
        wm->net_wm_state_above,
        wm->net_wm_state_stays_on_top,
        wm->net_wm_state_below,
        wm->net_wm_state_modal,
        wm->net_wm_window_type_dialog,
        wm->net_wm_window_type_normal,
        wm->net_wm_desktop,
        wm->net_wm_state_hidden,
        wm->net_wm_state_shaded,
        wm->net_wm_state_skip_taskbar,
        wm->net_wm_state_skip_pager,
        wm->net_wm_state_demands_attention,
        wm->net_wm_name,
        wm->net_supporting_wm_check,
        wm->net_supported,
        wm->net_number_of_desktops,
        wm->net_current_desktop,
        wm->net_client_list,
        wm->net_client_list_stacking,
        wm->net_wm_window_opacity,
        wm->net_wm_user_time,
        wm->motif_wm_hints
    };

    XChangeProperty(wm->display, wm->root, wm->net_supported,
                   XA_ATOM, 32, PropModeReplace, (unsigned char*)supported,
                   sizeof(supported) / sizeof(Atom));

    long num_desktops = MAX_WORKSPACES;
    XChangeProperty(wm->display, wm->root, wm->net_number_of_desktops,
                   XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&num_desktops, 1);

    long current_desktop = wm->current_workspace;
    XChangeProperty(wm->display, wm->root, wm->net_current_desktop,
                   XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&current_desktop, 1);
}

void update_client_list(AquaWm *wm) {
    Window *clients = malloc(sizeof(Window) * wm->num_clients);
    int count = 0;

    for (int i = 0; i < wm->num_clients; i++) {
        if (!wm->clients[i]->is_dock && wm->clients[i]->mapped &&
            wm->clients[i]->workspace == wm->current_workspace && !wm->clients[i]->minimized) {
            clients[count++] = wm->clients[i]->window;
        }
    }

    if (count > 0) {
        XChangeProperty(wm->display, wm->root, wm->net_client_list,
                       XA_WINDOW, 32, PropModeReplace, (unsigned char*)clients, count);
    } else {
        XDeleteProperty(wm->display, wm->root, wm->net_client_list);
    }

    free(clients);
}

Client* find_client_by_window(AquaWm *wm, Window win) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->window == win) {
            return wm->clients[i];
        }
    }
    return NULL;
}

Client* find_client_by_frame(AquaWm *wm, Window frame) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->frame == frame && wm->clients[i]->decorated) {
            return wm->clients[i];
        }
    }
    return NULL;
}

int is_sdl_app(AquaWm *wm, Window window) {
    XClassHint class_hint;
    int result = 0;

    if (XGetClassHint(wm->display, window, &class_hint)) {
        if (class_hint.res_name && (strstr(class_hint.res_name, "sdl") ||
                                    strstr(class_hint.res_name, "SDL") ||
                                    strstr(class_hint.res_name, "dosbox") ||
                                    strstr(class_hint.res_name, "betacraft") ||
                                    strstr(class_hint.res_name, "SDL3"))) {
            result = 1;
        }

        if (class_hint.res_class && (strstr(class_hint.res_class, "sdl") ||
                                     strstr(class_hint.res_class, "SDL") ||
                                     strstr(class_hint.res_name, "dosbox") ||
                                     strstr(class_hint.res_class, "betacraft") ||
                                     strstr(class_hint.res_class, "SDL3"))) {
            result = 1;
        }

        if (class_hint.res_name) XFree(class_hint.res_name);
        if (class_hint.res_class) XFree(class_hint.res_class);
    }

    return result;
}

int is_override_redirect_window(AquaWm *wm, Window window) {
    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->display, window, &attr)) return 0;
    return attr.override_redirect;
}

int is_special_window_type(AquaWm *wm, Window window) {
    Atom net_wm_window_type_splash = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    Atom net_wm_window_type_tooltip = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    Atom net_wm_window_type_notification = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    Atom net_wm_window_type_menu = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_MENU", False);

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int result = 0;

    if (XGetWindowProperty(wm->display, window, wm->net_wm_window_type,
                          0, 10, False, XA_ATOM, &type, &format,
                          &nitems, &bytes_after, &data) == Success && data) {
        Atom *atoms = (Atom*)data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == wm->net_wm_window_type_dock ||
                atoms[i] == wm->net_wm_window_type_desktop ||
                atoms[i] == net_wm_window_type_splash ||
                atoms[i] == net_wm_window_type_tooltip ||
                atoms[i] == net_wm_window_type_notification ||
                atoms[i] == net_wm_window_type_menu) {
                result = 1;
                break;
            }
        }
        XFree(data);
    }

    return result;
}

void lock_client_to_frame(AquaWm *wm, Client *client) {
    if (!client || !client->decorated || client->is_dock || client->window == None || client->minimized) return;

    int target_x = wm->config.frame_width;
    int target_y = TITLEBAR_HEIGHT;

    if (client->has_gtk_extents) {
        target_x += client->gtk_left;
        target_y += client->gtk_top;
    }

    XWindowAttributes attr;
    if (XGetWindowAttributes(wm->display, client->window, &attr)) {
        if (abs(attr.x - target_x) > 2 || abs(attr.y - target_y) > 2) {
            XMoveWindow(wm->display, client->window, target_x, target_y);
        }
    }
}

void enforce_frame_integrity(AquaWm *wm, Client *client) {
    if (!client || !client->decorated || client->is_dock || client->is_fullscreen || client->minimized) return;

    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->display, client->window, &attr)) return;

    if (attr.map_state == IsViewable && client->mapped) {
        int expected_x = wm->config.frame_width;
        int expected_y = TITLEBAR_HEIGHT;

        if (client->has_gtk_extents) {
            expected_x += client->gtk_left;
            expected_y += client->gtk_top;
        }

        if (abs(attr.x - expected_x) > 2 || abs(attr.y - expected_y) > 2) {
            XMoveWindow(wm->display, client->window, expected_x, expected_y);
        }

        int expected_width = client->width;
        int expected_height = client->height;

        if (client->has_gtk_extents) {
            expected_width -= (client->gtk_left + client->gtk_right);
            expected_height -= (client->gtk_top + client->gtk_bottom);

            if (expected_width < 1) expected_width = 1;
            if (expected_height < 1) expected_height = 1;
        }

        if (abs(attr.width - expected_width) > 2 || abs(attr.height - expected_height) > 2) {
            XResizeWindow(wm->display, client->window, expected_width, expected_height);
        }
    }
}

void ensure_window_in_bounds(AquaWm *wm, Client *client) {
    if (!client || client->is_dock || client->is_fullscreen) return;

    int max_x = wm->screen_width - client->width;
    int max_y = wm->screen_height - client->height;

    if (client->decorated) {
        max_x -= 2 * wm->config.frame_width;
        max_y -= TITLEBAR_HEIGHT + wm->config.frame_width;
    }

    if (client->x < 0) client->x = 0;
    if (client->y < 0) client->y = 0;
    if (client->x > max_x) client->x = max_x;
    if (client->y > max_y) client->y = max_y;

    if (client->decorated) {
        client->frame_x = client->x - wm->config.frame_width;
        client->frame_y = client->y - TITLEBAR_HEIGHT;
    } else {
        client->frame_x = client->x;
        client->frame_y = client->y;
    }
}

void apply_gaps(AquaWm *wm, Client *client) {
    if (!client || client->is_dock || client->is_fullscreen || client->minimized) return;

    if (wm->config.smart_gaps) {
        client->x += wm->config.gaps;
        client->y += wm->config.gaps;
        client->width -= 2 * wm->config.gaps;
        client->height -= 2 * wm->config.gaps;

        if (client->width < MIN_WIDTH) {
            client->width = MIN_WIDTH;
            client->x = (wm->screen_width - MIN_WIDTH) / 2;
        }
        if (client->height < MIN_HEIGHT) {
            client->height = MIN_HEIGHT;
            client->y = (wm->screen_height - MIN_HEIGHT) / 2;
        }
    }
}

void center_window(AquaWm *wm, Client *client) {
    if (!client || !wm || client->is_dock) return;

    int frame_width = client->width + 2 * wm->config.frame_width;
    int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

    client->frame_x = (wm->screen_width - frame_width) / 2;
    client->frame_y = (wm->screen_height - frame_height) / 2;

    if (client->frame_x < 0) client->frame_x = 0;
    if (client->frame_y < 0) client->frame_y = 0;
    if (client->frame_x + frame_width > wm->screen_width)
        client->frame_x = wm->screen_width - frame_width;
    if (client->frame_y + frame_height > wm->screen_height)
        client->frame_y = wm->screen_height - frame_height;

    client->x = client->frame_x + wm->config.frame_width;
    client->y = client->frame_y + TITLEBAR_HEIGHT;

    client->orig_x = client->x;
    client->orig_y = client->y;
    client->orig_width = client->width;
    client->orig_height = client->height;
}

void initialize_window_position(AquaWm *wm, Client *client) {
    if (!client || client->is_dock) return;

    if (wm->config.center_new_windows || client->force_center) {
        center_window(wm, client);
        client->force_center = 0;
    } else {
        if (client->x < 0 || client->y < 0 ||
            client->x + client->width > wm->screen_width ||
            client->y + client->height > wm->screen_height) {

            center_window(wm, client);
        } else {
            ensure_window_in_bounds(wm, client);
        }
    }

    apply_gaps(wm, client);
}

void position_window_correctly(AquaWm *wm, Client *client) {
    if (!client || client->is_dock) return;

    initialize_window_position(wm, client);
}

void reparent_window(AquaWm *wm, Client *client) {
    if (!client || client->is_dock) return;

    if (client->decorated) {
        int frame_width = client->width + 2 * wm->config.frame_width;
        int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

        client->frame = XCreateSimpleWindow(wm->display, wm->root,
                                           client->frame_x,
                                           client->frame_y,
                                           frame_width,
                                           frame_height,
                                           0,
                                           0x444444,
                                           0xFFFFFF);

        XSelectInput(wm->display, client->frame,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     ButtonMotionMask | EnterWindowMask | PointerMotionMask |
                     LeaveWindowMask | FocusChangeMask | StructureNotifyMask);

        Atom protocols[] = {wm->wm_delete_window, wm->wm_take_focus};
        XSetWMProtocols(wm->display, client->frame, protocols, 2);

        XSetWindowBorderWidth(wm->display, client->window, 0);

        XReparentWindow(wm->display, client->window, client->frame,
                       wm->config.frame_width, TITLEBAR_HEIGHT);

        if (client->has_gtk_extents) {
            XMoveWindow(wm->display, client->window,
                       wm->config.frame_width + client->gtk_left,
                       TITLEBAR_HEIGHT + client->gtk_top);
            XResizeWindow(wm->display, client->window,
                         client->width - client->gtk_left - client->gtk_right,
                         client->height - client->gtk_top - client->gtk_bottom);
        } else {
            XMoveWindow(wm->display, client->window,
                       wm->config.frame_width, TITLEBAR_HEIGHT);
        }

        XSelectInput(wm->display, client->window,
                     StructureNotifyMask | PropertyChangeMask | SubstructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask);
    } else {
        client->frame = client->window;
        XSelectInput(wm->display, client->window,
                     StructureNotifyMask | PropertyChangeMask | SubstructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask);
    }
}

void force_client_position(AquaWm *wm, Client *client) {
    if (!client || client->is_dock || client->window == None || client->minimized) return;

    if (client->decorated && !client->is_fullscreen) {
        int target_x = wm->config.frame_width;
        int target_y = TITLEBAR_HEIGHT;
        int target_width = client->width;
        int target_height = client->height;

        if (client->has_gtk_extents) {
            target_x += client->gtk_left;
            target_y += client->gtk_top;
            target_width -= (client->gtk_left + client->gtk_right);
            target_height -= (client->gtk_top + client->gtk_bottom);

            if (target_width < 1) target_width = 1;
            if (target_height < 1) target_height = 1;
        }

        XMoveResizeWindow(wm->display, client->window,
                          target_x, target_y,
                          target_width, target_height);
    } else {
        XMoveResizeWindow(wm->display, client->window,
                          client->x, client->y,
                          client->width, client->height);
    }

    XFlush(wm->display);
}

void update_gtk_extents(AquaWm *wm, Client *client) {
    if (!client || client->window == None) return;

    client->has_gtk_extents = 0;
    Atom gtk_frame_extents = XInternAtom(wm->display, "_GTK_FRAME_EXTENTS", False);
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(wm->display, client->window, gtk_frame_extents,
                          0, 4, False, XA_CARDINAL, &type, &format,
                          &nitems, &bytes_after, &data) == Success && data && nitems == 4) {
        unsigned long *extents = (unsigned long*)data;
        client->gtk_left = extents[0];
        client->gtk_right = extents[1];
        client->gtk_top = extents[2];
        client->gtk_bottom = extents[3];
        client->has_gtk_extents = 1;
        XFree(data);
    }
}

int get_motif_decorations(AquaWm *wm, Window window) {
    if (wm->motif_wm_hints == None) return 1;

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int decorations = 1;

    if (XGetWindowProperty(wm->display, window, wm->motif_wm_hints,
                          0, 5, False, wm->motif_wm_hints, &type, &format,
                          &nitems, &bytes_after, &data) == Success && data && nitems >= 5) {
        unsigned long *hints = (unsigned long*)data;
        if (hints[0] & 2) {
            decorations = (hints[2] & 2) ? 1 : 0;
        }
        XFree(data);
    }

    return decorations;
}

void remove_client(AquaWm *wm, Client *client) {
    if (!client) return;

    int index = -1;
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i] == client) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        if (wm->moving_client == client || wm->grabbed_client == client) {
            wm->is_moving = 0;
            wm->is_resizing = 0;
            wm->moving_client = NULL;
            wm->grabbed_client = NULL;
            wm->mouse_grabbed = 0;
            wm->grab_window = None;
            XUngrabPointer(wm->display, CurrentTime);
        }

        if (client->title) free(client->title);

        if (client->decorated && client->frame != None && client->frame != client->window) {
            XDestroyWindow(wm->display, client->frame);
        }

        for (int i = index; i < wm->num_clients - 1; i++) {
            wm->clients[i] = wm->clients[i + 1];
        }

        wm->num_clients--;

        if (wm->active_client == client) {
            wm->active_client = NULL;
            for (int i = 0; i < wm->num_clients; i++) {
                if (wm->clients[i]->decorated && !wm->clients[i]->is_dock &&
                    wm->clients[i]->workspace == wm->current_workspace &&
                    wm->clients[i]->mapped && !wm->clients[i]->minimized) {
                    wm->active_client = wm->clients[i];
                    wm->active_client->is_active = 1;
                    XSetInputFocus(wm->display, wm->active_client->window,
                                 RevertToParent, CurrentTime);
                    if (!wm->active_client->is_fullscreen && !wm->active_client->minimized) {
                        XClearWindow(wm->display, wm->active_client->frame);
                        XFlush(wm->display);
                    }
                    break;
                }
            }
        }

        free(client);
    }

    update_client_list(wm);
    keep_docks_on_top(wm);
}

void set_input_focus(AquaWm *wm, Client *client) {
    if (!client || client->window == None || client->minimized) return;

    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->display, client->window, &attr)) return;

    if (attr.map_state == IsViewable) {
        XSetInputFocus(wm->display, client->window, RevertToParent, CurrentTime);

        if (wm->config.raise_on_focus && client->decorated && !client->is_fullscreen) {
            XRaiseWindow(wm->display, client->frame);
        }

        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = client->window;
        ev.xclient.message_type = wm->net_active_window;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = 1;
        ev.xclient.data.l[1] = CurrentTime;
        ev.xclient.data.l[2] = wm->active_client ? wm->active_client->window : None;

        XSendEvent(wm->display, wm->root, False,
                  SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    }
}

void grab_server(AquaWm *wm) {
    XGrabServer(wm->display);
}

void ungrab_server(AquaWm *wm) {
    XUngrabServer(wm->display);
    XFlush(wm->display);
}

void send_configure_notify(AquaWm *wm, Client *client) {
    if (wm->suppress_configure || client->minimized) return;

    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = ConfigureNotify;
    ev.xconfigure.serial = 0;
    ev.xconfigure.send_event = True;
    ev.xconfigure.display = wm->display;
    ev.xconfigure.event = client->window;
    ev.xconfigure.window = client->window;

    if (client->decorated) {
        ev.xconfigure.x = client->x;
        ev.xconfigure.y = client->y;
        ev.xconfigure.width = client->width;
        ev.xconfigure.height = client->height;
    } else {
        ev.xconfigure.x = client->frame_x;
        ev.xconfigure.y = client->frame_y;
        ev.xconfigure.width = client->width;
        ev.xconfigure.height = client->height;
    }

    ev.xconfigure.border_width = 0;
    ev.xconfigure.above = None;
    ev.xconfigure.override_redirect = False;

    XSendEvent(wm->display, client->window, False, StructureNotifyMask, &ev);
}

void draw_shadow(AquaWm *wm, Window win, int x, int y, int width, int height) {
    if (!wm->config.shadow_enabled) return;

    Display *dpy = wm->display;
    GC gc = wm->shadow_gc;

    for (int i = 0; i < wm->config.shadow_size; i++) {
        int alpha = 40 - i * (40 / wm->config.shadow_size);
        if (alpha < 10) alpha = 10;

        XSetForeground(dpy, gc, (alpha << 24) | (wm->config.shadow_color & 0x00FFFFFF));

        XDrawRectangle(dpy, win, gc,
                       x - i - 1, y - i - 1,
                       width + 2*i + 2, height + 2*i + 2);
    }
}

void draw_titlebar_gradient(AquaWm *wm, Window win, int x, int y, int width, int height, int is_active) {
    Display *dpy = wm->display;
    GC gc = wm->titlebar_gc;

    if (!wm->config.titlebar_gradient) {
        unsigned int color = is_active ? wm->config.active_title_color : wm->config.inactive_title_color;
        XSetForeground(dpy, gc, color);
        XFillRectangle(dpy, win, gc, x, y, width, height);
        return;
    }

    unsigned int top_color, bottom_color;

    if (is_active) {
        top_color = wm->config.active_title_color;
        bottom_color = (top_color & 0xFEFEFE) >> 1;
    } else {
        top_color = wm->config.inactive_title_color;
        bottom_color = (top_color & 0xFEFEFE) >> 1;
    }

    for (int i = 0; i < height; i++) {
        float t = (float)i / (height - 1);
        int r = ((top_color >> 16) & 0xFF) * (1 - t) + ((bottom_color >> 16) & 0xFF) * t;
        int g = ((top_color >> 8) & 0xFF) * (1 - t) + ((bottom_color >> 8) & 0xFF) * t;
        int b = (top_color & 0xFF) * (1 - t) + (bottom_color & 0xFF) * t;
        int color = (r << 16) | (g << 8) | b;

        XSetForeground(dpy, gc, color);
        XDrawLine(dpy, win, gc, x, y + i, x + width - 1, y + i);
    }

    XSetForeground(dpy, gc, is_active ? 0x1A3D88 : 0x666666);
    XDrawRectangle(dpy, win, gc, x, y, width, height);
}

void draw_button_osx(AquaWm *wm, Window win, int x, int y, int is_hover, int is_pressed, int type, int is_active) {
    (void)is_hover;
    (void)is_pressed;
    (void)is_active;
    Display *dpy = wm->display;

    int center_x = x + BUTTON_SIZE / 2;
    int center_y = y + BUTTON_SIZE / 2;

    if (type == 0) {
        XSetForeground(dpy, wm->button_bg_gc, 0xFF5C5C);
        XFillArc(dpy, win, wm->button_bg_gc, x, y, BUTTON_SIZE - 1, BUTTON_SIZE - 1, 0, 360*64);
        XSetForeground(dpy, wm->button_fg_gc, 0x000000);
        XSetLineAttributes(dpy, wm->button_fg_gc, 2, LineSolid, CapRound, JoinRound);
        XDrawLine(dpy, win, wm->button_fg_gc, center_x - 3, center_y - 3, center_x + 3, center_y + 3);
        XDrawLine(dpy, win, wm->button_fg_gc, center_x + 3, center_y - 3, center_x - 3, center_y + 3);
        XSetLineAttributes(dpy, wm->button_fg_gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 1) {
        XSetForeground(dpy, wm->button_bg_gc, 0xFFBD5C);
        XFillArc(dpy, win, wm->button_bg_gc, x, y, BUTTON_SIZE - 1, BUTTON_SIZE - 1, 0, 360*64);
        XSetForeground(dpy, wm->button_fg_gc, 0x000000);
        XSetLineAttributes(dpy, wm->button_fg_gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, win, wm->button_fg_gc, x + 4, y + 4, BUTTON_SIZE - 9, BUTTON_SIZE - 9);
        XSetLineAttributes(dpy, wm->button_fg_gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 2) {
        XSetForeground(dpy, wm->button_bg_gc, 0x5CFF5C);
        XFillArc(dpy, win, wm->button_bg_gc, x, y, BUTTON_SIZE - 1, BUTTON_SIZE - 1, 0, 360*64);
        XSetForeground(dpy, wm->button_fg_gc, 0x000000);
        XSetLineAttributes(dpy, wm->button_fg_gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawLine(dpy, win, wm->button_fg_gc, x + 4, center_y + 2, x + BUTTON_SIZE - 5, center_y + 2);
        XSetLineAttributes(dpy, wm->button_fg_gc, 1, LineSolid, CapButt, JoinMiter);
    }
}

void draw_button_aero(AquaWm *wm, Window win, int x, int y, int is_hover, int is_pressed, int type, int is_active) {
    (void)is_active;
    Display *dpy = wm->display;
    GC gc = wm->button_gc;

    int width = 18;
    int height = 16;

    int bg_color_top, bg_color_bottom, border_color, symbol_color;

    if (type == 0) {
        bg_color_top = 0xFF8080;
        bg_color_bottom = 0xFF4040;
        border_color = 0xCC0000;
        symbol_color = 0x000000;
    } else if (type == 1) {
        bg_color_top = 0xFFFFFF;
        bg_color_bottom = 0xF0F0F0;
        border_color = 0xCCCCCC;
        symbol_color = 0x000000;
    } else if (type == 2) {
        bg_color_top = 0xFFFFFF;
        bg_color_bottom = 0xF0F0F0;
        border_color = 0xCCCCCC;
        symbol_color = 0x000000;
    } else {
        bg_color_top = 0xFFFFFF;
        bg_color_bottom = 0xF0F0F0;
        border_color = 0xCCCCCC;
        symbol_color = 0x000000;
    }

    if (is_pressed) {
        bg_color_top = (bg_color_top & 0xFEFEFE) >> 1;
        bg_color_bottom = (bg_color_bottom & 0xFEFEFE) >> 1;
        border_color = (border_color & 0xFEFEFE) >> 1;
    } else if (is_hover) {
        if (type == 0) {
            bg_color_top = 0xFFA0A0;
            bg_color_bottom = 0xFF6060;
        } else {
            bg_color_top = 0xF8F8F8;
            bg_color_bottom = 0xE8E8E8;
        }
    }

    for (int i = 0; i < height; i++) {
        float t = (float)i / (height - 1);
        int r = ((bg_color_top >> 16) & 0xFF) * (1 - t) + ((bg_color_bottom >> 16) & 0xFF) * t;
        int g = ((bg_color_top >> 8) & 0xFF) * (1 - t) + ((bg_color_bottom >> 8) & 0xFF) * t;
        int b = (bg_color_top & 0xFF) * (1 - t) + (bg_color_bottom & 0xFF) * t;
        int color = (r << 16) | (g << 8) | b;

        XSetForeground(dpy, gc, color);
        XDrawLine(dpy, win, gc, x, y + i, x + width - 1, y + i);
    }

    XSetForeground(dpy, gc, border_color);
    XDrawRectangle(dpy, win, gc, x, y, width - 1, height - 1);

    XSetForeground(dpy, gc, symbol_color);

    if (type == 0) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
        XDrawLine(dpy, win, gc, x + 4, y + 4, x + width - 4, y + height - 4);
        XDrawLine(dpy, win, gc, x + width - 4, y + 4, x + 4, y + height - 4);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 1) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, win, gc, x + 3, y + 3, width - 6, height - 6);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 2) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawLine(dpy, win, gc, x + 4, y + height - 6, x + width - 4, y + height - 6);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    }
}

void draw_button_classic(AquaWm *wm, Window win, int x, int y, int is_hover, int is_pressed, int type, int is_active) {
    (void)is_hover;
    (void)is_pressed;
    (void)is_active;
    Display *dpy = wm->display;
    GC gc = wm->button_fg_gc;

    XSetForeground(dpy, gc, 0x000000);

    if (type == 0) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
        XDrawLine(dpy, win, gc, x + 4, y + 4, x + BUTTON_SIZE - 5, y + BUTTON_SIZE - 5);
        XDrawLine(dpy, win, gc, x + BUTTON_SIZE - 5, y + 4, x + 4, y + BUTTON_SIZE - 5);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 1) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, win, gc, x + 3, y + 3, BUTTON_SIZE - 7, BUTTON_SIZE - 7);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 2) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawLine(dpy, win, gc, x + 4, y + BUTTON_SIZE - 6, x + BUTTON_SIZE - 5, y + BUTTON_SIZE - 6);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    }
}

void draw_button_coolclean(AquaWm *wm, Window win, int x, int y, int is_hover, int is_pressed, int type, int is_active) {
    (void)is_hover;
    Display *dpy = wm->display;
    GC gc = wm->button_gc;

    int bg_color_top, bg_color_bottom, border_color, symbol_color;

    if (is_active) {
        if (type == 0) {
            bg_color_top = 0xFF6B6B;
            bg_color_bottom = 0xFF3B3B;
            border_color = 0xCC2A2A;
        } else {
            bg_color_top = 0x6BB5FF;
            bg_color_bottom = 0x3B95FF;
            border_color = 0x2A75CC;
        }
    } else {
        if (type == 0) {
            bg_color_top = 0xFFA0A0;
            bg_color_bottom = 0xFF8080;
            border_color = 0xCC6060;
        } else {
            bg_color_top = 0xA0D0FF;
            bg_color_bottom = 0x80B0FF;
            border_color = 0x6090CC;
        }
    }

    if (is_pressed) {
        bg_color_top = (bg_color_top & 0xFEFEFE) >> 1;
        bg_color_bottom = (bg_color_bottom & 0xFEFEFE) >> 1;
        border_color = (border_color & 0xFEFEFE) >> 1;
    }

    symbol_color = 0x000000;

    for (int i = 0; i < BUTTON_SIZE; i++) {
        float t = (float)i / (BUTTON_SIZE - 1);
        int r = ((bg_color_top >> 16) & 0xFF) * (1 - t) + ((bg_color_bottom >> 16) & 0xFF) * t;
        int g = ((bg_color_top >> 8) & 0xFF) * (1 - t) + ((bg_color_bottom >> 8) & 0xFF) * t;
        int b = (bg_color_top & 0xFF) * (1 - t) + (bg_color_bottom & 0xFF) * t;
        int color = (r << 16) | (g << 8) | b;

        XSetForeground(dpy, gc, color);
        XDrawLine(dpy, win, gc, x, y + i, x + BUTTON_SIZE - 1, y + i);
    }

    XSetForeground(dpy, gc, border_color);
    XDrawRectangle(dpy, win, gc, x, y, BUTTON_SIZE - 1, BUTTON_SIZE - 1);

    XSetForeground(dpy, gc, symbol_color);

    if (type == 0) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
        XDrawLine(dpy, win, gc, x + 4, y + 4, x + BUTTON_SIZE - 4, y + BUTTON_SIZE - 4);
        XDrawLine(dpy, win, gc, x + BUTTON_SIZE - 4, y + 4, x + 4, y + BUTTON_SIZE - 4);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 1) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, win, gc, x + 3, y + 3, BUTTON_SIZE - 6, BUTTON_SIZE - 6);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (type == 2) {
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawLine(dpy, win, gc, x + 4, y + BUTTON_SIZE - 6, x + BUTTON_SIZE - 4, y + BUTTON_SIZE - 6);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    }
}

void draw_button(AquaWm *wm, Window win, int x, int y, int is_hover, int is_pressed, int type, int is_active) {
    if (strcmp(wm->config.wm_theme, "osx") == 0) {
        draw_button_osx(wm, win, x, y, is_hover, is_pressed, type, is_active);
    } else if (strcmp(wm->config.wm_theme, "aero") == 0) {
        draw_button_aero(wm, win, x, y, is_hover, is_pressed, type, is_active);
    } else if (strcmp(wm->config.wm_theme, "coolclean") == 0) {
        draw_button_coolclean(wm, win, x, y, is_hover, is_pressed, type, is_active);
    } else {
        draw_button_classic(wm, win, x, y, is_hover, is_pressed, type, is_active);
    }
}

void draw_frame_classic(AquaWm *wm, Client *client) {
    if (!client->decorated || client->is_fullscreen || client->is_dock || client->minimized) return;

    Display *dpy = wm->display;
    Window win = client->frame;
    int frame_width = client->width + 2 * wm->config.frame_width;
    int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

    XClearWindow(dpy, win);

    if (wm->config.shadow_enabled) {
        draw_shadow(wm, win, 2, 2, frame_width - 4, frame_height - 4);
    }

    XSetForeground(dpy, wm->frame_gc, client->is_active ? wm->config.active_title_color : wm->config.inactive_title_color);
    XFillRectangle(dpy, win, wm->frame_gc, 0, 0, frame_width, frame_height);

    XSetForeground(dpy, wm->border_gc, client->is_active ? 0xC0C0C0 : 0xE0E0E0);
    XDrawRectangle(dpy, win, wm->border_gc, 0, 0, frame_width - 1, frame_height - 1);

    draw_titlebar_gradient(wm, win,
                          wm->config.frame_width,
                          wm->config.frame_width,
                          frame_width - 2 * wm->config.frame_width,
                          TITLEBAR_HEIGHT - wm->config.frame_width,
                          client->is_active);

    if (client->title) {
        XSetForeground(dpy, wm->text_gc, 0x000000);

        int text_width = XTextWidth(wm->font, client->title, strlen(client->title));
        int text_x = (frame_width - text_width) / 2;

        XDrawString(dpy, win, wm->text_gc,
                   text_x,
                   wm->config.frame_width + 16,
                   client->title, strlen(client->title));
    }

    int close_x = wm->config.frame_width + BUTTON_SPACING;
    int maximize_x = frame_width - wm->config.frame_width - BUTTON_SPACING - BUTTON_SIZE;
    int minimize_x = frame_width - wm->config.frame_width - BUTTON_SPACING - 2 * BUTTON_SIZE - BUTTON_SPACING;
    int button_y = wm->config.frame_width + (TITLEBAR_HEIGHT - BUTTON_SIZE) / 2;

    draw_button(wm, win, close_x, button_y, 0, 0, 0, client->is_active);
    draw_button(wm, win, maximize_x, button_y, 0, 0, 1, client->is_active);
    draw_button(wm, win, minimize_x, button_y, 0, 0, 2, client->is_active);

    XSetForeground(dpy, wm->highlight_gc, client->is_active ? 0xFFFFFF : 0xF0F0F0);
    XDrawLine(dpy, win, wm->highlight_gc,
              wm->config.frame_width,
              wm->config.frame_width + TITLEBAR_HEIGHT - 1,
              frame_width - wm->config.frame_width - 1,
              wm->config.frame_width + TITLEBAR_HEIGHT - 1);
}

void draw_frame_aero(AquaWm *wm, Client *client) {
    if (!client->decorated || client->is_fullscreen || client->is_dock || client->minimized) return;

    Display *dpy = wm->display;
    Window win = client->frame;
    int frame_width = client->width + 2 * wm->config.frame_width;
    int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

    XClearWindow(dpy, win);

    if (wm->config.shadow_enabled) {
        draw_shadow(wm, win, 2, 2, frame_width - 4, frame_height - 4);
    }

    XSetForeground(dpy, wm->frame_gc, 0x70B4E6);
    XFillRectangle(dpy, win, wm->frame_gc, 0, 0, frame_width, frame_height);

    XSetForeground(dpy, wm->border_gc, client->is_active ? 0x4080C0 : 0x80A0C0);
    XDrawRectangle(dpy, win, wm->border_gc, 0, 0, frame_width - 1, frame_height - 1);

    for (int i = wm->config.frame_width; i < wm->config.frame_width + TITLEBAR_HEIGHT - wm->config.frame_width; i++) {
        float t = (float)(i - wm->config.frame_width) / (TITLEBAR_HEIGHT - wm->config.frame_width - 1);
        int r = ((0x90C8F0 >> 16) & 0xFF) * (1 - t) + ((0xC0E0FF >> 16) & 0xFF) * t;
        int g = ((0x90C8F0 >> 8) & 0xFF) * (1 - t) + ((0xC0E0FF >> 8) & 0xFF) * t;
        int b = (0x90C8F0 & 0xFF) * (1 - t) + (0xC0E0FF & 0xFF) * t;
        int color = (r << 16) | (g << 8) | b;

        XSetForeground(dpy, wm->titlebar_gc, color);
        XDrawLine(dpy, win, wm->titlebar_gc,
                  wm->config.frame_width, i,
                  frame_width - wm->config.frame_width - 1, i);
    }

    if (client->title) {
        XSetForeground(dpy, wm->text_gc, 0x000000);

        int text_width = XTextWidth(wm->font, client->title, strlen(client->title));
        int text_x = (frame_width - text_width) / 2;

        XDrawString(dpy, win, wm->text_gc,
                   text_x,
                   wm->config.frame_width + 16,
                   client->title, strlen(client->title));
    }

    int button_width = 18;
    int button_spacing = 3;
    int total_buttons_width = button_width + 2 * (button_width + button_spacing);
    int start_x = frame_width - wm->config.frame_width - BUTTON_SPACING - total_buttons_width + 6;

    int close_x = start_x + 2 * (button_width + button_spacing);
    int maximize_x = start_x + (button_width + button_spacing);
    int minimize_x = start_x;
    int button_y = wm->config.frame_width + (TITLEBAR_HEIGHT - 16) / 2;

    draw_button(wm, win, close_x, button_y, 0, 0, 0, client->is_active);
    draw_button(wm, win, maximize_x, button_y, 0, 0, 1, client->is_active);
    draw_button(wm, win, minimize_x, button_y, 0, 0, 2, client->is_active);

    XSetForeground(dpy, wm->highlight_gc, 0xFFFFFF);
    XDrawLine(dpy, win, wm->highlight_gc,
              wm->config.frame_width,
              wm->config.frame_width + TITLEBAR_HEIGHT - 1,
              frame_width - wm->config.frame_width - 1,
              wm->config.frame_width + TITLEBAR_HEIGHT - 1);
}

void draw_frame_osx(AquaWm *wm, Client *client) {
    if (!client->decorated || client->is_fullscreen || client->is_dock || client->minimized) return;

    Display *dpy = wm->display;
    Window win = client->frame;
    int frame_width = client->width + 2 * wm->config.frame_width;
    int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

    XClearWindow(dpy, win);

    XSetForeground(dpy, wm->frame_gc, 0xD0D0D0);
    for (int i = 0; i < frame_height; i++) {
        int color_offset = (i % 3) * 0x10;
        unsigned int color = 0xD0D0D0 - color_offset;
        XSetForeground(dpy, wm->frame_gc, color);
        XDrawLine(dpy, win, wm->frame_gc, 0, i, frame_width - 1, i);
    }

    XSetForeground(dpy, wm->border_gc, 0xA0A0A0);
    XDrawRectangle(dpy, win, wm->border_gc, 0, 0, frame_width - 1, frame_height - 1);

    XSetForeground(dpy, wm->titlebar_gc, 0xD0D0D0);
    XFillRectangle(dpy, win, wm->titlebar_gc,
                   wm->config.frame_width,
                   wm->config.frame_width,
                   frame_width - 2 * wm->config.frame_width,
                   TITLEBAR_HEIGHT - wm->config.frame_width);

    for (int i = 0; i < TITLEBAR_HEIGHT - wm->config.frame_width; i += 2) {
        XSetForeground(dpy, wm->highlight_gc, 0xE0E0E0);
        XDrawLine(dpy, win, wm->highlight_gc,
                  wm->config.frame_width,
                  wm->config.frame_width + i,
                  frame_width - wm->config.frame_width - 1,
                  wm->config.frame_width + i);
    }

    if (client->title) {
        XSetForeground(dpy, wm->text_gc, 0x000000);

        int text_width = XTextWidth(wm->font, client->title, strlen(client->title));
        int text_x = (frame_width - text_width) / 2;

        XDrawString(dpy, win, wm->text_gc,
                   text_x,
                   wm->config.frame_width + 16,
                   client->title, strlen(client->title));
    }

    int close_x = wm->config.frame_width + BUTTON_SPACING;
    int minimize_x = wm->config.frame_width + BUTTON_SPACING + BUTTON_SIZE + BUTTON_SPACING;
    int maximize_x = wm->config.frame_width + BUTTON_SPACING + 2 * BUTTON_SIZE + 2 * BUTTON_SPACING;
    int button_y = wm->config.frame_width + (TITLEBAR_HEIGHT - BUTTON_SIZE) / 2;

    draw_button(wm, win, close_x, button_y, 0, 0, 0, client->is_active);
    draw_button(wm, win, maximize_x, button_y, 0, 0, 1, client->is_active);
    draw_button(wm, win, minimize_x, button_y, 0, 0, 2, client->is_active);

    XSetForeground(dpy, wm->highlight_gc, 0xA0A0A0);
    XDrawLine(dpy, win, wm->highlight_gc,
              wm->config.frame_width,
              wm->config.frame_width + TITLEBAR_HEIGHT - 1,
              frame_width - wm->config.frame_width - 1,
              wm->config.frame_width + TITLEBAR_HEIGHT - 1);
}

void draw_frame(AquaWm *wm, Client *client) {
    if (!client->decorated || client->is_fullscreen || client->is_dock || client->minimized) return;

    if (strcmp(wm->config.wm_theme, "aero") == 0) {
        draw_frame_aero(wm, client);
    } else if (strcmp(wm->config.wm_theme, "osx") == 0) {
        draw_frame_osx(wm, client);
    } else {
        draw_frame_classic(wm, client);
    }
}

void update_cursor(AquaWm *wm, Window window, int x, int y) {
    Client *client = find_client_by_frame(wm, window);
    if (!client) return;

    int frame_width = client->width + 2 * wm->config.frame_width;
    int close_x, maximize_x, minimize_x;
    int button_y = wm->config.frame_width + (TITLEBAR_HEIGHT - BUTTON_SIZE) / 2;

    if (strcmp(wm->config.wm_theme, "aero") == 0) {
        int button_width = 18;
        int button_spacing = 3;
        int total_buttons_width = button_width + 2 * (button_width + button_spacing);
        int start_x = frame_width - wm->config.frame_width - BUTTON_SPACING - total_buttons_width + 6;

        close_x = start_x + 2 * (button_width + button_spacing);
        maximize_x = start_x + (button_width + button_spacing);
        minimize_x = start_x;
    } else if (strcmp(wm->config.wm_theme, "osx") == 0) {
        close_x = wm->config.frame_width + BUTTON_SPACING;
        maximize_x = wm->config.frame_width + BUTTON_SPACING + BUTTON_SIZE + BUTTON_SPACING;
        minimize_x = wm->config.frame_width + BUTTON_SPACING + 2 * BUTTON_SIZE + 2 * BUTTON_SPACING;
    } else {
        close_x = wm->config.frame_width + BUTTON_SPACING;
        maximize_x = frame_width - wm->config.frame_width - BUTTON_SPACING - BUTTON_SIZE;
        minimize_x = frame_width - wm->config.frame_width - BUTTON_SPACING - 2 * BUTTON_SIZE - BUTTON_SPACING;
    }

    if (x >= close_x && x <= close_x + BUTTON_SIZE && y >= button_y && y <= button_y + BUTTON_SIZE) {
        XDefineCursor(wm->display, window, wm->cursor_normal);
        return;
    }
    if (x >= maximize_x && x <= maximize_x + BUTTON_SIZE && y >= button_y && y <= button_y + BUTTON_SIZE) {
        XDefineCursor(wm->display, window, wm->cursor_normal);
        return;
    }
    if (x >= minimize_x && x <= minimize_x + BUTTON_SIZE && y >= button_y && y <= button_y + BUTTON_SIZE) {
        XDefineCursor(wm->display, window, wm->cursor_normal);
        return;
    }

    Cursor new_cursor = wm->cursor_normal;

    if (y < TITLEBAR_HEIGHT) {
        new_cursor = wm->cursor_move;
    } else {
        if (x < RESIZE_HANDLE_SIZE) {
            if (y < TITLEBAR_HEIGHT + RESIZE_HANDLE_SIZE) new_cursor = wm->cursor_resize_diag2;
            else if (y > client->height + TITLEBAR_HEIGHT + wm->config.frame_width - RESIZE_HANDLE_SIZE) new_cursor = wm->cursor_resize_diag1;
            else new_cursor = wm->cursor_resize_hor;
        } else if (x > frame_width - RESIZE_HANDLE_SIZE) {
            if (y < TITLEBAR_HEIGHT + RESIZE_HANDLE_SIZE) new_cursor = wm->cursor_resize_diag1;
            else if (y > client->height + TITLEBAR_HEIGHT + wm->config.frame_width - RESIZE_HANDLE_SIZE) new_cursor = wm->cursor_resize_diag2;
            else new_cursor = wm->cursor_resize_hor;
        } else if (y > TITLEBAR_HEIGHT && y < TITLEBAR_HEIGHT + RESIZE_HANDLE_SIZE) {
            new_cursor = wm->cursor_resize_ver;
        } else if (y > client->height + TITLEBAR_HEIGHT + wm->config.frame_width - RESIZE_HANDLE_SIZE) {
            new_cursor = wm->cursor_resize_ver;
        }
    }

    XDefineCursor(wm->display, window, new_cursor);
}

int get_resize_edge(AquaWm *wm, Client *client, int x, int y) {
    if (!client || !client->decorated) return 0;

    int edge = 0;

    int frame_width = wm->config.frame_width;
    int client_frame_width = client->width + 2 * frame_width;
    int client_frame_height = client->height + TITLEBAR_HEIGHT + frame_width;

    if (x < RESIZE_HANDLE_SIZE) edge |= 1;
    if (x > client_frame_width - RESIZE_HANDLE_SIZE) edge |= 2;

    if (y > TITLEBAR_HEIGHT && y < TITLEBAR_HEIGHT + RESIZE_HANDLE_SIZE) edge |= 4;
    if (y > client_frame_height - RESIZE_HANDLE_SIZE) edge |= 8;

    return edge;
}

int is_over_button(int x, int y, int button_x, int button_y) {
    return (x >= button_x && x <= button_x + BUTTON_SIZE &&
            y >= button_y && y <= button_y + BUTTON_SIZE);
}

void toggle_maximize(AquaWm *wm, Client *client) {
    if (!client || client->is_fullscreen || client->is_dock || client->minimized) return;

    grab_server(wm);
    wm->suppress_configure = 1;

    if (client->is_maximized) {
        client->x = client->orig_x;
        client->y = client->orig_y;
        client->width = client->orig_width;
        client->height = client->orig_height;

        client->frame_x = client->x - wm->config.frame_width;
        client->frame_y = client->y - TITLEBAR_HEIGHT;
        client->is_maximized = 0;

        Atom atoms[] = {0, 0};
        XChangeProperty(wm->display, client->window, wm->net_wm_state,
                       XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, 0);

        int frame_width = client->width + 2 * wm->config.frame_width;
        int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

        XMoveResizeWindow(wm->display, client->frame,
                         client->frame_x, client->frame_y,
                         frame_width, frame_height);

        XResizeWindow(wm->display, client->window, client->width, client->height);
    } else {
        client->orig_x = client->x;
        client->orig_y = client->y;
        client->orig_width = client->width;
        client->orig_height = client->height;

        client->frame_x = 0;
        client->frame_y = 0;
        client->x = client->frame_x + wm->config.frame_width;
        client->y = client->frame_y + TITLEBAR_HEIGHT;
        client->width = wm->screen_width - 2 * wm->config.frame_width;
        client->height = wm->screen_height - TITLEBAR_HEIGHT - wm->config.frame_width;
        client->is_maximized = 1;

        Atom atoms[] = {wm->net_wm_state_maximized_horz, wm->net_wm_state_maximized_vert};
        XChangeProperty(wm->display, client->window, wm->net_wm_state,
                       XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, 2);

        XMoveResizeWindow(wm->display, client->frame,
                         client->frame_x, client->frame_y,
                         wm->screen_width,
                         wm->screen_height);

        XResizeWindow(wm->display, client->window,
                     wm->screen_width - 2 * wm->config.frame_width,
                     wm->screen_height - TITLEBAR_HEIGHT - wm->config.frame_width);
    }

    send_configure_notify(wm, client);
    if (client->decorated && !client->is_fullscreen && !client->minimized) {
        draw_frame(wm, client);
    }

    wm->suppress_configure = 0;
    ungrab_server(wm);
}

void toggle_fullscreen(AquaWm *wm, Client *client) {
    if (!client || client->is_dock || client->minimized) return;

    grab_server(wm);
    wm->suppress_configure = 1;

    if (client->is_fullscreen) {
        client->is_fullscreen = 0;
        client->is_maximized = 0;
        client->x = client->orig_x;
        client->y = client->orig_y;
        client->width = client->orig_width;
        client->height = client->orig_height;

        client->frame_x = client->x - wm->config.frame_width;
        client->frame_y = client->y - TITLEBAR_HEIGHT;

        Atom atoms[] = {0, 0};
        XChangeProperty(wm->display, client->window, wm->net_wm_state,
                       XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, 0);

        if (client->decorated) {
            XMapWindow(wm->display, client->frame);
            int frame_width = client->width + 2 * wm->config.frame_width;
            int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;
            XMoveResizeWindow(wm->display, client->frame,
                             client->frame_x, client->frame_y,
                             frame_width, frame_height);
            XResizeWindow(wm->display, client->window, client->width, client->height);
        } else {
            XMoveResizeWindow(wm->display, client->window,
                             client->x, client->y,
                             client->width, client->height);
        }

        force_client_position(wm, client);
    } else {
        client->orig_x = client->x;
        client->orig_y = client->y;
        client->orig_width = client->width;
        client->orig_height = client->height;

        client->is_fullscreen = 1;
        client->is_maximized = 0;
        client->x = 0;
        client->y = 0;
        client->width = wm->screen_width;
        client->height = wm->screen_height;
        client->frame_x = 0;
        client->frame_y = 0;

        Atom atoms[] = {wm->net_wm_state_fullscreen};
        XChangeProperty(wm->display, client->window, wm->net_wm_state,
                       XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, 1);

        if (client->decorated) {
            XMoveResizeWindow(wm->display, client->window, 0, 0, wm->screen_width, wm->screen_height);
            XUnmapWindow(wm->display, client->frame);
        } else {
            XMoveResizeWindow(wm->display, client->window, 0, 0, wm->screen_width, wm->screen_height);
        }
    }

    send_configure_notify(wm, client);
    if (client->decorated && !client->is_fullscreen && !client->minimized) {
        draw_frame(wm, client);
    }

    wm->suppress_configure = 0;
    ungrab_server(wm);
}

void minimize_window(AquaWm *wm, Client *client) {
    if (!client || client->is_dock) return;

    client->minimized = 1;
    client->minimized_state = 1;

    if (client->decorated) {
        XUnmapWindow(wm->display, client->frame);
    }
    XUnmapWindow(wm->display, client->window);
    client->mapped = False;

    update_client_list(wm);
}

void unminimize_window(AquaWm *wm, Client *client) {
    if (!client || client->is_dock || !client->minimized) return;

    client->minimized = 0;
    client->minimized_state = 0;

    if (client->decorated && !client->is_fullscreen) {
        XMapWindow(wm->display, client->frame);
    }
    XMapWindow(wm->display, client->window);
    client->mapped = True;

    if (wm->active_client && wm->active_client != client) {
        wm->active_client->is_active = 0;
        if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
            draw_frame(wm, wm->active_client);
        }
    }

    wm->active_client = client;
    client->is_active = 1;

    if (client->decorated && !client->is_fullscreen) {
        draw_frame(wm, client);
    }

    set_input_focus(wm, client);
    update_client_list(wm);
}

int is_dock_or_desktop(AquaWm *wm, Window window) {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int result = 0;

    if (XGetWindowProperty(wm->display, window, wm->net_wm_window_type,
                          0, 10, False, XA_ATOM, &type, &format,
                          &nitems, &bytes_after, &data) == Success && data) {
        Atom *atoms = (Atom*)data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == wm->net_wm_window_type_dock ||
                atoms[i] == wm->net_wm_window_type_desktop) {
                result = 1;
                break;
            }
        }
        XFree(data);
    }

    return result;
}

int is_likely_dock(AquaWm *wm, Window window) {
    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->display, window, &attr)) return 0;

    if (attr.height < 100 && (attr.y < 10 || attr.y > wm->screen_height - attr.height - 10)) {
        return 1;
    }

    if (attr.width < 100 && (attr.x < 10 || attr.x > wm->screen_width - attr.width - 10)) {
        return 1;
    }

    return 0;
}

void set_window_above(AquaWm *wm, Window window) {
    Atom atoms[] = {wm->net_wm_state_above};
    XChangeProperty(wm->display, window, wm->net_wm_state,
                   XA_ATOM, 32, PropModeReplace,
                   (unsigned char*)atoms, 1);
}

void enforce_dock_above(AquaWm *wm, Client *dock) {
    if (!dock || !dock->is_dock) return;

    set_window_above(wm, dock->window);
    XRaiseWindow(wm->display, dock->window);
}

void calculate_workarea(AquaWm *wm) {
    wm->workarea_x = wm->config.screen_margin;
    wm->workarea_y = wm->config.screen_top_margin;
    wm->workarea_width = wm->screen_width - 2 * wm->config.screen_margin;
    wm->workarea_height = wm->screen_height - wm->config.screen_top_margin - wm->config.screen_bottom_margin;
}

void enforce_dock_boundaries(AquaWm *wm) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->is_dock && wm->clients[i]->mapped) {
            Client *dock = wm->clients[i];

            if (dock->x < 0) {
                dock->x = 0;
                XMoveWindow(wm->display, dock->window, dock->x, dock->y);
            }
            if (dock->y < 0) {
                dock->y = 0;
                XMoveWindow(wm->display, dock->window, dock->x, dock->y);
            }
            if (dock->x + dock->width > wm->screen_width) {
                dock->x = wm->screen_width - dock->width;
                XMoveWindow(wm->display, dock->window, dock->x, dock->y);
            }
            if (dock->y + dock->width > wm->screen_height) {
                dock->y = wm->screen_height - dock->height;
                XMoveWindow(wm->display, dock->window, dock->x, dock->y);
            }
        }
    }
}

void update_dock_struts(AquaWm *wm) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->is_dock && wm->clients[i]->mapped) {
            Client *dock = wm->clients[i];

            Window root;
            int x, y;
            unsigned int width, height, border, depth;
            if (XGetGeometry(wm->display, dock->window, &root, &x, &y,
                           &width, &height, &border, &depth)) {

                dock->x = x;
                dock->y = y;
                dock->width = (int)width;
                dock->height = (int)height;

                if (y < wm->screen_height / 2) {
                    dock->dock_side = 0;
                    dock->strut_size = (int)height;
                } else if (y + (int)height > wm->screen_height * 3 / 4) {
                    dock->dock_side = 1;
                    dock->strut_size = (int)height;
                } else if (x < wm->screen_width / 2) {
                    dock->dock_side = 2;
                    dock->strut_size = (int)width;
                } else {
                    dock->dock_side = 3;
                    dock->strut_size = (int)width;
                }

                unsigned long strut[12] = {0};

                switch (dock->dock_side) {
                    case 0:
                        strut[2] = (unsigned long)height;
                        strut[6] = (unsigned long)x;
                        strut[7] = (unsigned long)(x + (int)width - 1);
                        break;
                    case 1:
                        strut[3] = (unsigned long)height;
                        strut[8] = (unsigned long)x;
                        strut[9] = (unsigned long)(x + (int)width - 1);
                        break;
                    case 2:
                        strut[0] = (unsigned long)width;
                        strut[4] = (unsigned long)y;
                        strut[5] = (unsigned long)(y + (int)height - 1);
                        break;
                    case 3:
                        strut[1] = (unsigned long)width;
                        strut[10] = (unsigned long)y;
                        strut[11] = (unsigned long)(y + (int)height - 1);
                        break;
                }

                XChangeProperty(wm->display, dock->window, wm->net_wm_strut,
                               XA_CARDINAL, 32, PropModeReplace,
                               (unsigned char*)strut, 4);

                XChangeProperty(wm->display, dock->window, wm->net_wm_strut_partial,
                               XA_CARDINAL, 32, PropModeReplace,
                               (unsigned char*)strut, 12);
            }
        }
    }

    calculate_workarea(wm);
}

void keep_docks_on_top(AquaWm *wm) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->is_dock && wm->clients[i]->mapped) {
            XRaiseWindow(wm->display, wm->clients[i]->window);
        }
    }
}

void apply_window_state(AquaWm *wm, Client *client) {
    if (client->is_fullscreen) {
        if (client->decorated) {
            XUnmapWindow(wm->display, client->frame);
        }
        XMoveResizeWindow(wm->display, client->window,
                         0, 0,
                         wm->screen_width, wm->screen_height);
    } else if (client->is_maximized && !client->minimized) {
        if (client->decorated) {
            XMoveResizeWindow(wm->display, client->frame,
                             0, 0,
                             wm->screen_width,
                             wm->screen_height);
            XResizeWindow(wm->display, client->window,
                         wm->screen_width - 2 * wm->config.frame_width,
                         wm->screen_height - TITLEBAR_HEIGHT - wm->config.frame_width);
        } else {
            XMoveResizeWindow(wm->display, client->window,
                             0, 0,
                             wm->screen_width,
                             wm->screen_height);
        }
    } else if (client->decorated && !client->minimized) {
        int frame_width = client->width + 2 * wm->config.frame_width;
        int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

        XMoveResizeWindow(wm->display, client->frame,
                         client->frame_x, client->frame_y,
                         frame_width, frame_height);
        XResizeWindow(wm->display, client->window, client->width, client->height);
    } else if (!client->minimized) {
        XMoveResizeWindow(wm->display, client->window,
                         client->frame_x, client->frame_y,
                         client->width, client->height);
    }
}

char* get_window_title(AquaWm *wm, Window window) {
    XTextProperty text_prop;
    char *title = NULL;

    Atom utf8_string = XInternAtom(wm->display, "UTF8_STRING", False);
    Atom net_wm_name = XInternAtom(wm->display, "_NET_WM_NAME", False);

    if (XGetWMName(wm->display, window, &text_prop)) {
        if (text_prop.value && text_prop.nitems > 0) {
            if (text_prop.encoding == utf8_string || text_prop.encoding == XA_STRING) {
                title = strdup((char*)text_prop.value);
            }
        }
        XFree(text_prop.value);
    }

    if (!title) {
        Atom type;
        int format;
        unsigned long nitems, bytes_after;
        unsigned char *data = NULL;

        if (XGetWindowProperty(wm->display, window, net_wm_name,
                              0, 1024, False, utf8_string, &type, &format,
                              &nitems, &bytes_after, &data) == Success && data && type == utf8_string) {
            if (nitems > 0) {
                title = strdup((char*)data);
                XFree(data);
            }
        }
    }

    if (!title) {
        title = strdup("Untitled");
    }

    return title;
}

void switch_workspace(AquaWm *wm, int workspace) {
    if (workspace < 0 || workspace >= MAX_WORKSPACES) return;

    wm->current_workspace = workspace;

    grab_server(wm);

    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->is_dock) {
            continue;
        }

        if (wm->clients[i]->workspace == workspace && !wm->clients[i]->minimized) {
            if (wm->clients[i]->decorated && !wm->clients[i]->is_fullscreen) {
                XMapWindow(wm->display, wm->clients[i]->frame);
            }
            XMapWindow(wm->display, wm->clients[i]->window);
            wm->clients[i]->mapped = True;
        } else if (wm->clients[i]->workspace == workspace && wm->clients[i]->minimized) {
            if (wm->clients[i]->decorated && !wm->clients[i]->is_fullscreen) {
                XUnmapWindow(wm->display, wm->clients[i]->frame);
            }
            XUnmapWindow(wm->display, wm->clients[i]->window);
            wm->clients[i]->mapped = False;
        } else {
            if (wm->clients[i]->decorated) {
                XUnmapWindow(wm->display, wm->clients[i]->frame);
            }
            XUnmapWindow(wm->display, wm->clients[i]->window);
            wm->clients[i]->mapped = False;
        }
    }

    long current_desktop = workspace;
    XChangeProperty(wm->display, wm->root, wm->net_current_desktop,
                   XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&current_desktop, 1);

    ungrab_server(wm);

    update_client_list(wm);
    draw_workspace_indicator(wm);
    keep_docks_on_top(wm);
}

void* socket_thread_func(void* arg);

void setup_socket_server(AquaWm *wm) {
    wm->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (wm->socket_fd < 0) return;

    int opt = 1;
    setsockopt(wm->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SOCKET_PORT);

    if (bind(wm->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(wm->socket_fd);
        wm->socket_fd = -1;
        return;
    }

    if (listen(wm->socket_fd, 5) < 0) {
        close(wm->socket_fd);
        wm->socket_fd = -1;
        return;
    }

    atomic_store(&wm->socket_active, 1);
    pthread_create(&wm->socket_thread, NULL, socket_thread_func, wm);
}

void* socket_thread_func(void* arg) {
    AquaWm *wm = (AquaWm*)arg;

    while (atomic_load(&wm->socket_active)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(wm->socket_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        char buffer[MAX_COMMAND_LEN];
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            char *trimmed = trim_whitespace(buffer);
            if (strcmp(trimmed, "reload") == 0) {
                atomic_store(&wm->reload_requested, 1);
            }
        }

        close(client_fd);
    }

    return NULL;
}

void minimize_all_windows(AquaWm *wm) {
    int all_minimized = 1;

    for (int i = 0; i < wm->num_clients; i++) {
        if (!wm->clients[i]->is_dock && wm->clients[i]->workspace == wm->current_workspace) {
            if (!wm->clients[i]->minimized) {
                all_minimized = 0;
                break;
            }
        }
    }

    if (all_minimized) {
        for (int i = 0; i < wm->num_clients; i++) {
            if (!wm->clients[i]->is_dock && wm->clients[i]->workspace == wm->current_workspace && wm->clients[i]->minimized) {
                unminimize_window(wm, wm->clients[i]);
            }
        }
    } else {
        for (int i = 0; i < wm->num_clients; i++) {
            if (!wm->clients[i]->is_dock && wm->clients[i]->workspace == wm->current_workspace && !wm->clients[i]->minimized) {
                minimize_window(wm, wm->clients[i]);
            }
        }
    }
}

void unminimize_all_windows(AquaWm *wm) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (!wm->clients[i]->is_dock && wm->clients[i]->workspace == wm->current_workspace && wm->clients[i]->minimized) {
            unminimize_window(wm, wm->clients[i]);
        }
    }
}

int handle_error(Display *display, XErrorEvent *error) {
    (void)display;
    (void)error;
    return 0;
}

int handle_io_error(Display *display) {
    (void)display;
    exit(1);
    return 0;
}

void update_all_window_borders(AquaWm *wm) {
    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->decorated && !wm->clients[i]->is_dock &&
            !wm->clients[i]->is_fullscreen && !wm->clients[i]->minimized) {
            draw_frame(wm, wm->clients[i]);
        }
    }
}

void reload_config(AquaWm *wm) {
    load_config(wm);
    load_wallpaper(wm);

    if (wm->font) {
        XFreeFont(wm->display, wm->font);
    }
    wm->font = XLoadQueryFont(wm->display, wm->config.font_name);
    if (!wm->font) {
        wm->font = XLoadQueryFont(wm->display, "fixed");
    }
    if (wm->font) {
        XSetFont(wm->display, wm->text_gc, wm->font->fid);
    }

    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->decorated && !wm->clients[i]->is_dock &&
            wm->clients[i]->mapped && !wm->clients[i]->minimized) {
            draw_frame(wm, wm->clients[i]);
        }
    }

    update_dock_struts(wm);
    calculate_workarea(wm);

    for (int i = 0; i < wm->num_clients; i++) {
        if (!wm->clients[i]->is_dock && !wm->clients[i]->minimized) {
            if (wm->clients[i]->is_maximized) {
                toggle_maximize(wm, wm->clients[i]);
                toggle_maximize(wm, wm->clients[i]);
            }
        }
    }

    draw_workspace_indicator(wm);
}

AquaWm* aquawm_init(int use_socket_server) {
    AquaWm *wm = calloc(1, sizeof(AquaWm));
    if (!wm) return NULL;

    wm->display = XOpenDisplay(NULL);
    if (!wm->display) {
        free(wm);
        return NULL;
    }

    XSetErrorHandler(handle_error);
    XSetIOErrorHandler(handle_io_error);

    wm->screen = DefaultScreen(wm->display);
    wm->root = RootWindow(wm->display, wm->screen);
    wm->visual = DefaultVisual(wm->display, wm->screen);
    wm->depth = DefaultDepth(wm->display, wm->screen);
    wm->screen_width = DisplayWidth(wm->display, wm->screen);
    wm->screen_height = DisplayHeight(wm->display, wm->screen);
    wm->workarea_x = 0;
    wm->workarea_y = 0;
    wm->workarea_width = wm->screen_width;
    wm->workarea_height = wm->screen_height;

    wm->colormap = DefaultColormap(wm->display, wm->screen);

    load_config(wm);

    load_wallpaper(wm);

    int border_width = wm->config.border_width > 0 ? wm->config.border_width : 1;

    wm->frame_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->button_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->border_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->desktop_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->highlight_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->text_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->titlebar_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->shadow_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->button_bg_gc = XCreateGC(wm->display, wm->root, 0, NULL);
    wm->button_fg_gc = XCreateGC(wm->display, wm->root, 0, NULL);

    XSetLineAttributes(wm->display, wm->border_gc, border_width, LineSolid, CapButt, JoinMiter);
    XSetLineAttributes(wm->display, wm->shadow_gc, 1, LineSolid, CapButt, JoinMiter);

    wm->font = XLoadQueryFont(wm->display, wm->config.font_name);
    if (!wm->font) {
        wm->font = XLoadQueryFont(wm->display, "fixed");
    }
    if (wm->font) {
        XSetFont(wm->display, wm->text_gc, wm->font->fid);
    }

    wm->wm_protocols = XInternAtom(wm->display, "WM_PROTOCOLS", False);
    wm->wm_delete_window = XInternAtom(wm->display, "WM_DELETE_WINDOW", False);
    wm->wm_take_focus = XInternAtom(wm->display, "WM_TAKE_FOCUS", False);
    wm->net_wm_state = XInternAtom(wm->display, "_NET_WM_STATE", False);
    wm->net_wm_state_maximized_vert = XInternAtom(wm->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    wm->net_wm_state_maximized_horz = XInternAtom(wm->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    wm->net_wm_state_fullscreen = XInternAtom(wm->display, "_NET_WM_STATE_FULLSCREEN", False);
    wm->net_active_window = XInternAtom(wm->display, "_NET_ACTIVE_WINDOW", False);
    wm->wm_transient_for = XInternAtom(wm->display, "WM_TRANSIENT_FOR", False);
    wm->utf8_string = XInternAtom(wm->display, "UTF8_STRING", False);
    wm->net_wm_window_type = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE", False);
    wm->net_wm_window_type_dock = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    wm->net_wm_window_type_desktop = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    wm->net_wm_strut = XInternAtom(wm->display, "_NET_WM_STRUT", False);
    wm->net_wm_strut_partial = XInternAtom(wm->display, "_NET_WM_STRUT_PARTIAL", False);
    wm->net_wm_state_above = XInternAtom(wm->display, "_NET_WM_STATE_ABOVE", False);
    wm->net_wm_state_stays_on_top = XInternAtom(wm->display, "_NET_WM_STATE_STAYS_ON_TOP", False);
    wm->net_wm_state_below = XInternAtom(wm->display, "_NET_WM_STATE_BELOW", False);
    wm->net_wm_state_modal = XInternAtom(wm->display, "_NET_WM_STATE_MODAL", False);
    wm->net_wm_window_type_dialog = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    wm->net_wm_window_type_normal = XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    wm->net_wm_desktop = XInternAtom(wm->display, "_NET_WM_DESKTOP", False);
    wm->net_wm_state_hidden = XInternAtom(wm->display, "_NET_WM_STATE_HIDDEN", False);
    wm->net_wm_state_shaded = XInternAtom(wm->display, "_NET_WM_STATE_SHADED", False);
    wm->net_wm_state_skip_taskbar = XInternAtom(wm->display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    wm->net_wm_state_skip_pager = XInternAtom(wm->display, "_NET_WM_STATE_SKIP_PAGER", False);
    wm->net_wm_state_demands_attention = XInternAtom(wm->display, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
    wm->net_wm_name = XInternAtom(wm->display, "_NET_WM_NAME", False);
    wm->net_supporting_wm_check = XInternAtom(wm->display, "_NET_SUPPORTING_WM_CHECK", False);
    wm->net_supported = XInternAtom(wm->display, "_NET_SUPPORTING_WM_CHECK", False);
    wm->net_number_of_desktops = XInternAtom(wm->display, "_NET_NUMBER_OF_DESKTOPS", False);
    wm->net_current_desktop = XInternAtom(wm->display, "_NET_CURRENT_DESKTOP", False);
    wm->net_client_list = XInternAtom(wm->display, "_NET_CLIENT_LIST", False);
    wm->net_client_list_stacking = XInternAtom(wm->display, "_NET_CLIENT_LIST_STACKING", False);
    wm->net_wm_window_opacity = XInternAtom(wm->display, "_NET_WM_WINDOW_OPACITY", False);
    wm->net_wm_user_time = XInternAtom(wm->display, "_NET_WM_USER_TIME", False);
    wm->motif_wm_hints = XInternAtom(wm->display, "_MOTIF_WM_HINTS", False);

    wm->cursor_normal = XCreateFontCursor(wm->display, XC_left_ptr);
    wm->cursor_resize = XCreateFontCursor(wm->display, XC_bottom_right_corner);
    wm->cursor_move = XCreateFontCursor(wm->display, XC_fleur);
    wm->cursor_resize_hor = XCreateFontCursor(wm->display, XC_sb_h_double_arrow);
    wm->cursor_resize_ver = XCreateFontCursor(wm->display, XC_sb_v_double_arrow);
    wm->cursor_resize_diag1 = XCreateFontCursor(wm->display, XC_bottom_right_corner);
    wm->cursor_resize_diag2 = XCreateFontCursor(wm->display, XC_bottom_left_corner);
    wm->cursor_wait = XCreateFontCursor(wm->display, XC_watch);
    wm->cursor_crosshair = XCreateFontCursor(wm->display, XC_crosshair);

    XDefineCursor(wm->display, wm->root, wm->cursor_normal);

    wm->num_clients = 0;
    wm->active_client = NULL;
    wm->moving_client = NULL;
    wm->grabbed_client = NULL;
    wm->is_moving = 0;
    wm->is_resizing = 0;
    wm->resize_edge = 0;
    wm->drag_button = 0;
    wm->mouse_grabbed = 0;
    wm->grab_window = None;
    atomic_store(&wm->reload_requested, 0);
    wm->socket_fd = -1;
    atomic_store(&wm->socket_active, 0);
    wm->current_workspace = 0;
    wm->should_exit = 0;
    wm->allow_input_focus = 1;
    wm->last_click_time = 0;
    wm->last_click_window = None;
    wm->resize_guard = 0;
    wm->suppress_configure = 0;
    wm->force_sdl_decorations = 1;
    wm->pending_configure = 0;

    int opcode, event, error;
    wm->xrender_supported = XQueryExtension(wm->display, "RENDER", &opcode, &event, &error);
    wm->shape_supported = XQueryExtension(wm->display, "SHAPE", &opcode, &event, &error);
    wm->compositing_active = 0;

    XSetWindowAttributes attrs;
    unsigned long attr_mask = CWEventMask | CWCursor;

    attrs.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                       ButtonPressMask | ButtonReleaseMask |
                       KeyPressMask | KeyReleaseMask |
                       EnterWindowMask | LeaveWindowMask |
                       PointerMotionMask |
                       FocusChangeMask | PropertyChangeMask |
                       ExposureMask;

    attrs.cursor = wm->cursor_normal;

    XChangeWindowAttributes(wm->display, wm->root, attr_mask, &attrs);

    XClearWindow(wm->display, wm->root);
    XFlush(wm->display);

    for (int i = 0; i < wm->config.keybinding_count; i++) {
        KeyCode keycode = 0;
        unsigned int modifiers = 0;

        if (strstr(wm->config.keybindings[i].keysym, "Alt+") == wm->config.keybindings[i].keysym) {
            modifiers |= Mod1Mask;
            char *keysym_name = wm->config.keybindings[i].keysym + 4;
            keycode = XKeysymToKeycode(wm->display, XStringToKeysym(keysym_name));
        } else if (strstr(wm->config.keybindings[i].keysym, "Super+") == wm->config.keybindings[i].keysym) {
            modifiers |= Mod4Mask;
            char *keysym_name = wm->config.keybindings[i].keysym + 6;
            keycode = XKeysymToKeycode(wm->display, XStringToKeysym(keysym_name));
        } else if (strcmp(wm->config.keybindings[i].keysym, "F11") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_F11);
        } else if (strcmp(wm->config.keybindings[i].keysym, "F4") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_F4);
        } else if (strcmp(wm->config.keybindings[i].keysym, "Tab") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_Tab);
        } else if (strcmp(wm->config.keybindings[i].keysym, "D") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_d);
        } else if (strcmp(wm->config.keybindings[i].keysym, "A") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_a);
        }

        if (keycode != 0) {
            XGrabKey(wm->display, keycode, modifiers, wm->root, True, GrabModeAsync, GrabModeAsync);
        }
    }

    if (wm->config.keybinding_count == 0) {
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_F4),
                 Mod4Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_Tab),
                 Mod4Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_F11),
                 0, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_1),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_2),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_3),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_4),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_5),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_6),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_Tab),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_d),
                 Mod4Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_d),
                 Mod4Mask | ShiftMask, wm->root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(wm->display, XKeysymToKeycode(wm->display, XK_a),
                 Mod1Mask, wm->root, True, GrabModeAsync, GrabModeAsync);
    }

    setup_wm_properties(wm);
    draw_workspace_indicator(wm);

    if (use_socket_server) {
        setup_socket_server(wm);
    }

    atomic_store(&wm->reload_requested, 1);

    return wm;
}

void handle_map_request(AquaWm *wm, XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;

    Client *existing = find_client_by_window(wm, ev->window);
    if (existing) {
        if (existing->is_dock) {
            XMapWindow(wm->display, existing->window);
            enforce_dock_above(wm, existing);
        } else if (existing->decorated && !existing->is_fullscreen && !existing->minimized) {
            if (existing->workspace == wm->current_workspace) {
                XMapWindow(wm->display, existing->frame);
            }
        } else if (!existing->minimized) {
            if (existing->workspace == wm->current_workspace) {
                XMapWindow(wm->display, existing->window);
            }
        }
        existing->mapped = True;
        keep_docks_on_top(wm);
        update_client_list(wm);
        return;
    }

    if (wm->num_clients >= MAX_CLIENTS) return;

    Client *client = calloc(1, sizeof(Client));
    client->window = ev->window;
    client->workspace = wm->current_workspace;
    client->minimized = 0;
    client->minimized_state = 0;

    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->display, client->window, &attr)) {
        free(client);
        return;
    }

    if (is_override_redirect_window(wm, client->window)) {
        XMapWindow(wm->display, client->window);
        free(client);
        return;
    }

    if (is_special_window_type(wm, client->window)) {
        client->decorated = 0;
        client->frame = client->window;
        client->is_dock = 0;
        client->title = get_window_title(wm, client->window);

        XMapWindow(wm->display, client->window);

        client->x = attr.x;
        client->y = attr.y;
        client->width = attr.width;
        client->height = attr.height;
        client->frame_x = client->x;
        client->frame_y = client->y;
        client->mapped = True;

        wm->clients[wm->num_clients++] = client;
        update_client_list(wm);
        return;
    }

    int dock_type = is_dock_or_desktop(wm, client->window);
    if (dock_type || is_likely_dock(wm, client->window)) {
        client->decorated = 0;
        client->frame = client->window;
        client->is_dock = 1;
        client->dock_side = -1;
        client->strut_size = 0;
        client->title = get_window_title(wm, client->window);

        enforce_dock_above(wm, client);

        XMapWindow(wm->display, client->window);
        XRaiseWindow(wm->display, client->window);

        client->x = attr.x;
        client->y = attr.y;
        client->width = attr.width;
        client->height = attr.height;
        client->frame_x = client->x;
        client->frame_y = client->y;
        client->mapped = True;

        wm->clients[wm->num_clients++] = client;

        update_dock_struts(wm);
        keep_docks_on_top(wm);
        return;
    }

    client->is_sdl_app = is_sdl_app(wm, client->window);

    int decorations = get_motif_decorations(wm, client->window);
    if (wm->force_sdl_decorations && client->is_sdl_app) {
        decorations = 1;
    }

    if (!decorations && !client->is_sdl_app) {
        client->decorated = 0;
        client->frame = client->window;
        client->is_dock = 0;
        client->title = get_window_title(wm, client->window);

        client->width = attr.width > MIN_WIDTH ? attr.width : MIN_WIDTH;
        client->height = attr.height > MIN_HEIGHT ? attr.height : MIN_HEIGHT;
        client->x = attr.x;
        client->y = attr.y;
        client->frame_x = client->x;
        client->frame_y = client->y;

        client->force_center = 1;
        initialize_window_position(wm, client);

        XMapWindow(wm->display, client->window);
        XMoveResizeWindow(wm->display, client->window,
                         client->frame_x, client->frame_y,
                         client->width, client->height);

        client->mapped = True;

        wm->clients[wm->num_clients++] = client;
        update_client_list(wm);
        return;
    }

    client->decorated = 1;
    client->is_dock = 0;
    client->ignore_unmap = 0;
    client->is_active = 0;
    client->is_maximized = 0;
    client->is_fullscreen = 0;
    client->title = get_window_title(wm, client->window);
    client->transient_for = None;
    client->has_gtk_extents = 0;
    client->gtk_left = client->gtk_right = client->gtk_top = client->gtk_bottom = 0;
    client->is_transient = 0;
    client->alt_move_active = 0;
    client->force_center = 1;

    update_gtk_extents(wm, client);

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(wm->display, client->window, wm->wm_transient_for,
                          0, 1, False, XA_WINDOW, &type, &format,
                          &nitems, &bytes_after, &data) == Success && data && nitems > 0) {
        client->transient_for = *(Window*)data;
        client->is_transient = 1;
        XFree(data);
    }

    client->width = attr.width > MIN_WIDTH ? attr.width : MIN_WIDTH;
    client->height = attr.height > MIN_HEIGHT ? attr.height : MIN_HEIGHT;

    client->x = attr.x;
    client->y = attr.y;

    client->frame_x = client->x - wm->config.frame_width;
    client->frame_y = client->y - TITLEBAR_HEIGHT;

    center_window(wm, client);

    client->orig_x = client->x;
    client->orig_y = client->y;
    client->orig_width = client->width;
    client->orig_height = client->height;

    reparent_window(wm, client);

    client->mapped = False;

    wm->clients[wm->num_clients++] = client;

    if (client->workspace == wm->current_workspace) {
        if (client->decorated) {
            XMapWindow(wm->display, client->frame);
            XDefineCursor(wm->display, client->frame, wm->cursor_normal);
        }
        XMapWindow(wm->display, client->window);
        client->mapped = True;

        if (client->decorated) {
            lock_client_to_frame(wm, client);
            force_client_position(wm, client);
            draw_frame(wm, client);
        }
    }

    update_client_list(wm);
}

void handle_map_notify(AquaWm *wm, XEvent *e) {
    XMapEvent *ev = &e->xmap;

    Client *client = find_client_by_window(wm, ev->window);
    if (client) {
        client->mapped = True;
        client->ignore_unmap = 0;

        if (client->is_dock) {
            enforce_dock_above(wm, client);
            XRaiseWindow(wm->display, client->window);
            update_dock_struts(wm);
            keep_docks_on_top(wm);
        } else if (!client->minimized) {
            if (!wm->active_client && client->workspace == wm->current_workspace) {
                wm->active_client = client;
                client->is_active = 1;
                set_input_focus(wm, client);
            }

            if (client->workspace == wm->current_workspace &&
                !client->is_fullscreen && client->decorated) {
                draw_frame(wm, client);
                if (!client->is_sdl_app) {
                    lock_client_to_frame(wm, client);
                }
            }
        }

        keep_docks_on_top(wm);
        update_client_list(wm);
    }
}

void handle_configure_notify(AquaWm *wm, XEvent *e) {
    XConfigureEvent *ev = &e->xconfigure;

    if (ev->window == wm->root) return;

    Client *client = find_client_by_window(wm, ev->window);
    if (client && !client->is_dock && !client->minimized) {
        if (ev->send_event) return;

        if (wm->resize_guard > 0) {
            wm->resize_guard--;
            return;
        }

        if (client->is_sdl_app || !client->decorated) {
            if (!client->is_sdl_app) {
                client->frame_x = ev->x;
                client->frame_y = ev->y;
                client->width = ev->width;
                client->height = ev->height;
                client->x = client->frame_x;
                client->y = client->frame_y;

                XMoveResizeWindow(wm->display, client->window,
                                 client->frame_x, client->frame_y,
                                 client->width, client->height);
            }
        } else if (client->decorated) {
            if (abs(client->x - ev->x) > 2 || abs(client->y - ev->y) > 2 ||
                abs(client->width - ev->width) > 2 || abs(client->height - ev->height) > 2) {

                wm->resize_guard = 3;

                client->x = ev->x;
                client->y = ev->y;
                client->width = ev->width;
                client->height = ev->height;

                client->frame_x = client->x - wm->config.frame_width;
                client->frame_y = client->y - TITLEBAR_HEIGHT;

                XMoveResizeWindow(wm->display, client->frame,
                                client->frame_x, client->frame_y,
                                client->width + 2 * wm->config.frame_width,
                                client->height + TITLEBAR_HEIGHT + wm->config.frame_width);

                if (!client->is_fullscreen) {
                    draw_frame(wm, client);
                }

                force_client_position(wm, client);
            }
        }
    }
}

void handle_configure_request(AquaWm *wm, XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;

    Client *client = find_client_by_window(wm, ev->window);

    if (client) {
        if (client->is_dock) {
            XWindowChanges wc;
            wc.x = ev->x;
            wc.y = ev->y;
            wc.width = ev->width;
            wc.height = ev->height;
            wc.border_width = ev->border_width;
            wc.sibling = ev->above;
            wc.stack_mode = ev->detail;

            XConfigureWindow(wm->display, ev->window, ev->value_mask, &wc);

            client->x = ev->x;
            client->y = ev->y;
            client->width = ev->width;
            client->height = ev->height;

            update_dock_struts(wm);
            keep_docks_on_top(wm);
            enforce_dock_boundaries(wm);
            return;
        }

        if (client->is_fullscreen || client->minimized) {
            return;
        }

        int new_width = client->width;
        int new_height = client->height;
        int new_x = client->x;
        int new_y = client->y;

        if (ev->value_mask & CWWidth) {
            new_width = ev->width;
            if (new_width < MIN_WIDTH) new_width = MIN_WIDTH;
        }
        if (ev->value_mask & CWHeight) {
            new_height = ev->height;
            if (new_height < MIN_HEIGHT) new_height = MIN_HEIGHT;
        }
        if (ev->value_mask & CWX) {
            new_x = ev->x;
            if (client->decorated) {
                new_x -= wm->config.frame_width;
            }
        }
        if (ev->value_mask & CWY) {
            new_y = ev->y;
            if (client->decorated) {
                new_y -= TITLEBAR_HEIGHT;
            }
        }

        if (new_width != client->width || new_height != client->height ||
            new_x != client->x || new_y != client->y) {

            client->x = new_x;
            client->y = new_y;
            client->width = new_width;
            client->height = new_height;

            if (client->decorated) {
                client->frame_x = client->x - wm->config.frame_width;
                client->frame_y = client->y - TITLEBAR_HEIGHT;
            } else {
                client->frame_x = client->x;
                client->frame_y = client->y;
            }

            wm->resize_guard = 3;
            wm->suppress_configure = 1;

            apply_window_state(wm, client);
            force_client_position(wm, client);
            if (!client->is_fullscreen && client->decorated) {
                draw_frame(wm, client);
            }
            send_configure_notify(wm, client);

            wm->suppress_configure = 0;
        }

        keep_docks_on_top(wm);
    } else {
        XWindowChanges wc;
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;

        XConfigureWindow(wm->display, ev->window, ev->value_mask, &wc);
    }
}

void handle_button_press(AquaWm *wm, XEvent *e) {
    XButtonEvent *ev = &e->xbutton;
    Client *client = find_client_by_frame(wm, ev->window);

    if (wm->mouse_grabbed && wm->grab_window != ev->window) {
        return;
    }

    if (!client) {
        client = find_client_by_window(wm, ev->window);
        if (client) {
            if (client->is_dock) {
                keep_docks_on_top(wm);
                return;
            }

            if (client->minimized) {
                return;
            }

            if (ev->state & Mod1Mask) {
                wm->is_moving = 1;
                wm->moving_client = client;
                wm->move_start_x = ev->x_root;
                wm->move_start_y = ev->y_root;
                wm->drag_button = ev->button;
                wm->mouse_grabbed = 1;
                wm->grab_window = client->decorated ? client->frame : client->window;
                wm->grabbed_client = client;
                client->alt_move_active = 1;

                XGrabPointer(wm->display, wm->grab_window, True,
                            ButtonMotionMask | ButtonReleaseMask,
                            GrabModeAsync, GrabModeAsync,
                            None, wm->cursor_move, CurrentTime);
                return;
            }

            if (!client->decorated || client->is_sdl_app) {
                if (wm->active_client != client) {
                    if (wm->active_client) {
                        wm->active_client->is_active = 0;
                        if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                            draw_frame(wm, wm->active_client);
                        }
                    }
                    wm->active_client = client;
                    client->is_active = 1;
                    if (client->decorated && !client->is_fullscreen) {
                        draw_frame(wm, client);
                    }
                    set_input_focus(wm, client);
                }
                return;
            }
        }
        return;
    }

    if (client->decorated && !client->is_fullscreen && !client->is_dock && !client->minimized) {
        XRaiseWindow(wm->display, client->frame);

        time_t now = time(NULL);
        if (wm->last_click_window == client->frame &&
            now - wm->last_click_time < wm->config.double_click_time / 1000) {
            toggle_maximize(wm, client);
            wm->last_click_time = 0;
            wm->last_click_window = None;
            return;
        }

        wm->last_click_time = now;
        wm->last_click_window = client->frame;

        if (wm->active_client != client) {
            if (wm->active_client) {
                wm->active_client->is_active = 0;
                if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                    draw_frame(wm, wm->active_client);
                }
            }
            wm->active_client = client;
            client->is_active = 1;
            draw_frame(wm, client);
        }

        set_input_focus(wm, client);
        wm->drag_button = ev->button;

        int local_x = ev->x;
        int local_y = ev->y;

        if (local_y < TITLEBAR_HEIGHT || (ev->state & Mod1Mask)) {
            if (ev->state & Mod1Mask) {
                client->alt_move_active = 1;
            }

            int frame_width = client->width + 2 * wm->config.frame_width;

            int close_x, maximize_x, minimize_x;
            int button_y = wm->config.frame_width + (TITLEBAR_HEIGHT - BUTTON_SIZE) / 2;

            if (strcmp(wm->config.wm_theme, "aero") == 0) {
                int button_width = 18;
                int button_spacing = 3;
                int total_buttons_width = button_width + 2 * (button_width + button_spacing);
                int start_x = frame_width - wm->config.frame_width - BUTTON_SPACING - total_buttons_width + 6;

                close_x = start_x + 2 * (button_width + button_spacing);
                maximize_x = start_x + (button_width + button_spacing);
                minimize_x = start_x;
            } else if (strcmp(wm->config.wm_theme, "osx") == 0) {
                close_x = wm->config.frame_width + BUTTON_SPACING;
                maximize_x = wm->config.frame_width + BUTTON_SPACING + BUTTON_SIZE + BUTTON_SPACING;
                minimize_x = wm->config.frame_width + BUTTON_SPACING + 2 * BUTTON_SIZE + 2 * BUTTON_SPACING;
            } else {
                close_x = wm->config.frame_width + BUTTON_SPACING;
                maximize_x = frame_width - wm->config.frame_width - BUTTON_SPACING - BUTTON_SIZE;
                minimize_x = frame_width - wm->config.frame_width - BUTTON_SPACING - 2 * BUTTON_SIZE - BUTTON_SPACING;
            }

            if (!(ev->state & Mod1Mask) && is_over_button(local_x, local_y, close_x, button_y)) {
                XEvent ce;
                memset(&ce, 0, sizeof(ce));
                ce.type = ClientMessage;
                ce.xclient.window = client->window;
                ce.xclient.message_type = wm->wm_protocols;
                ce.xclient.format = 32;
                ce.xclient.data.l[0] = wm->wm_delete_window;
                ce.xclient.data.l[1] = CurrentTime;

                XSendEvent(wm->display, client->window, False, NoEventMask, &ce);
                return;
            } else if (!(ev->state & Mod1Mask) && is_over_button(local_x, local_y, maximize_x, button_y)) {
                toggle_maximize(wm, client);
                return;
            } else if (!(ev->state & Mod1Mask) && is_over_button(local_x, local_y, minimize_x, button_y)) {
                minimize_window(wm, client);
                return;
            }

            wm->is_moving = 1;
            wm->moving_client = client;
            wm->move_start_x = ev->x_root;
            wm->move_start_y = ev->y_root;
            wm->mouse_grabbed = 1;
            wm->grab_window = client->frame;
            wm->grabbed_client = client;

            XGrabPointer(wm->display, client->frame, True,
                        ButtonMotionMask | ButtonReleaseMask,
                        GrabModeAsync, GrabModeAsync,
                        wm->root, wm->cursor_move, CurrentTime);
        } else {
            int edge = get_resize_edge(wm, client, local_x, local_y);
            if (edge != 0) {
                wm->is_resizing = 1;
                wm->resize_edge = edge;
                wm->moving_client = client;
                wm->drag_start_x = ev->x_root;
                wm->drag_start_y = ev->y_root;
                client->orig_x = client->x;
                client->orig_y = client->y;
                client->orig_width = client->width;
                client->orig_height = client->height;
                wm->mouse_grabbed = 1;
                wm->grab_window = client->frame;
                wm->grabbed_client = client;

                XGrabPointer(wm->display, client->frame, True,
                            ButtonMotionMask | ButtonReleaseMask,
                            GrabModeAsync, GrabModeAsync,
                            wm->root, wm->cursor_resize, CurrentTime);
            }
        }

        keep_docks_on_top(wm);
    }
}

void handle_motion_notify(AquaWm *wm, XEvent *e) {
    XMotionEvent *ev = &e->xmotion;

    if (wm->config.focus_follows_mouse && !wm->mouse_grabbed) {
        Window root, child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;

        if (XQueryPointer(wm->display, wm->root, &root, &child,
                         &root_x, &root_y, &win_x, &win_y, &mask)) {
            if (child != None) {
                Client *client = find_client_by_window(wm, child);
                if (!client) client = find_client_by_frame(wm, child);

                if (client && client != wm->active_client && !client->is_dock &&
                    client->workspace == wm->current_workspace && !client->minimized) {
                    if (wm->active_client) {
                        wm->active_client->is_active = 0;
                        if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                            draw_frame(wm, wm->active_client);
                        }
                    }

                    wm->active_client = client;
                    client->is_active = 1;
                    if (client->decorated && !client->is_fullscreen) {
                        draw_frame(wm, client);
                    }
                    set_input_focus(wm, client);
                }
            }
        }
    }

    if (!wm->mouse_grabbed || !wm->moving_client) return;

    Client *client = wm->moving_client;

    if (!client || client->window == None || client->minimized) {
        wm->is_moving = 0;
        wm->is_resizing = 0;
        wm->moving_client = NULL;
        wm->mouse_grabbed = 0;
        wm->grab_window = None;
        XUngrabPointer(wm->display, CurrentTime);
        return;
    }

    if (wm->is_moving) {
        int dx = ev->x_root - wm->move_start_x;
        int dy = ev->y_root - wm->move_start_y;

        wm->move_start_x = ev->x_root;
        wm->move_start_y = ev->y_root;

        if (client->decorated) {
            client->frame_x += dx;
            client->frame_y += dy;
            client->x = client->frame_x + wm->config.frame_width;
            client->y = client->frame_y + TITLEBAR_HEIGHT;
        } else {
            client->frame_x += dx;
            client->frame_y += dy;
            client->x = client->frame_x;
            client->y = client->frame_y;
        }

        if (client->decorated) {
            XMoveWindow(wm->display, client->frame, client->frame_x, client->frame_y);
        } else {
            XMoveWindow(wm->display, client->window, client->frame_x, client->frame_y);
        }

        send_configure_notify(wm, client);
    } else if (wm->is_resizing && client->decorated) {
        int dx = ev->x_root - wm->drag_start_x;
        int dy = ev->y_root - wm->drag_start_y;

        int new_x = client->orig_x;
        int new_y = client->orig_y;
        int new_width = client->orig_width;
        int new_height = client->orig_height;

        if (wm->resize_edge & 1) {
            new_x = client->orig_x + dx;
            new_width = client->orig_width - dx;
            if (new_width < MIN_WIDTH) {
                new_width = MIN_WIDTH;
                new_x = client->orig_x + client->orig_width - MIN_WIDTH;
            }
            if (new_x < 0) {
                new_x = 0;
                new_width = client->orig_x + client->orig_width;
            }
        }
        if (wm->resize_edge & 2) {
            new_width = client->orig_width + dx;
            if (new_width < MIN_WIDTH) new_width = MIN_WIDTH;
            if (new_x + new_width > wm->screen_width) {
                new_width = wm->screen_width - new_x;
            }
        }
        if (wm->resize_edge & 4) {
            new_y = client->orig_y + dy;
            new_height = client->orig_height - dy;
            if (new_height < MIN_HEIGHT) {
                new_height = MIN_HEIGHT;
                new_y = client->orig_y + client->orig_height - MIN_HEIGHT;
            }
            if (new_y < 0) {
                new_y = 0;
                new_height = client->orig_y + client->orig_height;
            }
        }
        if (wm->resize_edge & 8) {
            new_height = client->orig_height + dy;
            if (new_height < MIN_HEIGHT) new_height = MIN_HEIGHT;
            if (new_y + new_height > wm->screen_height) {
                new_height = wm->screen_height - new_y;
            }
        }

        if (new_width < MIN_WIDTH) new_width = MIN_WIDTH;
        if (new_height < MIN_HEIGHT) new_height = MIN_HEIGHT;

        client->x = new_x;
        client->y = new_y;
        client->width = new_width;
        client->height = new_height;

        client->frame_x = client->x - wm->config.frame_width;
        client->frame_y = client->y - TITLEBAR_HEIGHT;

        if (wm->config.opaque_resize) {
            int frame_width = client->width + 2 * wm->config.frame_width;
            int frame_height = client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

            XMoveResizeWindow(wm->display, client->frame,
                            client->frame_x, client->frame_y,
                            frame_width, frame_height);

            XResizeWindow(wm->display, client->window, client->width, client->height);
        }

        send_configure_notify(wm, client);
    }

    keep_docks_on_top(wm);
}

void handle_button_release(AquaWm *wm, XEvent *e) {
    XButtonEvent *ev = &e->xbutton;

    if (ev->button == wm->drag_button && (wm->is_moving || wm->is_resizing)) {
        XUngrabPointer(wm->display, CurrentTime);

        if (wm->grabbed_client) {
            wm->grabbed_client->alt_move_active = 0;
        }

        if (wm->grabbed_client && wm->grabbed_client->decorated &&
            !wm->grabbed_client->is_fullscreen && !wm->grabbed_client->minimized) {
            if (!wm->config.opaque_resize && wm->is_resizing) {
                int frame_width = wm->grabbed_client->width + 2 * wm->config.frame_width;
                int frame_height = wm->grabbed_client->height + TITLEBAR_HEIGHT + wm->config.frame_width;

                XMoveResizeWindow(wm->display, wm->grabbed_client->frame,
                                wm->grabbed_client->frame_x, wm->grabbed_client->frame_y,
                                frame_width, frame_height);

                XResizeWindow(wm->display, wm->grabbed_client->window,
                            wm->grabbed_client->width, wm->grabbed_client->height);
            }

            draw_frame(wm, wm->grabbed_client);
        }

        wm->is_moving = 0;
        wm->is_resizing = 0;
        wm->resize_edge = 0;
        wm->mouse_grabbed = 0;
        wm->grab_window = None;
        wm->moving_client = NULL;
        wm->grabbed_client = NULL;
        wm->drag_button = 0;

        keep_docks_on_top(wm);
    }
}

void handle_key_press(AquaWm *wm, XEvent *e) {
    XKeyEvent *ev = &e->xkey;

    for (int i = 0; i < wm->config.keybinding_count; i++) {
        KeyCode keycode = 0;
        unsigned int modifiers = 0;

        if (strstr(wm->config.keybindings[i].keysym, "Alt+") == wm->config.keybindings[i].keysym) {
            modifiers |= Mod1Mask;
            char *keysym_name = wm->config.keybindings[i].keysym + 4;
            keycode = XKeysymToKeycode(wm->display, XStringToKeysym(keysym_name));
        } else if (strstr(wm->config.keybindings[i].keysym, "Super+") == wm->config.keybindings[i].keysym) {
            modifiers |= Mod4Mask;
            char *keysym_name = wm->config.keybindings[i].keysym + 6;
            keycode = XKeysymToKeycode(wm->display, XStringToKeysym(keysym_name));
        } else if (strcmp(wm->config.keybindings[i].keysym, "F11") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_F11);
            modifiers = 0;
        } else if (strcmp(wm->config.keybindings[i].keysym, "F4") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_F4);
            modifiers = 0;
        } else if (strcmp(wm->config.keybindings[i].keysym, "Tab") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_Tab);
            modifiers = 0;
        } else if (strcmp(wm->config.keybindings[i].keysym, "D") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_d);
            modifiers = 0;
        } else if (strcmp(wm->config.keybindings[i].keysym, "A") == 0) {
            keycode = XKeysymToKeycode(wm->display, XK_a);
            modifiers = 0;
        }

        if (keycode != 0 && ev->keycode == keycode && (ev->state & (Mod1Mask|Mod4Mask|ShiftMask)) == modifiers) {
            if (strcmp(wm->config.keybindings[i].command, "close_window") == 0) {
                if (wm->active_client && wm->active_client->decorated) {
                    XEvent ce;
                    memset(&ce, 0, sizeof(ce));
                    ce.type = ClientMessage;
                    ce.xclient.window = wm->active_client->window;
                    ce.xclient.message_type = wm->wm_protocols;
                    ce.xclient.format = 32;
                    ce.xclient.data.l[0] = wm->wm_delete_window;
                    ce.xclient.data.l[1] = CurrentTime;

                    XSendEvent(wm->display, wm->active_client->window, False, NoEventMask, &ce);
                }
            } else if (strcmp(wm->config.keybindings[i].command, "cycle_windows") == 0) {
                if (wm->num_clients > 0) {
                    int start_index = 0;
                    for (int i = 0; i < wm->num_clients; i++) {
                        if (wm->active_client == wm->clients[i] && wm->clients[i]->decorated) {
                            start_index = i;
                            break;
                        }
                    }

                    int next_index = (start_index + 1) % wm->num_clients;
                    int attempts = 0;

                    while (attempts < wm->num_clients) {
                        if (wm->clients[next_index]->decorated && !wm->clients[next_index]->is_dock &&
                            wm->clients[next_index]->workspace == wm->current_workspace &&
                            wm->clients[next_index]->mapped && !wm->clients[next_index]->minimized) {
                            break;
                        }
                        next_index = (next_index + 1) % wm->num_clients;
                        attempts++;
                    }

                    if (attempts < wm->num_clients) {
                        if (wm->active_client) {
                            wm->active_client->is_active = 0;
                            if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                                draw_frame(wm, wm->active_client);
                            }
                        }

                        wm->active_client = wm->clients[next_index];
                        wm->active_client->is_active = 1;
                        if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                            XRaiseWindow(wm->display, wm->active_client->frame);
                            draw_frame(wm, wm->active_client);
                        }
                        set_input_focus(wm, wm->active_client);
                    }
                }
            } else if (strncmp(wm->config.keybindings[i].command, "workspace_", 10) == 0) {
                int workspace = atoi(wm->config.keybindings[i].command + 10) - 1;
                if (workspace >= 0 && workspace < MAX_WORKSPACES) {
                    switch_workspace(wm, workspace);
                }
            } else if (strcmp(wm->config.keybindings[i].command, "toggle_fullscreen") == 0) {
                if (wm->active_client && wm->active_client->decorated) {
                    toggle_fullscreen(wm, wm->active_client);
                }
            } else if (strcmp(wm->config.keybindings[i].command, "cycle_windows_reverse") == 0) {
                if (wm->num_clients > 0) {
                    int start_index = 0;
                    for (int i = 0; i < wm->num_clients; i++) {
                        if (wm->active_client == wm->clients[i] && wm->clients[i]->decorated) {
                            start_index = i;
                            break;
                        }
                    }

                    int prev_index = (start_index - 1 + wm->num_clients) % wm->num_clients;
                    int attempts = 0;

                    while (attempts < wm->num_clients) {
                        if (wm->clients[prev_index]->decorated && !wm->clients[prev_index]->is_dock &&
                            wm->clients[prev_index]->workspace == wm->current_workspace &&
                            wm->clients[prev_index]->mapped && !wm->clients[prev_index]->minimized) {
                            break;
                        }
                        prev_index = (prev_index - 1 + wm->num_clients) % wm->num_clients;
                        attempts++;
                    }

                    if (attempts < wm->num_clients) {
                        if (wm->active_client) {
                            wm->active_client->is_active = 0;
                            if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                                draw_frame(wm, wm->active_client);
                            }
                        }

                        wm->active_client = wm->clients[prev_index];
                        wm->active_client->is_active = 1;
                        if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                            XRaiseWindow(wm->display, wm->active_client->frame);
                            draw_frame(wm, wm->active_client);
                        }
                        set_input_focus(wm, wm->active_client);
                    }
                }
            } else if (strcmp(wm->config.keybindings[i].command, "minimize_window") == 0) {
                if (wm->active_client && wm->active_client->decorated && !wm->active_client->minimized) {
                    minimize_window(wm, wm->active_client);
                }
            } else if (strcmp(wm->config.keybindings[i].command, "unminimize_window") == 0) {
                for (int i = 0; i < wm->num_clients; i++) {
                    if (wm->clients[i]->workspace == wm->current_workspace && wm->clients[i]->minimized) {
                        unminimize_window(wm, wm->clients[i]);
                        break;
                    }
                }
            } else if (strcmp(wm->config.keybindings[i].command, "minimize_all_toggle") == 0) {
                minimize_all_windows(wm);
            } else if (strncmp(wm->config.keybindings[i].command, "prog_start=", 11) == 0) {
                const char *program = wm->config.keybindings[i].command + 11;
                pid_t pid = fork();
                if (pid == 0) {
                    execlp("sh", "sh", "-c", program, NULL);
                    exit(0);
                }
            }

            keep_docks_on_top(wm);
            return;
        }
    }

    if ((ev->state & Mod4Mask) && ev->keycode == XKeysymToKeycode(wm->display, XK_F4)) {
        if (wm->active_client && wm->active_client->decorated) {
            XEvent ce;
            memset(&ce, 0, sizeof(ce));
            ce.type = ClientMessage;
            ce.xclient.window = wm->active_client->window;
            ce.xclient.message_type = wm->wm_protocols;
            ce.xclient.format = 32;
            ce.xclient.data.l[0] = wm->wm_delete_window;
            ce.xclient.data.l[1] = CurrentTime;

            XSendEvent(wm->display, wm->active_client->window, False, NoEventMask, &ce);
        }
    } else if ((ev->state & Mod1Mask) && ev->keycode == XKeysymToKeycode(wm->display, XK_Tab)) {
        if (wm->num_clients > 0) {
            int start_index = 0;
            for (int i = 0; i < wm->num_clients; i++) {
                if (wm->active_client == wm->clients[i] && wm->clients[i]->decorated) {
                    start_index = i;
                    break;
                }
            }

            int next_index = (start_index + 1) % wm->num_clients;
            int attempts = 0;

            while (attempts < wm->num_clients) {
                if (wm->clients[next_index]->decorated && !wm->clients[next_index]->is_dock &&
                    wm->clients[next_index]->workspace == wm->current_workspace &&
                    wm->clients[next_index]->mapped && !wm->clients[next_index]->minimized) {
                    break;
                }
                next_index = (next_index + 1) % wm->num_clients;
                attempts++;
            }

            if (attempts < wm->num_clients) {
                if (wm->active_client) {
                    wm->active_client->is_active = 0;
                    if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                        draw_frame(wm, wm->active_client);
                    }
                }

                wm->active_client = wm->clients[next_index];
                wm->active_client->is_active = 1;
                if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                    XRaiseWindow(wm->display, wm->active_client->frame);
                    draw_frame(wm, wm->active_client);
                }
                set_input_focus(wm, wm->active_client);
            }
        }
    } else if ((ev->state & Mod1Mask) && ev->keycode >= XKeysymToKeycode(wm->display, XK_1) &&
               ev->keycode <= XKeysymToKeycode(wm->display, XK_6)) {
        int workspace = ev->keycode - XKeysymToKeycode(wm->display, XK_1);
        switch_workspace(wm, workspace);
    } else if (ev->keycode == XKeysymToKeycode(wm->display, XK_F11)) {
        if (wm->active_client && wm->active_client->decorated) {
            toggle_fullscreen(wm, wm->active_client);
        }
    } else if ((ev->state & Mod4Mask) && ev->keycode == XKeysymToKeycode(wm->display, XK_d)) {
        if (wm->active_client && wm->active_client->decorated && !wm->active_client->minimized) {
            minimize_window(wm, wm->active_client);
        }
    } else if ((ev->state & (Mod4Mask | ShiftMask)) == (Mod4Mask | ShiftMask) &&
               ev->keycode == XKeysymToKeycode(wm->display, XK_d)) {
        for (int i = 0; i < wm->num_clients; i++) {
            if (wm->clients[i]->workspace == wm->current_workspace && wm->clients[i]->minimized) {
                unminimize_window(wm, wm->clients[i]);
                break;
            }
        }
    } else if ((ev->state & Mod1Mask) && ev->keycode == XKeysymToKeycode(wm->display, XK_a)) {
        minimize_all_windows(wm);
    }

    keep_docks_on_top(wm);
}

void handle_client_message(AquaWm *wm, XEvent *e) {
    XClientMessageEvent *ev = &e->xclient;

    if (ev->message_type == wm->wm_protocols) {
        if ((Atom)ev->data.l[0] == wm->wm_delete_window) {
            Client *client = find_client_by_window(wm, ev->window);
            if (client) {
                client->ignore_unmap = 1;
                XDestroyWindow(wm->display, client->window);
            }
        } else if ((Atom)ev->data.l[0] == wm->wm_take_focus) {
            Client *client = find_client_by_window(wm, ev->window);
            if (client && !client->minimized) {
                set_input_focus(wm, client);
            }
        }
    } else if (ev->message_type == wm->net_wm_state) {
        Client *client = find_client_by_window(wm, ev->window);
        if (client && client->decorated) {
            for (int i = 1; i < 3; i++) {
                Atom atom = ev->data.l[i];
                if (atom == wm->net_wm_state_maximized_horz ||
                    atom == wm->net_wm_state_maximized_vert) {
                    toggle_maximize(wm, client);
                    break;
                } else if (atom == wm->net_wm_state_fullscreen) {
                    toggle_fullscreen(wm, client);
                    break;
                } else if (atom == wm->net_wm_state_hidden) {
                    if (client->minimized) {
                        unminimize_window(wm, client);
                    } else {
                        minimize_window(wm, client);
                    }
                    break;
                }
            }
        }
    }

    keep_docks_on_top(wm);
}

void handle_destroy_notify(AquaWm *wm, XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    Client *client = find_client_by_window(wm, ev->window);
    if (client) {
        if (client->title) free(client->title);
        free(client);
    }

    keep_docks_on_top(wm);
}

void handle_unmap_notify(AquaWm *wm, XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;

    Client *client = find_client_by_window(wm, ev->window);
    if (client) {
        if (client->ignore_unmap) {
            client->ignore_unmap = 0;
            return;
        }

        if (client->decorated && !client->is_fullscreen && !client->minimized) {
            XUnmapWindow(wm->display, client->frame);
        }
        client->mapped = False;
        update_client_list(wm);
    }

    keep_docks_on_top(wm);
}

void handle_expose(AquaWm *wm, XEvent *e) {
    XExposeEvent *ev = &e->xexpose;

    if (ev->window == wm->root) {
        draw_workspace_indicator(wm);
        return;
    }

    Client *client = find_client_by_frame(wm, ev->window);
    if (client && client->decorated && !client->is_fullscreen && !client->minimized) {
        draw_frame(wm, client);
    }

    keep_docks_on_top(wm);
}

void handle_enter_notify(AquaWm *wm, XEvent *e) {
    XEnterWindowEvent *ev = &e->xcrossing;

    Client *client = find_client_by_frame(wm, ev->window);
    if (client && client->decorated && !client->is_fullscreen && !client->minimized) {
        update_cursor(wm, ev->window, ev->x, ev->y);
        draw_frame(wm, client);
    }
}

void handle_leave_notify(AquaWm *wm, XEvent *e) {
    XLeaveWindowEvent *ev = &e->xcrossing;

    Client *client = find_client_by_frame(wm, ev->window);
    if (client && client->decorated && !client->is_fullscreen && !client->minimized) {
        XDefineCursor(wm->display, ev->window, wm->cursor_normal);
        draw_frame(wm, client);
    }
}

void handle_focus_in(AquaWm *wm, XEvent *e) {
    XFocusInEvent *ev = &e->xfocus;

    Client *client = find_client_by_window(wm, ev->window);
    if (!client) client = find_client_by_frame(wm, ev->window);

    if (client && !client->is_dock && client->workspace == wm->current_workspace && !client->minimized) {
        if (wm->active_client && wm->active_client != client) {
            wm->active_client->is_active = 0;
            if (wm->active_client->decorated && !wm->active_client->is_fullscreen) {
                draw_frame(wm, wm->active_client);
            }
        }

        wm->active_client = client;
        client->is_active = 1;
        if (client->decorated && !client->is_fullscreen) {
            draw_frame(wm, client);
        }
    }
}

void handle_focus_out(AquaWm *wm, XEvent *e) {
    XFocusOutEvent *ev = &e->xfocus;

    if (ev->mode == NotifyGrab || ev->mode == NotifyUngrab) {
        return;
    }

    Client *client = find_client_by_window(wm, ev->window);
    if (!client) client = find_client_by_frame(wm, ev->window);

    if (client && wm->active_client == client) {
        client->is_active = 0;
        if (client->decorated && !client->is_fullscreen) {
            draw_frame(wm, client);
        }
        wm->active_client = NULL;
    }
}

void handle_property_notify(AquaWm *wm, XEvent *e) {
    XPropertyEvent *ev = &e->xproperty;

    Client *client = find_client_by_window(wm, ev->window);
    if (client) {
        if (ev->atom == XA_WM_NAME || ev->atom == wm->net_wm_name) {
            if (client->title) free(client->title);
            client->title = get_window_title(wm, client->window);
            if (client->decorated && !client->is_fullscreen && !client->minimized) {
                draw_frame(wm, client);
            }
        } else if (ev->atom == wm->net_wm_state) {
            Atom type;
            int format;
            unsigned long nitems, bytes_after;
            unsigned char *data = NULL;

            if (XGetWindowProperty(wm->display, client->window, wm->net_wm_state,
                                  0, 10, False, XA_ATOM, &type, &format,
                                  &nitems, &bytes_after, &data) == Success && data) {
                Atom *atoms = (Atom*)data;
                client->is_maximized = 0;
                client->is_fullscreen = 0;
                client->urgent = 0;

                for (unsigned long i = 0; i < nitems; i++) {
                    if (atoms[i] == wm->net_wm_state_maximized_horz ||
                        atoms[i] == wm->net_wm_state_maximized_vert) {
                        client->is_maximized = 1;
                    } else if (atoms[i] == wm->net_wm_state_fullscreen) {
                        client->is_fullscreen = 1;
                    } else if (atoms[i] == wm->net_wm_state_demands_attention) {
                        client->urgent = 1;
                    } else if (atoms[i] == wm->net_wm_state_hidden) {
                        client->minimized = 1;
                    }
                }

                XFree(data);

                if (client->is_fullscreen && wm->active_client == client) {
                    apply_window_state(wm, client);
                }
            }
        } else if (ev->atom == XInternAtom(wm->display, "_GTK_FRAME_EXTENTS", False)) {
            update_gtk_extents(wm, client);
            force_client_position(wm, client);
        }
    }
}

int send_command_to_wm(const char* command) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SOCKET_PORT);

    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock_fd);
        return -1;
    }

    send(sock_fd, command, strlen(command), 0);
    close(sock_fd);

    return 0;
}

void handle_reload(AquaWm *wm) {
    if (atomic_load(&wm->reload_requested)) {
        reload_config(wm);
        atomic_store(&wm->reload_requested, 0);
    }
}

void aquawm_run(AquaWm *wm) {
    XEvent ev;

    keep_docks_on_top(wm);

    handle_reload(wm);

    while (!wm->should_exit) {
        int events_processed = 0;
        while (XPending(wm->display) > 0 && events_processed < 100) {
            XNextEvent(wm->display, &ev);
            events_processed++;

            switch (ev.type) {
                case MapRequest:
                    handle_map_request(wm, &ev);
                    break;
                case MapNotify:
                    handle_map_notify(wm, &ev);
                    break;
                case ConfigureRequest:
                    handle_configure_request(wm, &ev);
                    break;
                case ConfigureNotify:
                    handle_configure_notify(wm, &ev);
                    break;
                case ButtonPress:
                    handle_button_press(wm, &ev);
                    break;
                case MotionNotify:
                    handle_motion_notify(wm, &ev);
                    break;
                case ButtonRelease:
                    handle_button_release(wm, &ev);
                    break;
                case KeyPress:
                    handle_key_press(wm, &ev);
                    break;
                case ClientMessage:
                    handle_client_message(wm, &ev);
                    break;
                case DestroyNotify:
                    handle_destroy_notify(wm, &ev);
                    break;
                case UnmapNotify:
                    handle_unmap_notify(wm, &ev);
                    break;
                case Expose:
                    handle_expose(wm, &ev);
                    break;
                case EnterNotify:
                    handle_enter_notify(wm, &ev);
                    break;
                case LeaveNotify:
                    handle_leave_notify(wm, &ev);
                    break;
                case FocusIn:
                    handle_focus_in(wm, &ev);
                    break;
                case FocusOut:
                    handle_focus_out(wm, &ev);
                    break;
                case PropertyNotify:
                    handle_property_notify(wm, &ev);
                    break;
                default:
                    break;
            }
        }

        for (int i = 0; i < wm->num_clients; i++) {
            if (wm->clients[i]->decorated && !wm->clients[i]->is_dock &&
                wm->clients[i]->mapped && wm->clients[i]->workspace == wm->current_workspace && !wm->clients[i]->minimized) {
                enforce_frame_integrity(wm, wm->clients[i]);
                lock_client_to_frame(wm, wm->clients[i]);
            }
        }

        enforce_dock_boundaries(wm);
        handle_reload(wm);

        XFlush(wm->display);
        struct timespec ts = {0, 5000000};
        nanosleep(&ts, NULL);
    }
}

void aquawm_cleanup(AquaWm *wm) {
    if (!wm) return;

    atomic_store(&wm->socket_active, 0);
    if (wm->socket_fd >= 0) {
        shutdown(wm->socket_fd, SHUT_RDWR);
        close(wm->socket_fd);
        pthread_join(wm->socket_thread, NULL);
    }

    for (int i = 0; i < wm->num_clients; i++) {
        if (wm->clients[i]->title) free(wm->clients[i]->title);
        free(wm->clients[i]);
    }

    if (wm->frame_gc) XFreeGC(wm->display, wm->frame_gc);
    if (wm->button_gc) XFreeGC(wm->display, wm->button_gc);
    if (wm->border_gc) XFreeGC(wm->display, wm->border_gc);
    if (wm->desktop_gc) XFreeGC(wm->display, wm->desktop_gc);
    if (wm->highlight_gc) XFreeGC(wm->display, wm->highlight_gc);
    if (wm->text_gc) XFreeGC(wm->display, wm->text_gc);
    if (wm->titlebar_gc) XFreeGC(wm->display, wm->titlebar_gc);
    if (wm->shadow_gc) XFreeGC(wm->display, wm->shadow_gc);
    if (wm->button_bg_gc) XFreeGC(wm->display, wm->button_bg_gc);
    if (wm->button_fg_gc) XFreeGC(wm->display, wm->button_fg_gc);

    if (wm->desktop_pixmap != None) XFreePixmap(wm->display, wm->desktop_pixmap);

    if (wm->wm_window != None) XDestroyWindow(wm->display, wm->wm_window);

    if (wm->font) {
        XFreeFont(wm->display, wm->font);
    }

    XCloseDisplay(wm->display);
    free(wm);
}

void sigint_handler(int sig) {
    (void)sig;
    exit(0);
}

void print_usage(void) {
    printf("AquaWM - X11 Window Manager\n");
    printf("Usage:\n");
    printf("  aquawm              - Start the window manager\n");
    printf("  aquawm -r           - Reload configuration of running WM\n");
    printf("  aquawm -h           - Show this help\n");
    printf("  aquawm -c <command> - Send command to running WM\n");
    printf("\nCommands:\n");
    printf("  reload              - Reload configuration\n");
    printf("  list                - List active windows\n");
    printf("  workspace <n>       - Switch to workspace n (1-6)\n");
    printf("  minimize_all        - Minimize all windows\n");
    printf("  unminimize_all      - Unminimize all windows\n");
    printf("  minimize_all_toggle - Toggle minimize all windows\n");
    printf("  prog_start=<program>- Start a program\n");
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    signal(SIGSEGV, sigint_handler);

    if (argc > 1) {
        if (strcmp(argv[1], "-r") == 0) {
            if (send_command_to_wm("reload") == 0) {
                printf("Reload command sent successfully\n");
            } else {
                printf("Failed to send reload command\n");
                return 1;
            }
            return 0;
        } else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[1], "-c") == 0 && argc > 2) {
            if (send_command_to_wm(argv[2]) == 0) {
                printf("Command sent successfully\n");
            } else {
                printf("Failed to send command\n");
                return 1;
            }
            return 0;
        }
    }

    AquaWm *wm = aquawm_init(1);
    if (!wm) {
        fprintf(stderr, "Failed to initialize WM\n");
        return 1;
    }

    aquawm_run(wm);
    aquawm_cleanup(wm);

    return 0;
}
