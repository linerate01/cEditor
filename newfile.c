#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <signal.h>  // 시그널 핸들링을 위한 헤더
#include <unistd.h>
#include <ctype.h>

#define MENU_HEIGHT 1
#define STATUS_HEIGHT 1
#define MAX_FILES 256
#define MAX_ROWS 1000
#define MAX_COLS 256

WINDOW* editor_win;

const char* menu_titles[] = {"File", "Edit", "View", "Help"};
const char* file_menu[] = {"New", "Open", "Save", "Exit"};

int current_menu = -1;
int current_item = 0;
int input_enabled = 0;
int scroll_offset = 0;  // 현재 화면의 첫 줄이 editor_buf 몇 번째 줄인지
int cursor_x = 0, cursor_y = 0;
char editor_buf[MAX_ROWS][MAX_COLS];
char opened_filename[256] = "";


int get_filename_from_user(char* out_filename);

void show_editor_logo() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    const char* logo[] = {
        " _   _  _               _____      _  _  _                ",
        "| \\ | |(_)             |  ___|    | |(_)| |               ",
        "|  \\| | _   ___   ___  | |__    __| | _ | |_   ___   _ __ ",
        "| . ` || | / __| / _ \\ |  __|  / _` || || __| / _ \\ | '__|",
        "| |\\  || || (__ |  __/ | |___ | (_| || || |_ | (_) || |   ",
        "\\_| \\_/|_| \\___| \\___| \\____/  \\__,_||_| \\__| \\___/ |_|   "
    };

    int logo_lines = sizeof(logo) / sizeof(logo[0]);
    int win_height = getmaxy(editor_win) - 2;
    int start_y = (win_height - logo_lines) / 2;

    werase(editor_win);
    box(editor_win, 0, 0);

    wattron(editor_win, COLOR_PAIR(4));  // 마젠타 색상 ON

    for (int i = 0; i < logo_lines; i++) {
        int len = strlen(logo[i]);
        int start_x = (cols - len) / 2;
        mvwprintw(editor_win, start_y + i, start_x, "%s", logo[i]);
    }

    wattroff(editor_win, COLOR_PAIR(4)); // 색상 OFF
    wrefresh(editor_win);

    usleep(1500000);  // 1.5초 대기
    werase(editor_win);
    box(editor_win, 0, 0);
    wrefresh(editor_win);
}


void draw_menu_bar() {
    attron(COLOR_PAIR(2));
    mvprintw(0, 0, " File  Edit  View  Help ");
    attroff(COLOR_PAIR(2));
    refresh();
}

void draw_editor_window() {
    if (editor_win) {
        delwin(editor_win);
    }
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    editor_win = newwin(rows - MENU_HEIGHT - STATUS_HEIGHT, cols, MENU_HEIGHT, 0);
    box(editor_win, 0, 0);
    wrefresh(editor_win);
}

void draw_status_bar(const char* message) {
    int rows;
    getmaxyx(stdscr, rows, _);
    move(rows - 1, 0);
    clrtoeol();
    attron(A_REVERSE);
    mvprintw(rows - 1, 0, "%s", message);
    attroff(A_REVERSE);
    refresh();
}

void render_editor_buffer() {
    werase(editor_win);
    box(editor_win, 0, 0);
    int visible_lines = getmaxy(editor_win) - 2;

    for (int y = 0; y < visible_lines; y++) {
        int buf_line = y + scroll_offset;

        if (buf_line < MAX_ROWS && strlen(editor_buf[buf_line]) > 0) { //
            if (y == cursor_y) {
                wattron(editor_win, A_REVERSE);
                // 전체 줄을 공백으로 칠해서 줄 전체 강조
                int win_width = getmaxx(editor_win);
                mvwhline(editor_win, y + 1, 1, ' ', win_width - 2);
                mvwprintw(editor_win, y + 1, 1, "%s", editor_buf[buf_line]);
                wattroff(editor_win, A_REVERSE);
            } else {
                mvwprintw(editor_win, y + 1, 1, "%s", editor_buf[buf_line]);
            }

        }
    }

    wmove(editor_win, cursor_y + 1, cursor_x + 1);
    wrefresh(editor_win);
}

void save_current_file() {
    const char* filename = (strlen(opened_filename) > 0) ? opened_filename : "untitled.txt";
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        draw_status_bar("Failed to save file.");
        return;
    }
    for (int i = 0; i < MAX_ROWS; i++) {
        fprintf(fp, "%s\n", editor_buf[i]);
    }
    fclose(fp);
    draw_status_bar("Saved to file.");
}

void handle_key_input(int ch) {
    if (!input_enabled || !editor_win) return;

    int visible_lines = getmaxy(editor_win) - 2;
    int actual_row = cursor_y + scroll_offset;

    switch (ch) {
        case KEY_LEFT:
            if (cursor_x > 0) {
                cursor_x--;
            } else if (cursor_y > 0 || scroll_offset > 0) {
                // 이전 줄의 끝으로 이동
                if (cursor_y > 0) {
                    cursor_y--;
                } else {
                    scroll_offset--;
                }
                actual_row = cursor_y + scroll_offset;
                cursor_x = strlen(editor_buf[actual_row]);
            }
            break;

        case KEY_RIGHT:
            if (editor_buf[actual_row][cursor_x] != '\0' && cursor_x < MAX_COLS - 2) {
                cursor_x++;
            } else if (cursor_y < visible_lines - 1 && actual_row + 1 < MAX_ROWS) {
                cursor_y++;
                cursor_x = 0;
            } else if (scroll_offset + visible_lines < MAX_ROWS) {
                scroll_offset++;
                cursor_x = 0;
            }
            break;

        case KEY_UP:
            if (cursor_y > 0) {
                cursor_y--;
            } else if (scroll_offset > 0) {
                scroll_offset--;
            }
            break;

        case KEY_DOWN:
            if (cursor_y < visible_lines - 1 && actual_row + 1 < MAX_ROWS) {
                cursor_y++;
            } else if (scroll_offset + visible_lines < MAX_ROWS) {
                scroll_offset++;
            }
            break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            if (cursor_x > 0) {
                memmove(&editor_buf[actual_row][cursor_x - 1],
                        &editor_buf[actual_row][cursor_x],
                        strlen(&editor_buf[actual_row][cursor_x]) + 1);
                cursor_x--;
            } else if (actual_row > 0) {
                int prev_len = strlen(editor_buf[actual_row - 1]);
                if (prev_len + strlen(editor_buf[actual_row]) < MAX_COLS) {
                    strcat(editor_buf[actual_row - 1], editor_buf[actual_row]);
                    for (int i = actual_row; i < MAX_ROWS - 1; i++) {
                        strcpy(editor_buf[i], editor_buf[i + 1]);
                    }
                    if (cursor_y > 0) {
                        cursor_y--;
                    } else {
                        scroll_offset--;
                    }
                    cursor_x = prev_len;
                }
            }
            break;

        case 10:  // Enter
            if (actual_row < MAX_ROWS - 1) {
                for (int i = MAX_ROWS - 2; i > actual_row; i--) {
                    strcpy(editor_buf[i + 1], editor_buf[i]);
                }
                strcpy(editor_buf[actual_row + 1], &editor_buf[actual_row][cursor_x]);
                editor_buf[actual_row][cursor_x] = '\0';

                if (cursor_y < visible_lines - 1) {
                    cursor_y++;
                } else {
                    scroll_offset++;
                }
                cursor_x = 0;
            }
            break;

        default:
            if (ch == '\t') {
                // 탭 키 입력 시 공백 4칸 삽입
                if (strlen(editor_buf[actual_row]) + 4 < MAX_COLS - 1) {
                    for (int i = 0; i < 4; i++) {
                        memmove(&editor_buf[actual_row][cursor_x + 1],
                                &editor_buf[actual_row][cursor_x],
                                strlen(&editor_buf[actual_row][cursor_x]) + 1);
                        editor_buf[actual_row][cursor_x] = ' ';
                        cursor_x++;
                    }
                }
            } else if (ch >= 32 && ch <= 126 && strlen(editor_buf[actual_row]) < MAX_COLS - 1) {
                memmove(&editor_buf[actual_row][cursor_x + 1],
                        &editor_buf[actual_row][cursor_x],
                        strlen(&editor_buf[actual_row][cursor_x]) + 1);
                editor_buf[actual_row][cursor_x] = ch;
                cursor_x++;
            }
            break;
    }

    render_editor_buffer();
}


void show_file_list_popup() {
    DIR* dir;
    struct dirent* entry;
    char* files[MAX_FILES];
    int count = 0;

    dir = opendir(".");
    if (!dir) {
        draw_status_bar("Failed to open directory.");
        return;
    }

    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            files[count] = strdup(entry->d_name);
            count++;
        }
    }
    closedir(dir);

    int win_h = 15;  // 고정된 높이로 보여줄 최대 줄 수 + 테두리
    int win_w = 40;
    int start_y = 3;
    int start_x = 5;

    WINDOW* popup = newwin(win_h, win_w, start_y, start_x);
    box(popup, 0, 0);
    mvwprintw(popup, 1, 2, "Select a file:");

    int highlight = 0;
    int offset = 0;  // 스크롤 오프셋
    int ch;
    keypad(popup, TRUE);

    while (1) {
        for (int i = 0; i < win_h - 4; i++) {
            int file_index = i + offset;
            if (file_index >= count) break;

            if (file_index == highlight)
                wattron(popup, A_REVERSE);
            mvwprintw(popup, i + 2, 2, "%-36s", files[file_index]);
            wattroff(popup, A_REVERSE);
        }
        wrefresh(popup);

        ch = wgetch(popup);
        if (ch == KEY_UP) {
            if (highlight > 0) highlight--;
            if (highlight < offset) offset--;
        } else if (ch == KEY_DOWN) {
            if (highlight < count - 1) highlight++;
            if (highlight >= offset + (win_h - 4)) offset++;
        } else if (ch == 10) {
            FILE* fp = fopen(files[highlight], "r");
            if (fp) {
                memset(editor_buf, 0, sizeof(editor_buf));
                int row = 0;
                while (fgets(editor_buf[row], MAX_COLS, fp) && row < MAX_ROWS - 1) {
                    editor_buf[row][strcspn(editor_buf[row], "\n")] = '\0';
                    row++;
                }
                fclose(fp);
                strcpy(opened_filename, files[highlight]);
                input_enabled = 1;
                cursor_x = cursor_y = 0;
                render_editor_buffer();
                draw_status_bar(files[highlight]);
            } else {
                draw_status_bar("Failed to open file.");
            }
            break;
        } else if (ch == 27) {
            break;
        }
    }

    for (int i = 0; i < count; i++) free(files[i]);
    delwin(popup);
    touchwin(stdscr);
    refresh();
    box(editor_win, 0, 0);
    wrefresh(editor_win);
}


void draw_dropdown(int menu_index) {
    int x = menu_index * 6;
    int count = 4;
    for (int i = 0; i < count; i++) {
        attron(COLOR_PAIR(i == current_item ? 3 : 1));
        mvprintw(1 + i, x, "%-10s", file_menu[i]);
        attroff(COLOR_PAIR(i == current_item ? 3 : 1));
    }
    refresh();
}

void clear_dropdown(int menu_index) {
    int x = menu_index * 6;
    for (int i = 0; i < 4; i++) {
        move(1 + i, x);
        clrtoeol();
    }
    if (editor_win) {
        box(editor_win, 0, 0);
        wrefresh(editor_win);
    }
    refresh();
}

int handle_menu_input(int ch) {
    if (current_menu == -1 && ch == 12) {
        current_menu = 0;
        current_item = 0;
        draw_dropdown(current_menu);
        return 1;
    }

    if (current_menu != -1) {
        switch (ch) {
            case KEY_LEFT:
                clear_dropdown(current_menu);
                current_menu = (current_menu + 3) % 4;
                current_item = 0;
                draw_dropdown(current_menu);
                break;
            case KEY_RIGHT:
                clear_dropdown(current_menu);
                current_menu = (current_menu + 1) % 4;
                current_item = 0;
                draw_dropdown(current_menu);
                break;
            case KEY_UP:
                current_item = (current_item + 3) % 4;
                draw_dropdown(current_menu);
                break;
            case KEY_DOWN:
                current_item = (current_item + 1) % 4;
                draw_dropdown(current_menu);
                break;
            case 10:
                draw_status_bar(file_menu[current_item]);
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);
                 if (strcmp(file_menu[current_item], "New") == 0) {
                    char newname[256] = "";
                    if (get_filename_from_user(newname)) {
                        memset(editor_buf, 0, sizeof(editor_buf));
                        cursor_x = cursor_y = scroll_offset = 0;
                        strcpy(opened_filename, newname);
                        input_enabled = 1;
                        render_editor_buffer();
                        draw_status_bar("New file created. Start typing.");
                    } else {
                        draw_status_bar("Filename input cancelled.");
                    }
                } else if (strcmp(file_menu[current_item], "Open") == 0) {
                    show_file_list_popup();
                } else if (strcmp(file_menu[current_item], "Save") == 0) {
                    save_current_file();
                } else if (strcmp(file_menu[current_item], "Exit") == 0) {
                    endwin();
                    exit(0);
                } else {
                    draw_status_bar("[Not Implemented] Press F10 to Exit.");
                }
                current_menu = -1;
                return 1;
            case 27:
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);
                current_menu = -1;
                return 1;
        }
        return 1;
    }
    return 0;
}

void handle_resize(int sig) {
    endwin();
    refresh();
    clear();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    resizeterm(rows, cols);

    draw_menu_bar();
    draw_editor_window();
    render_editor_buffer();
    draw_status_bar("Window resized.");
}

int get_filename_from_user(char* out_filename) {
    int width = 40;
    int height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW* input_win = newwin(height, width, start_y, start_x);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "Enter new filename:");
    wrefresh(input_win);

    echo();
    curs_set(1);
    mvwgetnstr(input_win, 2, 2, out_filename, 255);
    noecho();
    curs_set(0);

    delwin(input_win);
    touchwin(stdscr);
    refresh();
    return strlen(out_filename) > 0;
}



int main() {
    
    initscr();
    signal(SIGWINCH, handle_resize);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(2);
    start_color();
    use_default_colors();

    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_WHITE, COLOR_MAGENTA);
    init_pair(3, COLOR_WHITE, COLOR_MAGENTA);
    init_pair(4, COLOR_MAGENTA, -1);

    draw_menu_bar();
    draw_editor_window();
    draw_editor_window();   // 테두리 먼저
    show_editor_logo();     // 로고 출력 후 키 입력 대기
    render_editor_buffer(); // 편집기 초기화

    draw_status_bar("Press Ctrl+L to open menu | F10 to exit");

    int ch;
    while ((ch = getch()) != KEY_F(10)) {
        // Alt+S 입력 감지: ESC → 's'
        if (ch == 27) { // ESC
            int next = getch();  // Alt 조합 키의 실제 문자
            if (next == 's' || next == 'S') {
                save_current_file();
                continue;
            }
            ungetch(next); // Alt+S가 아니라면 다음 키를 다시 큐에 넣기
        }

        if (!handle_menu_input(ch)) {
            handle_key_input(ch);
        }
    }


    endwin();
    return 0;
}

