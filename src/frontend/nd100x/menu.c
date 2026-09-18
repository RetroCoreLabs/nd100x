/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <ncurses.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

#include "nd100x_types.h"
#include "nd100x_protos.h"
#include "keyboard.h"
#include "../../machine/machine_types.h"
#include "../../machine/machine_protos.h"
#include "download.h"

// URL constants
#define FLOPPIES_JSON_URL "https://ndlib.hackercorp.no/floppies.json"
#define IMAGES_BASE_URL "https://ndlib.hackercorp.no/images/"

// Cache constants
#define CACHE_DIR_SUFFIX "/.cache/nd100x"
#define CACHE_FILE_NAME "floppies.json"
// Time conversion constants
#define SECONDS_PER_DAY  86400
#define SECONDS_PER_HOUR 3600

#define CACHE_MAX_AGE_SECONDS (7 * SECONDS_PER_DAY)  // 7 days

/*
 * Window layout constants.
 *
 * The screen is divided vertically into three bands:
 *   Row 0..2                  : Search box    (SEARCH_WIN_HEIGHT rows)
 *   Row 3..max_y-5            : Content area  (list + detail side by side)
 *   Row max_y-4..max_y-1      : Toolbar       (TOOLBAR_WIN_HEIGHT rows)
 *
 * Content area height = max_y - SEARCH_WIN_HEIGHT - TOOLBAR_WIN_HEIGHT
 */
#define SEARCH_WIN_HEIGHT        3
#define TOOLBAR_WIN_HEIGHT       4
#define TOOLBAR_TEXT_LEFT_MARGIN  2
#define TOOLBAR_TEXT_RIGHT_MARGIN 2

// URL builder function for downloading image files
static char* build_image_url(const char* md5_hash) {
    if (!md5_hash) return NULL;

    // Calculate required buffer size: base URL + md5 + ".img" + null terminator
    size_t base_len = strlen(IMAGES_BASE_URL);
    size_t md5_len = strlen(md5_hash);
    size_t total_len = base_len + md5_len + 4 + 1; // +4 for ".img", +1 for null terminator

    char* url = malloc(total_len);
    if (!url) return NULL;

    // Build the complete URL: base + md5 + .img
    snprintf(url, total_len, "%s%s.img", IMAGES_BASE_URL, md5_hash);

    return url;
}

// Forward declarations for popup functions
static void show_mount_popup(void);
static void hide_mount_popup(void);
static void draw_mount_popup(void);
static void handle_mount_popup_input(int ch);
static void show_unmount_popup(void);
static void hide_unmount_popup(void);
static void draw_unmount_popup(void);
static void handle_unmount_popup_input(int ch);

// Floppy disk structure
typedef struct {
    int id;
    char name[256];
    char description[1024];
    char reference[256];
    char md5[33];
    char *directory_content;  // Dynamically allocated based on content length
    char product[256];
    DRIVE_TYPE drive_type;  // FLOPPY or SMD based on filesystem image size
} FloppyDisk_t;

// Menu state structure
// Page structure for directory content
typedef struct {
    int start_line;     // First line number (0-based)
    int end_line;       // Last line number (0-based)
    int scroll_y;       // Scroll position for this page
    int lines_count;    // Number of lines on this page
} DirectoryPage_t;

// Mount popup window structure
typedef struct {
    WINDOW *popup_win;
    int selected_unit;  // 0-1 for floppy, 0-3 for SMD
    bool visible;
    FloppyDisk_t *floppy;  // Pointer to the floppy to mount
} MountPopup_t;

typedef struct {
    FloppyDisk_t *floppies;
    int floppy_count;
    int selected_index;
    char search_text[256];
    int search_cursor;
    int *filtered_indices;  // Array of indices for filtered results
    int filtered_count;     // Number of filtered results
    int detail_scroll_x;    // Horizontal scroll position for details window
    int detail_scroll_y;    // Vertical scroll position for details window
    int detail_page_size;   // Number of lines that fit in details window
    int detail_total_lines; // Total number of lines in directory content
    DirectoryPage_t *directory_pages;  // Array of pages for current floppy
    int directory_page_count;          // Number of pages
    int current_page;                  // Current page index (0-based)
    WINDOW *search_win;
    WINDOW *list_win;
    WINDOW *detail_win;
    WINDOW *toolbar_win;
    int max_y, max_x;
    MountPopup_t mount_popup;  // Mount popup window
    MountPopup_t unmount_popup;  // Unmount popup window
} MenuState_t;

static MenuState_t menu_state;

// Helper function to detect drive type based on filesystem image size
static DRIVE_TYPE detect_drive_type(const char *directory_content) {
    if (!directory_content) return DRIVE_FLOPPY;  // Default to floppy

    char *content_copy = strdup(directory_content);
    if (!content_copy) return DRIVE_FLOPPY;

    char *line = strtok(content_copy, "\r\n");
    while (line) {
        // Look for "Filesystem image size" line
        if (strstr(line, "Filesystem image size")) {
            // Extract the octal number
            char *colon = strchr(line, ':');
            if (colon) {
                colon++; // Skip the colon
                // Skip whitespace
                while (*colon == ' ' || *colon == '\t') colon++;

                // Find "pages"
                char *pages = strstr(colon, "pages");
                if (pages) {
                    // Extract the number
                    char number_str[32];
                    int len = pages - colon;
                    if (len > 0 && len < sizeof(number_str)) {
                        strncpy(number_str, colon, len);
                        number_str[len] = '\0';

                        // Convert octal string to integer
                        int pages_count = strtol(number_str, NULL, 8);

                        free(content_copy);
                        return (pages_count > 1000) ? DRIVE_SMD : DRIVE_FLOPPY;
                    }
                }
            }
        }
        line = strtok(NULL, "\r\n");
    }

    free(content_copy);
    return DRIVE_FLOPPY;  // Default to floppy if not found
}

// Helper function to count total lines in directory content
static int count_directory_lines(const char *content) {
    if (!content) return 0;

    int line_count = 0;
    char *content_copy = strdup(content);  // Dynamic allocation
    if (!content_copy) return 0;

    char *line = strtok(content_copy, "\r\n");
    while (line) {
        line_count++;
        line = strtok(NULL, "\r\n");
    }

    free(content_copy);  // Free the dynamically allocated copy
    return line_count;
}

// Free directory pages array
static void free_directory_pages(void) {
    if (menu_state.directory_pages) {
        free(menu_state.directory_pages);
        menu_state.directory_pages = NULL;
        menu_state.directory_page_count = 0;
        menu_state.current_page = 0;
    }
}

// Build directory pages array for current floppy
static void build_directory_pages(FloppyDisk_t *floppy) {
    // Free existing pages
    free_directory_pages();

    if (!floppy) return;

    // Count total lines
    int total_lines = count_directory_lines(floppy->directory_content);
    menu_state.detail_total_lines = total_lines;

    if (total_lines == 0) return;

    // Get window dimensions to calculate page size
    int win_height, win_width;
    getmaxyx(menu_state.detail_win, win_height, win_width);

    // Calculate available space for directory content
    // Reserve space for: Details title (1) + Name (1) + Description (1) + Reference (1) + MD5 (1) + empty line (1) + Directory Content header (1) + page info (1) = 7 lines
    int reserved_lines = 7;
    int max_display_lines = win_height - reserved_lines;

    // Ensure we have at least some space for content
    if (max_display_lines <= 0) {
        max_display_lines = win_height - 1; // Just reserve 1 line for page info
        if (max_display_lines <= 0) {
            max_display_lines = 1; // Absolute minimum
        }
    }

    // Calculate number of pages
    int page_count = (total_lines + max_display_lines - 1) / max_display_lines;

    // Allocate pages array
    menu_state.directory_pages = malloc(page_count * sizeof(DirectoryPage_t));
    if (!menu_state.directory_pages) return;

    menu_state.directory_page_count = page_count;
    menu_state.current_page = 0;

    // Build each page
    for (int i = 0; i < page_count; i++) {
        DirectoryPage_t *page = &menu_state.directory_pages[i];
        page->start_line = i * max_display_lines;
        page->end_line = (i + 1) * max_display_lines - 1;
        if (page->end_line >= total_lines) {
            page->end_line = total_lines - 1;
        }
        page->scroll_y = page->start_line;
        page->lines_count = page->end_line - page->start_line + 1;
    }

    // Update current scroll position
    menu_state.detail_scroll_y = menu_state.directory_pages[0].scroll_y;
    menu_state.detail_page_size = max_display_lines;
}





// Build the cache file path: $HOME/.cache/nd100x/floppies.json
// Returns a malloc'd string or NULL on failure.
static char *build_cache_path(void) {
    const char *home = getenv("HOME");
    if (!home) return NULL;

    size_t len = strlen(home) + strlen(CACHE_DIR_SUFFIX) + 1 + strlen(CACHE_FILE_NAME) + 1;
    char *path = malloc(len);
    if (!path) return NULL;
    snprintf(path, len, "%s%s/%s", home, CACHE_DIR_SUFFIX, CACHE_FILE_NAME);
    return path;
}

// Ensure the cache directory exists (mkdir -p equivalent for one level).
static bool ensure_cache_dir(void) {
    const char *home = getenv("HOME");
    if (!home) return false;

    // Create $HOME/.cache if needed
    char dir1[512];
    snprintf(dir1, sizeof(dir1), "%s/.cache", home);
    mkdir(dir1, 0755);  // ignore EEXIST

    // Create $HOME/.cache/nd100x
    char dir2[512];
    snprintf(dir2, sizeof(dir2), "%s%s", home, CACHE_DIR_SUFFIX);
    if (mkdir(dir2, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

// Write JSON data to the cache file.
static void save_to_cache(const char *json_data) {
    if (!json_data) return;
    if (!ensure_cache_dir()) return;

    char *path = build_cache_path();
    if (!path) return;

    FILE *f = fopen(path, "w");
    if (f) {
        fputs(json_data, f);
        fclose(f);
    }
    free(path);
}

// Read the cache file contents into a malloc'd string. Returns NULL if missing.
static char *load_from_cache(void) {
    char *path = build_cache_path();
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    free(path);
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char *data = malloc(size + 1);
    if (!data) { fclose(f); return NULL; }

    size_t read_bytes = fread(data, 1, size, f);
    fclose(f);
    data[read_bytes] = '\0';
    return data;
}

// Return the age of the cache file in seconds, or -1 if it doesn't exist.
static long cache_age_seconds(void) {
    char *path = build_cache_path();
    if (!path) return -1;

    struct stat st;
    int rc = stat(path, &st);
    free(path);
    if (rc != 0) return -1;

    return (long)(time(NULL) - st.st_mtime);
}

// Get the floppy catalog JSON, using cache when available.
// If force_download is true, the cache is bypassed.
// Sets *from_cache to true if data came from cache, false if downloaded.
static char *get_cached_or_download_json(bool force_download, bool *from_cache) {
    if (from_cache) *from_cache = false;

    if (!force_download) {
        long age = cache_age_seconds();
        if (age >= 0 && age < CACHE_MAX_AGE_SECONDS) {
            char *cached = load_from_cache();
            if (cached) {
                if (from_cache) *from_cache = true;
                return cached;
            }
        }
    }

    // Download from network
    char *json_data = download_file(FLOPPIES_JSON_URL);
    if (json_data) {
        save_to_cache(json_data);
        return json_data;
    }

    // Download failed - fall back to stale cache if one exists
    char *stale = load_from_cache();
    if (stale) {
        if (from_cache) *from_cache = true;
        return stale;
    }

    return NULL;
}

// Parse JSON and populate floppy list
static bool parse_floppies_json(const char* json_data) {
    cJSON *json = cJSON_Parse(json_data);
    if (!json) {
        return false;
    }

    if (!cJSON_IsArray(json)) {
        cJSON_Delete(json);
        return false;
    }

    int array_size = cJSON_GetArraySize(json);
    menu_state.floppies = malloc(array_size * sizeof(FloppyDisk_t));
    if (!menu_state.floppies) {
        cJSON_Delete(json);
        return false;
    }

    // Initialize all directory_content pointers to NULL
    for (int i = 0; i < array_size; i++) {
        menu_state.floppies[i].directory_content = NULL;
    }

    menu_state.floppy_count = 0;
    int skipped_count = 0;

    for (int i = 0; i < array_size; i++) {
        cJSON *item = cJSON_GetArrayItem(json, i);
        if (!cJSON_IsObject(item)) continue;

        FloppyDisk_t *floppy = &menu_state.floppies[menu_state.floppy_count];

        // Parse JSON fields
        cJSON *id = cJSON_GetObjectItem(item, "Id");
        cJSON *name = cJSON_GetObjectItem(item, "Name");
        cJSON *desc = cJSON_GetObjectItem(item, "Description");
        cJSON *ref = cJSON_GetObjectItem(item, "Reference");
        cJSON *md5 = cJSON_GetObjectItem(item, "Md5");
        cJSON *dir_content = cJSON_GetObjectItem(item, "DirectoryContent");
        cJSON *product = cJSON_GetObjectItem(item, "Product");
        cJSON *status = cJSON_GetObjectItem(item, "Status");

        // Only include records with Status = 0
        if (status && status->valueint != 0) {
            skipped_count++;
            continue; // Skip this record
        }

        floppy->id = id ? id->valueint : 0;

        // Safe string copying with NULL checks
        const char *name_str = (name && name->valuestring) ? name->valuestring : "";
        const char *desc_str = (desc && desc->valuestring) ? desc->valuestring : "";
        const char *ref_str = (ref && ref->valuestring) ? ref->valuestring : "";
        const char *md5_str = (md5 && md5->valuestring) ? md5->valuestring : "";
        const char *dir_content_str = (dir_content && dir_content->valuestring) ? dir_content->valuestring : "";
        const char *product_str = (product && product->valuestring) ? product->valuestring : "";

        strncpy(floppy->name, name_str, sizeof(floppy->name) - 1);
        floppy->name[sizeof(floppy->name) - 1] = '\0';

        strncpy(floppy->description, desc_str, sizeof(floppy->description) - 1);
        floppy->description[sizeof(floppy->description) - 1] = '\0';

        strncpy(floppy->reference, ref_str, sizeof(floppy->reference) - 1);
        floppy->reference[sizeof(floppy->reference) - 1] = '\0';

        strncpy(floppy->md5, md5_str, sizeof(floppy->md5) - 1);
        floppy->md5[sizeof(floppy->md5) - 1] = '\0';

        // Dynamically allocate directory content based on actual length
        if (dir_content_str && strlen(dir_content_str) > 0) {
            floppy->directory_content = strdup(dir_content_str);
        } else {
            floppy->directory_content = strdup("");
        }

        // Check if strdup failed
        if (!floppy->directory_content) {
            // Clean up any previously allocated directory_content
            for (int j = 0; j < menu_state.floppy_count; j++) {
                if (menu_state.floppies[j].directory_content) {
                    free(menu_state.floppies[j].directory_content);
                    menu_state.floppies[j].directory_content = NULL;
                }
            }
            free(menu_state.floppies);
            cJSON_Delete(json);
            return false;
        }

        strncpy(floppy->product, product_str, sizeof(floppy->product) - 1);
        floppy->product[sizeof(floppy->product) - 1] = '\0';

        // Detect drive type based on filesystem image size
        floppy->drive_type = detect_drive_type(floppy->directory_content);

        menu_state.floppy_count++;
    }

    cJSON_Delete(json);

    return true;
}

/*
 * init_curses() - Set up ncurses and create the window layout.
 *
 * Creates four windows tiled vertically (see layout constants):
 *   search_win  - Search input box at top
 *   list_win    - Scrollable floppy disk list (left 1/3)
 *   detail_win  - Scrollable detail/directory view (right 2/3)
 *   toolbar_win - Key bindings and cache status at bottom
 */
static void init_curses(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    getmaxyx(stdscr, menu_state.max_y, menu_state.max_x);

    int content_height = menu_state.max_y - SEARCH_WIN_HEIGHT - TOOLBAR_WIN_HEIGHT;

    // Create windows
    menu_state.search_win = newwin(SEARCH_WIN_HEIGHT, menu_state.max_x, 0, 0);
    menu_state.list_win = newwin(content_height, menu_state.max_x / 3, SEARCH_WIN_HEIGHT, 0);
    menu_state.detail_win = newwin(content_height, (menu_state.max_x * 2) / 3, SEARCH_WIN_HEIGHT, menu_state.max_x / 3);
    menu_state.toolbar_win = newwin(TOOLBAR_WIN_HEIGHT, menu_state.max_x, menu_state.max_y - TOOLBAR_WIN_HEIGHT, 0);

    // Enable scrolling for list and detail windows
    scrollok(menu_state.list_win, TRUE);
    scrollok(menu_state.detail_win, TRUE);

    // Set up colors if available
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);   // Selected item
        init_pair(2, COLOR_YELLOW, COLOR_BLACK); // Highlight
        init_pair(3, COLOR_GREEN, COLOR_BLACK);  // Success
        init_pair(4, COLOR_RED, COLOR_BLACK);    // Error
    }

    // Clear screen and refresh to ensure proper initialization
    clear();
    refresh();
}

// Clean up curses
static void cleanup_curses(void) {
    delwin(menu_state.toolbar_win);
    delwin(menu_state.detail_win);
    delwin(menu_state.list_win);
    delwin(menu_state.search_win);
    endwin();
}

// Draw the search box
static void draw_search_box(void) {
    werase(menu_state.search_win);
    box(menu_state.search_win, 0, 0);
    mvwprintw(menu_state.search_win, 0, 2, " Search for name, reference or directory content ");

    // Draw search text
    mvwprintw(menu_state.search_win, 1, 2, "%s", menu_state.search_text);

    wnoutrefresh(menu_state.search_win);
}

// Draw the floppy list
static void draw_floppy_list(void) {
    werase(menu_state.list_win);
    box(menu_state.list_win, 0, 0);

    // Show filter status in title
    if (menu_state.filtered_count > 0) {
        mvwprintw(menu_state.list_win, 0, 2, " Floppy Disks (Filtered: %d/%d) ",
                  menu_state.filtered_count, menu_state.floppy_count);
    } else {
        mvwprintw(menu_state.list_win, 0, 2, " Floppy Disks (%d total) ", menu_state.floppy_count);
    }

    int start_y = 1;
    // Visible items = content height minus 2 for list window border
    int max_items = menu_state.max_y - SEARCH_WIN_HEIGHT - TOOLBAR_WIN_HEIGHT - 2;
    int start_item = 0;

    // Determine which list to show (filtered or all)
    int total_items = (menu_state.filtered_count > 0) ? menu_state.filtered_count : menu_state.floppy_count;

    if (menu_state.selected_index >= max_items) {
        start_item = menu_state.selected_index - max_items + 1;
    }

    for (int i = 0; i < max_items && (i + start_item) < total_items; i++) {
        int item_idx = i + start_item;

        // Get the actual floppy index (filtered or direct)
        int floppy_idx = (menu_state.filtered_count > 0) ?
                        menu_state.filtered_indices[item_idx] : item_idx;

        FloppyDisk_t *floppy = &menu_state.floppies[floppy_idx];

        if (item_idx == menu_state.selected_index) {
            wattron(menu_state.list_win, A_REVERSE);
        }

        // Truncate name if too long
        char display_name[menu_state.max_x / 3 - 4];
        strncpy(display_name, floppy->name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';

        mvwprintw(menu_state.list_win, start_y + i, 1, "%s", display_name);

        if (item_idx == menu_state.selected_index) {
            wattroff(menu_state.list_win, A_REVERSE);
        }
    }

    wnoutrefresh(menu_state.list_win);
}

// Helper function to safely print text within window bounds
static void safe_print_line(WINDOW *win, int y, int x, const char *text, int max_width) {
    if (!text || !win) return;

    int text_len = strlen(text);
    int available_width = max_width - x;

    // Apply horizontal scroll offset
    int scroll_offset = menu_state.detail_scroll_x;
    const char *display_text = text;

    // If we have horizontal scroll, start from the scroll offset
    if (scroll_offset > 0) {
        if (scroll_offset < text_len) {
            display_text = text + scroll_offset;
            text_len = strlen(display_text);
        } else {
            // Scroll offset is beyond text length, show empty or minimal content
            display_text = "";
            text_len = 0;
        }
    }

    if (text_len <= available_width) {
        // Text fits, print it directly
        mvwprintw(win, y, x, "%s", display_text);
    } else {
        // Text is too long, truncate and add ellipsis
        char truncated[available_width + 4];
        strncpy(truncated, display_text, available_width - 3);
        truncated[available_width - 3] = '\0';
        strcat(truncated, "...");
        mvwprintw(win, y, x, "%s", truncated);
    }
}

// Helper function to safely print text within window bounds (no horizontal scroll)
static void safe_print_line_no_scroll(WINDOW *win, int y, int x, const char *text, int max_width) {
    if (!text || !win) return;

    int text_len = strlen(text);
    int available_width = max_width - x;

    if (text_len <= available_width) {
        // Text fits, print it directly
        mvwprintw(win, y, x, "%s", text);
    } else {
        // Text is too long, truncate and add ellipsis
        char truncated[available_width + 4];
        strncpy(truncated, text, available_width - 3);
        truncated[available_width - 3] = '\0';
        strcat(truncated, "...");
        mvwprintw(win, y, x, "%s", truncated);
    }
}

// Helper function to print wrapped text
static int print_wrapped_text(WINDOW *win, int start_y, int x, const char *text, int max_width, int max_lines) {
    if (!text || !win) return start_y;

    int y = start_y;
    int text_len = strlen(text);
    int available_width = max_width - x;
    int display_lines_used = 0;

    // If text is short enough, print it directly
    if (text_len <= available_width) {
        mvwprintw(win, y, x, "%s", text);
        return y + 1;
    }

    // Split text into words and wrap
    char *text_copy = strdup(text);
    char *word = strtok(text_copy, " \t");

    char current_line[available_width + 1];
    current_line[0] = '\0';

    while (word && display_lines_used < max_lines) {
        int word_len = strlen(word);

        // If adding this word would exceed the line width
        if (strlen(current_line) + word_len + 1 > available_width) {
            // Print current line
            if (strlen(current_line) > 0) {
                mvwprintw(win, y, x, "%s", current_line);
                y++;
                display_lines_used++;
            }

            // Start new line with current word
            strcpy(current_line, word);
        } else {
            // Add word to current line
            if (strlen(current_line) > 0) {
                strcat(current_line, " ");
            }
            strcat(current_line, word);
        }

        word = strtok(NULL, " \t");
    }

    // Print the last line if there's content and we haven't exceeded the limit
    if (strlen(current_line) > 0 && display_lines_used < max_lines) {
        mvwprintw(win, y, x, "%s", current_line);
        y++;
    }

    free(text_copy);
    return y;
}

// Draw floppy details
static void draw_floppy_details(void) {
    werase(menu_state.detail_win);
    box(menu_state.detail_win, 0, 0);
    mvwprintw(menu_state.detail_win, 0, 2, " Details ");

    // Get window dimensions
    int win_height, win_width;
    getmaxyx(menu_state.detail_win, win_height, win_width);
    int max_width = win_width - 4; // Leave 2 chars margin on each side

    // Get the actual floppy index (filtered or direct)
    int total_items = (menu_state.filtered_count > 0) ? menu_state.filtered_count : menu_state.floppy_count;

    if (menu_state.selected_index >= 0 && menu_state.selected_index < total_items) {
        int floppy_idx = (menu_state.filtered_count > 0) ?
                        menu_state.filtered_indices[menu_state.selected_index] : menu_state.selected_index;
        FloppyDisk_t *floppy = &menu_state.floppies[floppy_idx];

        int y = 1;

        // Print basic info with safe printing (no horizontal scroll for header)
        char name_line[512];
        snprintf(name_line, sizeof(name_line), "Name: %s", floppy->name);
        safe_print_line_no_scroll(menu_state.detail_win, y++, 2, name_line, max_width);

        char desc_line[2048];
        snprintf(desc_line, sizeof(desc_line), "Description: %s", floppy->description);
        safe_print_line_no_scroll(menu_state.detail_win, y++, 2, desc_line, max_width);

        char ref_line[512];
        snprintf(ref_line, sizeof(ref_line), "Reference: %s", floppy->reference);
        safe_print_line_no_scroll(menu_state.detail_win, y++, 2, ref_line, max_width);

        char md5_line[512];
        snprintf(md5_line, sizeof(md5_line), "MD5: %s", floppy->md5);
        safe_print_line_no_scroll(menu_state.detail_win, y++, 2, md5_line, max_width);
/*
        char product_line[512];
        snprintf(product_line, sizeof(product_line), "Product: %s", floppy->product);
        safe_print_line(menu_state.detail_win, y++, 2, product_line, max_width);
*/
        y++;
        mvwprintw(menu_state.detail_win, y++, 2, "__________________ Directory Content __________________");

        // Handle directory content with vertical scrolling using page array
        if (!menu_state.directory_pages) {
            build_directory_pages(floppy);
        }

        int lines_printed = 0;

        if (menu_state.directory_pages && menu_state.current_page < menu_state.directory_page_count) {
            DirectoryPage_t *current_page = &menu_state.directory_pages[menu_state.current_page];

            // Create a copy to avoid destroying the original string with strtok
            char *content_copy = strdup(floppy->directory_content);
            if (!content_copy) return;

            char *line = strtok(content_copy, "\r\n");
            int current_line = 0;

            // Skip lines to reach the start of current page
            while (line && current_line < current_page->start_line) {
                line = strtok(NULL, "\r\n");
                current_line++;
            }

            // Display lines for current page
            while (line && current_line <= current_page->end_line && y < win_height - 2) {
                // Use safe printing to handle long lines
                safe_print_line(menu_state.detail_win, y, 2, line, max_width);
                y++;
                lines_printed++;
                line = strtok(NULL, "\r\n");
                current_line++;
            }

            // Update scroll position to match current page
            menu_state.detail_scroll_y = current_page->scroll_y;
            menu_state.detail_page_size = current_page->lines_count;

            free(content_copy);  // Free the dynamically allocated copy
        }



        // Show page information
        if (menu_state.directory_pages && menu_state.current_page < menu_state.directory_page_count) {
            DirectoryPage_t *current_page = &menu_state.directory_pages[menu_state.current_page];
            char page_info[128];
            snprintf(page_info, sizeof(page_info), "Page %d/%d (Lines %d-%d of %d)",
                    menu_state.current_page + 1, menu_state.directory_page_count,
                    current_page->start_line + 1, current_page->end_line + 1,
                    menu_state.detail_total_lines);
            mvwprintw(menu_state.detail_win, win_height - 1, 2, "%s", page_info);
        }
    }

    wnoutrefresh(menu_state.detail_win);
}

/*
 * draw_toolbar() - Render the keybinding toolbar.
 *
 * The toolbar window has TOOLBAR_WIN_HEIGHT rows (including border).
 * Row 1: Navigation keys.  Row 2: Action keys + right-aligned cache age.
 */
static void draw_toolbar(void) {
    werase(menu_state.toolbar_win);
    box(menu_state.toolbar_win, 0, 0);

    // Show cache age indicator
    long age = cache_age_seconds();
    char cache_info[64] = "";
    if (age >= 0) {
        int days = age / SECONDS_PER_DAY;
        int hours = (age % SECONDS_PER_DAY) / SECONDS_PER_HOUR;
        if (days > 0)
            snprintf(cache_info, sizeof(cache_info), "[cached %dd %dh ago]", days, hours);
        else if (hours > 0)
            snprintf(cache_info, sizeof(cache_info), "[cached %dh ago]", hours);
        else
            snprintf(cache_info, sizeof(cache_info), "[cached <1h ago]");
    }

    static const char nav_keys[] = "ESC=Exit  ENTER=Search  UP/DN=Navigate  PgUp/PgDn=Scroll  LEFT/RIGHT=Scroll";
    static const char action_keys[] = "F5=Mount  F6=Unmount  F7=Refresh";

    mvwprintw(menu_state.toolbar_win, 1, TOOLBAR_TEXT_LEFT_MARGIN, "%s", nav_keys);
    mvwprintw(menu_state.toolbar_win, 2, TOOLBAR_TEXT_LEFT_MARGIN, "%s", action_keys);
    if (cache_info[0]) {
        int len = strlen(cache_info);
        int x = menu_state.max_x - len - TOOLBAR_TEXT_RIGHT_MARGIN;
        if (x > (int)sizeof(action_keys) + TOOLBAR_TEXT_LEFT_MARGIN)
            mvwprintw(menu_state.toolbar_win, 2, x, "%s", cache_info);
    }
    wnoutrefresh(menu_state.toolbar_win);
}

// Filter floppies based on search text
static void filter_floppies(void) {
    // Free existing filtered indices
    if (menu_state.filtered_indices) {
        free(menu_state.filtered_indices);
        menu_state.filtered_indices = NULL;
    }

    if (strlen(menu_state.search_text) == 0) {
        // No filter - show all floppies
        menu_state.filtered_count = 0;
        menu_state.selected_index = 0;
        return;
    }

    // Allocate space for filtered indices (worst case: all floppies match)
    menu_state.filtered_indices = malloc(menu_state.floppy_count * sizeof(int));
    if (!menu_state.filtered_indices) {
        menu_state.filtered_count = 0;
        return;
    }

    menu_state.filtered_count = 0;

    // Search through all floppies
    for (int i = 0; i < menu_state.floppy_count; i++) {
        FloppyDisk_t *floppy = &menu_state.floppies[i];

        // Search in all text fields (case-insensitive)
        bool found = false;
        char search_lower[256];
        strncpy(search_lower, menu_state.search_text, sizeof(search_lower) - 1);
        search_lower[sizeof(search_lower) - 1] = '\0';

        // Convert search text to lowercase
        for (int j = 0; search_lower[j]; j++) {
            search_lower[j] = tolower(search_lower[j]);
        }

        // Check name
        if (strstr(floppy->name, search_lower) ||
            strcasestr(floppy->name, menu_state.search_text)) {
            found = true;
        }

        // Check description
        if (!found && (strstr(floppy->description, search_lower) ||
            strcasestr(floppy->description, menu_state.search_text))) {
            found = true;
        }

        // Check reference
        if (!found && (strstr(floppy->reference, search_lower) ||
            strcasestr(floppy->reference, menu_state.search_text))) {
            found = true;
        }

        // Check directory content
        if (!found && (strstr(floppy->directory_content, search_lower) ||
            strcasestr(floppy->directory_content, menu_state.search_text))) {
            found = true;
        }

        // Check product
        if (!found && (strstr(floppy->product, search_lower) ||
            strcasestr(floppy->product, menu_state.search_text))) {
            found = true;
        }

        if (found) {
            menu_state.filtered_indices[menu_state.filtered_count] = i;
            menu_state.filtered_count++;
        }
    }

    // Reset selection to first item if current selection is out of bounds
    if (menu_state.selected_index >= menu_state.filtered_count) {
        menu_state.selected_index = 0;
    }
}

// Handle search input
static void handle_search_input(int ch) {
    switch (ch) {
        case KEY_LEFT:
            if (menu_state.search_cursor > 0) {
                menu_state.search_cursor--;
            }
            break;
        case KEY_RIGHT:
            if (menu_state.search_cursor < strlen(menu_state.search_text)) {
                menu_state.search_cursor++;
            }
            break;
        case KEY_BACKSPACE:
        case 127: // Backspace
            if (menu_state.search_cursor > 0) {
                memmove(&menu_state.search_text[menu_state.search_cursor - 1],
                       &menu_state.search_text[menu_state.search_cursor],
                       strlen(&menu_state.search_text[menu_state.search_cursor]) + 1);
                menu_state.search_cursor--;
            }
            break;
        case KEY_DC: // Delete
            if (menu_state.search_cursor < strlen(menu_state.search_text)) {
                memmove(&menu_state.search_text[menu_state.search_cursor],
                       &menu_state.search_text[menu_state.search_cursor + 1],
                       strlen(&menu_state.search_text[menu_state.search_cursor + 1]) + 1);
            }
            break;
        default:
            if (isprint(ch) && strlen(menu_state.search_text) < sizeof(menu_state.search_text) - 1) {
                memmove(&menu_state.search_text[menu_state.search_cursor + 1],
                       &menu_state.search_text[menu_state.search_cursor],
                       strlen(&menu_state.search_text[menu_state.search_cursor]) + 1);
                menu_state.search_text[menu_state.search_cursor] = ch;
                menu_state.search_cursor++;
            }
            break;
    }
}

// Show mount popup function
static void show_mount_popup_for_floppy(int unit) {
    // Show mount popup instead of direct mounting
    show_mount_popup();
}

static void unmount_floppy(int unit) {
    // Show unmount popup instead of direct unmounting
    show_unmount_popup();
}

// Initialize mount popup window
static void init_mount_popup(void) {
    // Calculate popup window size and position
    int popup_height = 13;
    int popup_width = 60;
    int popup_y = (menu_state.max_y - popup_height) / 2;
    int popup_x = (menu_state.max_x - popup_width) / 2;

    menu_state.mount_popup.popup_win = newwin(popup_height, popup_width, popup_y, popup_x);
    menu_state.mount_popup.selected_unit = 0;
    menu_state.mount_popup.visible = false;
    menu_state.mount_popup.floppy = NULL;
}

// Show mount popup for the selected floppy
static void show_mount_popup(void) {
    int total_items = (menu_state.filtered_count > 0) ? menu_state.filtered_count : menu_state.floppy_count;

    if (menu_state.selected_index >= 0 && menu_state.selected_index < total_items) {
        int floppy_idx = (menu_state.filtered_count > 0) ?
                        menu_state.filtered_indices[menu_state.selected_index] : menu_state.selected_index;
        menu_state.mount_popup.floppy = &menu_state.floppies[floppy_idx];
        menu_state.mount_popup.selected_unit = 0;
        menu_state.mount_popup.visible = true;
    }
}

// Hide mount popup
static void hide_mount_popup(void) {
    menu_state.mount_popup.visible = false;
    menu_state.mount_popup.floppy = NULL;
}

// Draw mount popup window
static void draw_mount_popup(void) {
    if (!menu_state.mount_popup.visible || !menu_state.mount_popup.floppy) return;

    WINDOW *popup = menu_state.mount_popup.popup_win;
    FloppyDisk_t *floppy = menu_state.mount_popup.floppy;

    // Clear and draw border
    werase(popup);
    box(popup, 0, 0);

    // Draw title
    mvwprintw(popup, 0, 2, " Mount ");

    // Draw floppy details
    mvwprintw(popup, 2, 2, "Name: %s", floppy->name);
    mvwprintw(popup, 3, 2, "Description: %s", floppy->description);
    mvwprintw(popup, 4, 2, "MD5: %s", floppy->md5);

    // Get current mounted drives
    MountedDriveInfo_t* current_drives = list_mount(floppy->drive_type);
    if (!current_drives) {
        mvwprintw(popup, 6, 2, "Error: Could not get mounted drives");
        wnoutrefresh(popup);
        return;
    }

    // Draw unit selection based on drive type
    mvwprintw(popup, 6, 2, "Select unit to mount:");

    if (floppy->drive_type == DRIVE_SMD) {
        // SMD units 0-3
        for (int i = 0; i < 4; i++) {
            if (current_drives[i].is_mounted) {
                // Unit is mounted - show current disk
                mvwprintw(popup, 7 + i, 4, "%s smd-disc-1 unit %d (mounted: %s)",
                          menu_state.mount_popup.selected_unit == i ? ">" : " ",
                          i, current_drives[i].name);
            } else {
                // Unit is available
                mvwprintw(popup, 7 + i, 4, "%s smd-disc-1 unit %d",
                          menu_state.mount_popup.selected_unit == i ? ">" : " ",
                          i);
            }
        }
    } else {
        // Floppy units 0-2
        for (int i = 0; i < 3; i++) {
            if (current_drives[i].is_mounted) {
                // Unit is mounted - show current disk
                mvwprintw(popup, 7 + i, 4, "%s floppy-disc-1 unit %d (mounted: %s)",
                          menu_state.mount_popup.selected_unit == i ? ">" : " ",
                          i, current_drives[i].name);
            } else {
                // Unit is available
                mvwprintw(popup, 7 + i, 4, "%s floppy-disc-1 unit %d",
                          menu_state.mount_popup.selected_unit == i ? ">" : " ",
                          i);
            }
        }
    }

    // Draw instructions
    int instruction_line = (floppy->drive_type == DRIVE_SMD) ? 12 : 11;
    mvwprintw(popup, instruction_line, 2, "ESC=Exit  ENTER=Mount  UP/DOWN=Select Unit");

    wnoutrefresh(popup);
}

// Handle mount popup input
static void handle_mount_popup_input(int ch) {
    switch (ch) {
        case KEY_UP:
            if (menu_state.mount_popup.selected_unit > 0) {
                menu_state.mount_popup.selected_unit--;
            }
            break;
        case KEY_DOWN:
            {
                int max_unit = (menu_state.mount_popup.floppy && menu_state.mount_popup.floppy->drive_type == DRIVE_SMD) ? 3 : 2;
                if (menu_state.mount_popup.selected_unit < max_unit) {
                    menu_state.mount_popup.selected_unit++;
                }
            }
            break;
        case '\n':
        case '\r':  // ENTER
            if (menu_state.mount_popup.floppy) {
                // Check if selected unit is already mounted
                MountedDriveInfo_t* current_drives = list_mount(menu_state.mount_popup.floppy->drive_type);
                if (current_drives && current_drives[menu_state.mount_popup.selected_unit].name[0] != '\0') {
                    // Unit is already mounted - don't allow mount
                    // Could show a message here, but for now just ignore
                    break;
                }

                // Build image URL for downloading
                char* image_path = build_image_url(menu_state.mount_popup.floppy->md5);
                if (!image_path) {
                    // Could show error message here
                    break;
                }

                // Call the external mount function with correct drive type and image path
                mount_drive(menu_state.mount_popup.floppy->drive_type, menu_state.mount_popup.selected_unit,
                           menu_state.mount_popup.floppy->md5,
                           menu_state.mount_popup.floppy->name,
                           menu_state.mount_popup.floppy->description,
                           image_path);

                free(image_path);  // Free the allocated URL
                hide_mount_popup();
            }
            break;
        case 27:  // ESC
            hide_mount_popup();
            break;
    }
}

// Initialize unmount popup window
static void init_unmount_popup(void) {
    // Calculate popup window size and position
    int popup_height = 15;
    int popup_width = 70;
    int popup_y = (menu_state.max_y - popup_height) / 2;
    int popup_x = (menu_state.max_x - popup_width) / 2;

    menu_state.unmount_popup.popup_win = newwin(popup_height, popup_width, popup_y, popup_x);
    menu_state.unmount_popup.selected_unit = 0;
    menu_state.unmount_popup.visible = false;
    menu_state.unmount_popup.floppy = NULL;
}

// Show unmount popup
static void show_unmount_popup(void) {
    menu_state.unmount_popup.selected_unit = 0;
    menu_state.unmount_popup.visible = true;
    menu_state.unmount_popup.floppy = NULL;  // Not used for unmount
}

// Hide unmount popup
static void hide_unmount_popup(void) {
    menu_state.unmount_popup.visible = false;
    menu_state.unmount_popup.floppy = NULL;
}

// Helper function to format file size in KB or MB
static void format_file_size(size_t bytes, char* buffer, size_t buffer_size) {
    if (bytes >= 1024 * 1024) {
        // Convert to MB
        double mb = (double)bytes / (1024.0 * 1024.0);
        snprintf(buffer, buffer_size, "%.1f MB", mb);
    } else if (bytes >= 1024) {
        // Convert to KB
        double kb = (double)bytes / 1024.0;
        snprintf(buffer, buffer_size, "%.0f KB", kb);
    } else {
        // Show in bytes
        snprintf(buffer, buffer_size, "%zu B", bytes);
    }
}

// Draw unmount popup window
static void draw_unmount_popup(void) {
    if (!menu_state.unmount_popup.visible) return;

    WINDOW *popup = menu_state.unmount_popup.popup_win;

    // Clear and draw border
    werase(popup);
    box(popup, 0, 0);

    // Draw title
    mvwprintw(popup, 0, 2, " Unmount ");

    // Get mounted drives
    MountedDriveInfo_t* floppy_drives = list_mount(DRIVE_FLOPPY);
    MountedDriveInfo_t* smd_drives = list_mount(DRIVE_SMD);

    if (!floppy_drives || !smd_drives) {
        mvwprintw(popup, 2, 2, "Error: Could not get mounted drives");
        wnoutrefresh(popup);
        return;
    }

    int y_pos = 2;

    // Draw floppy drives section
    mvwprintw(popup, y_pos++, 2, "Floppy Drives (Units 0-2):");
    for (int i = 0; i < 3; i++) {
        if (floppy_drives[i].is_mounted) {
            // Drive is mounted - show name, description, source type, and file size
            const char* source_type = floppy_drives[i].is_remote ? "REMOTE" : "LOCAL";
            char size_str[32];
            format_file_size(floppy_drives[i].data_size, size_str, sizeof(size_str));
            mvwprintw(popup, y_pos++, 4, "%s Unit %d: %s (%s, %s) [%s]",
                      menu_state.unmount_popup.selected_unit == i ? ">" : " ",
                      i, floppy_drives[i].name, floppy_drives[i].description, size_str, source_type);
        } else {
            // Drive is not mounted
            mvwprintw(popup, y_pos++, 4, "%s Unit %d: (not mounted)",
                      menu_state.unmount_popup.selected_unit == i ? ">" : " ",
                      i);
        }
    }

    y_pos++;

    // Draw SMD drives section
    mvwprintw(popup, y_pos++, 2, "SMD Drives (Units 0-3):");
    for (int i = 0; i < 4; i++) {
        if (smd_drives[i].is_mounted) {
            // Drive is mounted - show name, description, source type, and file size
            const char* source_type = smd_drives[i].is_remote ? "REMOTE" : "LOCAL";
            char size_str[32];
            format_file_size(smd_drives[i].data_size, size_str, sizeof(size_str));
            mvwprintw(popup, y_pos++, 4, "%s Unit %d: %s (%s, %s) [%s]",
                      menu_state.unmount_popup.selected_unit == (i + 3) ? ">" : " ",
                      i, smd_drives[i].name, smd_drives[i].description, size_str, source_type);
        } else {
            // Drive is not mounted
            mvwprintw(popup, y_pos++, 4, "%s Unit %d: (not mounted)",
                      menu_state.unmount_popup.selected_unit == (i + 3) ? ">" : " ",
                      i);
        }
    }

    y_pos++;

    // Draw instructions
    mvwprintw(popup, y_pos++, 2, "ESC=Exit  ENTER=Unmount  UP/DOWN=Select");

    wnoutrefresh(popup);
}

// Handle unmount popup input
static void handle_unmount_popup_input(int ch) {
    switch (ch) {
        case KEY_UP:
            if (menu_state.unmount_popup.selected_unit > 0) {
                menu_state.unmount_popup.selected_unit--;
            }
            break;
        case KEY_DOWN:
            if (menu_state.unmount_popup.selected_unit < 6) {  // 0-2 for floppy, 3-6 for SMD
                menu_state.unmount_popup.selected_unit++;
            }
            break;
        case '\n':
        case '\r':  // ENTER
            {
                // Determine drive type and unit
                DRIVE_TYPE drive_type;
                int unit;

                if (menu_state.unmount_popup.selected_unit < 3) {
                    // Floppy drive
                    drive_type = DRIVE_FLOPPY;
                    unit = menu_state.unmount_popup.selected_unit;
                } else {
                    // SMD drive
                    drive_type = DRIVE_SMD;
                    unit = menu_state.unmount_popup.selected_unit - 3;
                }

                // Call unmount function
                unmount_drive(drive_type, unit);
                hide_unmount_popup();
            }
            break;
        case 27:  // ESC
            hide_unmount_popup();
            break;
    }
}

// Main menu loop
static void menu_loop(void) {
    int ch;
    bool running = true;

    while (running) {
        draw_search_box();
        draw_floppy_list();
        draw_floppy_details();
        draw_toolbar();

        // Draw mount popup if visible
        if (menu_state.mount_popup.visible) {
            draw_mount_popup();
        }

        // Draw unmount popup if visible
        if (menu_state.unmount_popup.visible) {
            draw_unmount_popup();
        }

        // Position cursor in search box and flush all window updates
        curs_set(1);
        wmove(menu_state.search_win, 1, 2 + menu_state.search_cursor);
        wnoutrefresh(menu_state.search_win);
        doupdate();

        ch = getch();

        if (menu_state.mount_popup.visible) {
            // Handle mount popup input
            handle_mount_popup_input(ch);
        } else if (menu_state.unmount_popup.visible) {
            // Handle unmount popup input
            handle_unmount_popup_input(ch);
        } else {
            // Handle main menu input
            switch (ch) {
                case 27: // ESC
                    running = false;
                    break;
                case '\n':
                case '\r': // ENTER - apply search filter
                    filter_floppies();
                    break;
                case KEY_UP:
                    if (menu_state.selected_index > 0) {
                        menu_state.selected_index--;
                        free_directory_pages();  // Reset pages when changing floppy
                    }
                    break;
                case KEY_DOWN:
                    {
                        int total_items = (menu_state.filtered_count > 0) ? menu_state.filtered_count : menu_state.floppy_count;
                        if (menu_state.selected_index < total_items - 1) {
                            menu_state.selected_index++;
                            free_directory_pages();  // Reset pages when changing floppy
                        }
                    }
                    break;
                case KEY_LEFT:
                    // Horizontal scroll for details window
                    if (menu_state.detail_scroll_x > 0) {
                        menu_state.detail_scroll_x -= 10;
                        if (menu_state.detail_scroll_x < 0) menu_state.detail_scroll_x = 0;
                    }
                    break;
                case KEY_RIGHT:
                    // Horizontal scroll for details window
                    menu_state.detail_scroll_x += 10;
                    if (menu_state.detail_scroll_x > 50) menu_state.detail_scroll_x = 50;
                    break;
                case KEY_PPAGE:  // Page Up
                    // Navigate to previous page using page array
                    if (menu_state.directory_pages && menu_state.current_page > 0) {
                        menu_state.current_page--;
                    }
                    break;
                case KEY_NPAGE:  // Page Down
                    // Navigate to next page using page array
                    if (menu_state.directory_pages && menu_state.current_page < menu_state.directory_page_count - 1) {
                        menu_state.current_page++;
                    }
                    break;
                case KEY_F(5):   // F5 - mount floppy
                case KEY_IC:     // Insert key - mount floppy (fallback)
                    show_mount_popup_for_floppy(0);
                    break;
                case KEY_F(6):   // F6 - unmount floppy
                case KEY_DC:     // Delete key - unmount floppy (fallback)
                    unmount_floppy(0);
                    break;
                case KEY_F(7):   // F7 - force refresh catalog
                case 18:         // Ctrl-R (fallback)
                    {
                        // Exit curses to show download progress
                        cleanup_curses();

                        // Free current floppy data
                        free_directory_pages();
                        if (menu_state.filtered_indices) {
                            free(menu_state.filtered_indices);
                            menu_state.filtered_indices = NULL;
                            menu_state.filtered_count = 0;
                        }
                        if (menu_state.floppies) {
                            for (int i = 0; i < menu_state.floppy_count; i++) {
                                if (menu_state.floppies[i].directory_content) {
                                    free(menu_state.floppies[i].directory_content);
                                }
                            }
                            free(menu_state.floppies);
                            menu_state.floppies = NULL;
                            menu_state.floppy_count = 0;
                        }

                        printf("\033[2J\033[H");
                        printf("=== Floppy Database Browser ===\n\n");
                        printf("  Refreshing catalog from ndlib.hackercorp.no ...\n");
                        fflush(stdout);

                        bool from_cache_refresh = false;
                        char *refreshed_json = get_cached_or_download_json(true, &from_cache_refresh);
                        if (refreshed_json) {
                            parse_floppies_json(refreshed_json);
                            free(refreshed_json);
                        } else {
                            printf("  Refresh failed. Returning to menu.\n");
                            fflush(stdout);
                        }

                        // Reset menu state
                        menu_state.selected_index = 0;
                        menu_state.search_text[0] = '\0';
                        menu_state.search_cursor = 0;
                        menu_state.detail_scroll_x = 0;
                        menu_state.detail_scroll_y = 0;

                        // Re-enter curses
                        init_curses();
                        init_mount_popup();
                        init_unmount_popup();
                    }
                    break;
                default:
                    // Handle search input (always active)
                    handle_search_input(ch);
                    break;
            }
        }
    }
}

// Main menu function
int show_floppy_menu(void) {
    // Initialize menu state
    memset(&menu_state, 0, sizeof(menu_state));
    menu_state.selected_index = 0;
    menu_state.filtered_indices = NULL;
    menu_state.filtered_count = 0;
    menu_state.detail_scroll_x = 0;
    menu_state.detail_scroll_y = 0;
    menu_state.search_cursor = 0;  // Initialize search cursor

    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Check cache age to decide what message to show
    long age = cache_age_seconds();
    bool have_valid_cache = (age >= 0 && age < CACHE_MAX_AGE_SECONDS);

    printf("\033[2J\033[H");
    printf("=== Floppy Database Browser ===\n\n");
    if (have_valid_cache) {
        printf("  Loading floppy catalog (cached)...\n");
    } else {
        printf("  Downloading floppy catalog from ndlib.hackercorp.no ...\n");
    }
    fflush(stdout);

    // Get JSON data (from cache or network)
    bool from_cache = false;
    char *json_data = get_cached_or_download_json(false, &from_cache);
    if (!json_data) {
        printf("\n  Download failed. Check your network connection.\n");
        printf("  Press any key to return.\n");
        fflush(stdout);
        (void)read_key_event();
        return -1;
    }

    // Parse JSON
    if (!parse_floppies_json(json_data)) {
        free(json_data);
        return -1;
    }

    free(json_data);

    if (menu_state.floppy_count == 0) {
        return -1;
    }

    // Initialize curses
    init_curses();

    // Initialize mount popup
    init_mount_popup();

    // Initialize unmount popup
    init_unmount_popup();

    // Run menu loop
    menu_loop();

    // Cleanup
    cleanup_curses();
    free_directory_pages();  // Free directory pages

    // Clean up mount popup window
    if (menu_state.mount_popup.popup_win) {
        delwin(menu_state.mount_popup.popup_win);
    }

    // Clean up unmount popup window
    if (menu_state.unmount_popup.popup_win) {
        delwin(menu_state.unmount_popup.popup_win);
    }
    if (menu_state.filtered_indices) {
        free(menu_state.filtered_indices);
    }

    // Free dynamically allocated directory content
    if (menu_state.floppies) {
        for (int i = 0; i < menu_state.floppy_count; i++) {
            if (menu_state.floppies[i].directory_content) {
                free(menu_state.floppies[i].directory_content);
            }
        }
        free(menu_state.floppies);
    }

    curl_global_cleanup();

    return 0;
}