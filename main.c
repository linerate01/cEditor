#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <signal.h>  // 시그널 핸들링을 위한 헤더
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#define MENU_HEIGHT 1
#define STATUS_HEIGHT 1
#define MAX_FILES 256
#define MAX_ROWS 1000
#define MAX_COLS 256

WINDOW* editor_win;

const char* menu_titles[] = {"File", "Build", "Option", "Help"};
const char* file_menu[] = {"New", "Open", "Save", "Exit"};
const char* build_menu[] = {"Run", "Link"};
const char* option_menu[] = {"NUM", "SYN", "{ }"};
char status_message[256] = "Welcome to Nice Editor! Press F1 for help.";

int show_line_numbers = 0;
int show_syntax_highlight = 1;
int hide_brackets = 0;
int current_menu = -1;
int current_item = 0;
int input_enabled = 0;
int scroll_offset = 0;  // 현재 화면의 첫 줄이 editor_buf 몇 번째 줄인지
int cursor_x = 0, cursor_y = 0;
char editor_buf[MAX_ROWS][MAX_COLS];
char opened_filename[256] = "";
char link_flags[256] = "";
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void draw_menu_bar();
void draw_editor_window();
void draw_status_bar(const char* message);
void draw_dropdown(int menu_index);

void render_editor_buffer();

void handle_key_input(int ch);
void handle_resize(int sig);
int handle_menu_input(int ch);


int get_filename_from_user(char* out_filename);
int is_cyan_keyword(const char* word);
int is_magenta_keyword(const char* word);

void show_editor_logo();
void show_file_list_popup();

void clear_dropdown(int menu_index);

void run_in_gnome_terminal();
void tap(int actual_row, int actual_col);
void countBlock(int actual_row, int actual_col);
void search_text();
void save_current_file();

// auto saving
void* saveFileThread(void*);
void autoSaveHandler();

const char* cyan_keywords[] = {"int", "double", "float", "enum", "char", "short", "long", "malloc", "free", "calloc", "realloc", NULL};
const char* magenta_keywords[] = {"void", "unsigned", "signed", "sizeof", "typedef", "struct", "union", "extern", "static", "const",
                                 "if", "else", "switch", "case", "default", "while", "for", "do", "continue", "break", "return", NULL};

void autoSaveHandler() {
    pthread_t t;

    if (pthread_create(&t, NULL, saveFileThread, NULL) != 0) 
        perror("can't creat thread for autosave");
    else
        pthread_detach(t);

    alarm(5);
}

void* saveFileThread(void *arg) {
    pthread_mutex_lock(&mutex);
    const char* filename = (strlen(opened_filename) > 0) ? opened_filename : "untitled.txt";
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        draw_status_bar("Failed to save file.");
        return NULL;
    }
    for (int i = 0; i < MAX_ROWS; i++) {
        fprintf(fp, "%s\n", editor_buf[i]);
    }
    fclose(fp);
    draw_status_bar("the file is autosaved.");
    pthread_mutex_unlock(&mutex);
}

int get_user_input(const char* prompt, char* out) {
    int width = 50, height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW* win = newwin(height, width, start_y, start_x);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", prompt);
    wrefresh(win);

    echo();
    curs_set(1);
    mvwgetnstr(win, 2, 2, out, 255);
    noecho();
    curs_set(0);

    delwin(win);
    touchwin(stdscr);
    refresh();
    return strlen(out) > 0;
}

void search_text() {
    char query[256] = "";
    int width = 40;
    int height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW* input_win = newwin(height, width, start_y, start_x);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "Search for:");
    wrefresh(input_win);

    echo();
    curs_set(1);
    mvwgetnstr(input_win, 2, 2, query, 255);
    noecho();
    curs_set(0);

    delwin(input_win);
    touchwin(stdscr);
    refresh();

    if (strlen(query) == 0) {
        draw_status_bar("Search cancelled.");
        return;
    }

    // 검색 수행
    for (int i = 0; i < MAX_ROWS; i++) {
        char* pos = strstr(editor_buf[i], query);
        if (pos) {
            int col = pos - editor_buf[i];
            cursor_y = 0;
            scroll_offset = i;
            cursor_x = col;
            render_editor_buffer();

            char msg[256];
            snprintf(msg, sizeof(msg), "Found at line %d, column %d", i + 1, col + 1);
            draw_status_bar(msg);
            return;
        }
    }

    draw_status_bar("No match found.");
}

void run_in_gnome_terminal() {
    if (strlen(opened_filename) == 0) {
        draw_status_bar("No file to compile.");
        return;
    }

    save_current_file();  // 현재 파일 저장

    char cmd[1024];
   snprintf(cmd, sizeof(cmd),
    "gnome-terminal -- bash -c \"gcc '%s' %s -o temp_out && ./temp_out; echo; echo Press ENTER to close...; read\"",
    opened_filename,
    link_flags);

    pid_t pid = fork();
    if (pid == 0) {
        // 자식 프로세스가 명령 실행
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        exit(1);  // execl 실패 시 종료
    } else if (pid < 0) {
        draw_status_bar("Failed to fork.");
    } else {
        waitpid(pid, NULL, 0);  // 안전하게 자식 종료 기다리기
    }
}

int is_cyan_keyword(const char* word) {
    for (int i = 0; cyan_keywords[i] != NULL; i++) {
        if (strcmp(word, cyan_keywords[i]) == 0) return 1;
    }
    return 0;
}

int is_magenta_keyword(const char* word) {
    for (int i = 0; magenta_keywords[i] != NULL; i++) {
        if (strcmp(word, magenta_keywords[i]) == 0) return 1;
    }
    return 0;
}


void show_editor_logo() {
    int unused_rows, cols;
    getmaxyx(stdscr, unused_rows, cols);

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
    int cols = getmaxx(stdscr);
    const char* menu_text = " File  Build  Option  Help ";
    const char* hint = "Ctrl+L: menu | F10: exit | F5: compile & run | Ctrl+F: find";

    // 메뉴바 왼쪽
    attron(COLOR_PAIR(2));
    mvprintw(0, 0, "%s", menu_text);
    attroff(COLOR_PAIR(2));

    // 오른쪽 여백에 단축키 힌트 출력 (오른쪽 정렬)
    int hint_start = cols - strlen(hint) - 1;
    if (hint_start > (int)strlen(menu_text)) {
        mvprintw(0, hint_start, "%s", hint);
    }

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
    if (message) strncpy(status_message, message, sizeof(status_message));
    
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    char left[cols + 1];
    char right[cols + 1];

    snprintf(left, sizeof(left), " %s", status_message);
    snprintf(right, sizeof(right), " %s | Ln %d, Col %d ", 
             (strlen(opened_filename) > 0) ? opened_filename : "[No File]",
             cursor_y + scroll_offset + 1, cursor_x + 1);

    int left_len = strlen(left);
    int right_len = strlen(right);
    int space = cols - left_len - right_len;
    if (space < 0) space = 0;

    move(rows - 1, 0);
    clrtoeol();
    attron(A_REVERSE);
    printw("%s%*s%s", left, space, "", right);
    attroff(A_REVERSE);
    refresh();
}

void render_editor_buffer() {
    werase(editor_win);
    box(editor_win, 0, 0);
    int visible_lines = getmaxy(editor_win) - 2;
    
    //int editor_cols = getmaxx(editor_win) - 2;
    int gutter = show_line_numbers ? 4 : 0;
    //int min_cursor_x = gutter + 1;
    pthread_mutex_lock(&mutex);
    for (int y = 0; y < visible_lines; y++) {
        int buf_line = y + scroll_offset;
        if (buf_line >= MAX_ROWS) continue;

        const char* line = editor_buf[buf_line];
        int len = strlen(line);
        int x = gutter + 1;

        if (show_line_numbers) {
            mvwprintw(editor_win, y + 1, 1, "%3d", buf_line + 1);
            mvwaddch(editor_win, y + 1, 4, ACS_VLINE);
        }

        for (int i = 0; i < len;) {
            if (hide_brackets && (line[i] == '{' || line[i] == '}' || line[i] == '(' || line[i] == ')')) {
                i++; x++;
                continue;
            }

            if (show_syntax_highlight && line[i] == '\"') {
                wattron(editor_win, COLOR_PAIR(5));
                mvwaddch(editor_win, y + 1, x++, line[i++]);
                while (i < len && !(line[i] == '\"' && line[i - 1] != '\\')) {
                    mvwaddch(editor_win, y + 1, x++, line[i++]);
                }
                if (i < len) mvwaddch(editor_win, y + 1, x++, line[i++]);
                wattroff(editor_win, COLOR_PAIR(5));
            } else if (show_syntax_highlight && i == 0 && line[i] == '#') {
                wattron(editor_win, COLOR_PAIR(5));
                while (i < len) mvwaddch(editor_win, y + 1, x++, line[i++]);
                wattroff(editor_win, COLOR_PAIR(5));
                break;
            } else if (show_syntax_highlight && line[i] == '/' && line[i + 1] == '/') {
                wattron(editor_win, COLOR_PAIR(6));
                while (i < len) mvwaddch(editor_win, y + 1, x++, line[i++]);
                wattroff(editor_win, COLOR_PAIR(6));
                break;
            } else if (line[i] == ';') {
                if (show_syntax_highlight) wattron(editor_win, COLOR_PAIR(7));
                mvwaddch(editor_win, y + 1, x++, line[i++]);
                if (show_syntax_highlight) wattroff(editor_win, COLOR_PAIR(7));
            } else if (isalpha(line[i]) || line[i] == '_') {
                char word[64] = {0};
                int j = 0;
                while ((isalnum(line[i]) || line[i] == '_') && j < 63) {
                    word[j++] = line[i++];
                }
                word[j] = '\0';

                if (show_syntax_highlight && is_cyan_keyword(word)) {
                    wattron(editor_win, COLOR_PAIR(9));
                    mvwprintw(editor_win, y + 1, x, "%s", word);
                    wattroff(editor_win, COLOR_PAIR(9));
                } else if (show_syntax_highlight && is_magenta_keyword(word)) {
                    wattron(editor_win, COLOR_PAIR(8));
                    mvwprintw(editor_win, y + 1, x, "%s", word);
                    wattroff(editor_win, COLOR_PAIR(8));
                } else {
                    mvwprintw(editor_win, y + 1, x, "%s", word);
                }
                x += strlen(word);
            } else {
                mvwaddch(editor_win, y + 1, x++, line[i++]);
            }
        }
        pthread_mutex_unlock(&mutex);
        if (y == cursor_y) {
            wmove(editor_win, y + 1, cursor_x);
        }
    }

    // 커서 위치 보정
    /*int actual_row = scroll_offset + cursor_y;
    int line_len = strlen(editor_buf[actual_row]);
    if (cursor_x < min_cursor_x)
        cursor_x = min_cursor_x;
    if (cursor_x - gutter > line_len)
        cursor_x = line_len + gutter + 1;
    if (cursor_x > editor_cols)
        cursor_x = editor_cols;
    draw_status_bar(NULL);
    wmove(editor_win, cursor_y + 1, cursor_x + 1);
    curs_set(2);
    wrefresh(editor_win);*/

    //커서 위치 보정 예전꺼
    int max_y = getmaxy(editor_win) - 2;
    int max_x = getmaxx(editor_win) - 2;
    if (cursor_y >= max_y) cursor_y = max_y - 1;
    if (cursor_x >= max_x) cursor_x = max_x - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x < 0) cursor_x = 0;
    draw_status_bar("Ctrl+L: menu | F10: exit | F5: compile & run | Ctrl+F: find");
    wmove(editor_win, cursor_y + 1, cursor_x + 1);
    curs_set(2);
    wrefresh(editor_win);
}


void save_current_file() {
    pthread_mutex_lock(&mutex);
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
    pthread_mutex_unlock(&mutex);
}

void tap(int actual_row, int actual_col){
    if (strlen(editor_buf[actual_row]) + 4 < MAX_COLS - 1) {
        for (int i = 0; i < 4; i++) {
            memmove(&editor_buf[actual_row][actual_col + 1],
                    &editor_buf[actual_row][actual_col],
                    strlen(&editor_buf[actual_row][actual_col]) + 1);
            editor_buf[actual_row][actual_col++] = ' ';
            cursor_x++;
        }
    }
}

void countBlock(int actual_row, int actual_col){
    int count = 0;
    for(int i = 0; i <= actual_row; i++){
        if(strchr(editor_buf[i], '{'))
            count++;
        if(strchr(editor_buf[i], '}'))
            count--;
    }

    for(int i = 0; i < count; i++){
        tap(actual_row, actual_col);
    }
}

void handle_key_input(int ch) {
    
    if (!input_enabled || !editor_win) return;
    int visible_lines = getmaxy(editor_win) - 2;
    int actual_row = cursor_y + scroll_offset;
    int gutter = show_line_numbers ? 4 : 0;
    int actual_col = cursor_x - gutter;

    switch (ch) {
        case KEY_LEFT: // 왼쪽으로 이동
            if (cursor_x > gutter) {
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

        case KEY_RIGHT: // 오른쪽으로 이동
            if (editor_buf[actual_row][actual_col] != '\0' && cursor_x < MAX_COLS - 2) {
                cursor_x++;
            } else if (cursor_y < visible_lines - 1 && actual_row + 1 < MAX_ROWS) {
                cursor_y++;
                cursor_x = gutter;
            } else if (scroll_offset + visible_lines < MAX_ROWS) {
                scroll_offset++;
                cursor_x = gutter;
            }
            break;

        case KEY_UP: // 위로 이동
            if (cursor_y > 0) {
                cursor_y--;
            } else if (scroll_offset > 0) {
                scroll_offset--;
            }
            //그 줄의 뒤로 가도록 계산 
            actual_row = cursor_y + scroll_offset;
            cursor_x = strlen(editor_buf[actual_row]) + gutter;
            break;

        case KEY_DOWN:
            if (cursor_y < visible_lines - 1 && actual_row + 1 < MAX_ROWS) {
                cursor_y++;
            } else if (scroll_offset +
                visible_lines < MAX_ROWS) {
                scroll_offset++;
            }
            //그 줄의 뒤로 가도록 계산 
            actual_row = cursor_y + scroll_offset;
            cursor_x = strlen(editor_buf[actual_row]) + gutter;
            break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            pthread_mutex_lock(&mutex);
            if (cursor_x > gutter) {
                memmove(&editor_buf[actual_row][actual_col - 1],
                        &editor_buf[actual_row][actual_col],
                        strlen(&editor_buf[actual_row][actual_col]) + 1);
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
                    actual_row = cursor_y + scroll_offset;
                    cursor_x = prev_len + gutter;
                }
            }
            pthread_mutex_unlock(&mutex);
            break;

        case 10:  // Enter
            pthread_mutex_lock(&mutex);
            if(editor_buf[actual_row][actual_col - 1] == '{' && editor_buf[actual_row][actual_col] == '}'
                && actual_row < MAX_ROWS - 2){
                    for(int k = 0; k < 2; k++){
                        for (int i = MAX_ROWS - 2; i > actual_row; i--) {
                            strcpy(editor_buf[i + 1], editor_buf[i]);
                        }
                        strcpy(editor_buf[actual_row + 1], &editor_buf[actual_row][actual_col]);
                        editor_buf[actual_row][actual_col] = '\0';

                        if (cursor_y < visible_lines - 1) {
                            cursor_y++;
                        } else {
                            scroll_offset++;
                        }
                        cursor_x = gutter;
                        actual_col = 0;
                        actual_row = cursor_y + scroll_offset;
                    }
                    countBlock(actual_row, actual_col);
                    cursor_y--;
                    cursor_x = gutter;
                    actual_col = 0;
                    actual_row = cursor_y + scroll_offset;
                }
            else if (actual_row < MAX_ROWS - 1) {
                for (int i = MAX_ROWS - 2; i > actual_row; i--) {
                    strcpy(editor_buf[i + 1], editor_buf[i]);
                }
                strcpy(editor_buf[actual_row + 1], &editor_buf[actual_row][actual_col]);
                editor_buf[actual_row][actual_col] = '\0';

                if (cursor_y < visible_lines - 1) {
                    cursor_y++;
                } else {
                    scroll_offset++;
                }
                cursor_x = gutter;
                actual_col = 0;
                actual_row = cursor_y + scroll_offset;
            }
            countBlock(actual_row, actual_col);
            pthread_mutex_unlock(&mutex);
            break;

        default:
            if (ch == '\t') {
                // 탭 키 입력 시 공백 4칸 삽입
                tap(actual_row, actual_col);
            } else if (ch >= 32 && ch <= 126 && strlen(editor_buf[actual_row]) < MAX_COLS - 1) {
                pthread_mutex_lock(&mutex);
                if(ch == '{') {
                    memmove(&editor_buf[actual_row][actual_col + 2],
                            &editor_buf[actual_row][actual_col],
                            strlen(&editor_buf[actual_row][actual_col]) + 2);
                    editor_buf[actual_row][actual_col++] = '{';
                    editor_buf[actual_row][actual_col] = '}';
                    cursor_x += 1;
                }
                else if(ch == '(') {
                    memmove(&editor_buf[actual_row][actual_col + 2],
                            &editor_buf[actual_row][actual_col],
                            strlen(&editor_buf[actual_row][actual_col]) + 2);
                    editor_buf[actual_row][actual_col++] = '(';
                    editor_buf[actual_row][actual_col] = ')';
                    cursor_x += 1;
                }
                else if(ch == '\"' || ch == '\'') {
                    memmove(&editor_buf[actual_row][actual_col + 2],
                            &editor_buf[actual_row][actual_col],
                            strlen(&editor_buf[actual_row][actual_col]) + 2);
                    editor_buf[actual_row][actual_col++] = ch;
                    editor_buf[actual_row][actual_col] = ch;
                    cursor_x += 1;
                }
                else{
                    memmove(&editor_buf[actual_row][actual_col + 1],
                            &editor_buf[actual_row][actual_col],
                            strlen(&editor_buf[actual_row][actual_col]) + 1);
                            editor_buf[actual_row][actual_col] = ch;
                            cursor_x++;
                }
                pthread_mutex_unlock(&mutex);
            }
            break;
        
    }
    // 커서 안전 범위 보정
    /*int min_cursor_x = show_line_numbers ? 4 : 0;
    int max_y = getmaxy(editor_win) - 2;
    int max_x = getmaxx(editor_win) - 2;
    if (cursor_y >= max_y) cursor_y = max_y - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= max_x) cursor_x = max_x - 1;
    if (cursor_x < min_cursor_x) cursor_x = min_cursor_x;*/

   // 커서가 줄 끝을 넘지 않도록 보정
    int line_len = strlen(editor_buf[actual_row]);
    if (actual_col > line_len) cursor_x = line_len + gutter;
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
    const char** menu_items;
    int count = 0;

    if (menu_index == 0) {
        menu_items = file_menu;
        count = 4;
    } else if (menu_index == 1) {
        menu_items = build_menu;
        count = 2;
    } else if (menu_index == 2) {
        menu_items = option_menu;
        count = 3;
    } else {
        return;
    }

    for (int i = 0; i < count; i++) {
        attron(COLOR_PAIR(i == current_item ? 3 : 1));
        mvprintw(1 + i, x, "%-10s", menu_items[i]);
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
            case 10: // Enter
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);

                if (current_menu == 0) { // File
                    draw_status_bar(file_menu[current_item]);
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
                        save_current_file();
                        endwin();
                        exit(0);
                    }
                } else if (current_menu == 1) { // Build
                        const char* item = build_menu[current_item];
                        if (strcmp(item, "Run") == 0) {
                            run_in_gnome_terminal();
                        } else if (strcmp(item, "Link") == 0) {
                            char input[256] = "";
                            if (get_user_input("Enter link flags:", input)) {
                                strncpy(link_flags, input, sizeof(link_flags));
                                draw_status_bar("Link flags updated. Running...");
                                run_in_gnome_terminal();  // ← 바로 실행
                            } else {
                                draw_status_bar("Link input cancelled.");
                            }
                        }
                    } else if (current_menu == 2) { // Option
                    const char* item = option_menu[current_item];
                    if (strcmp(item, "NUM") == 0) {
                        show_line_numbers = !show_line_numbers;
                        draw_status_bar(show_line_numbers ? "Line numbers ON" : "Line numbers OFF");
                    } else if (strcmp(item, "SYN") == 0) {
                        show_syntax_highlight = !show_syntax_highlight;
                        draw_status_bar(show_syntax_highlight ? "Syntax highlight ON" : "Syntax highlight OFF");
                    } else if (strcmp(item, "{ }") == 0) {
                        hide_brackets = !hide_brackets;
                        draw_status_bar(hide_brackets ? "Brackets hidden" : "Brackets shown");
                    }
                    render_editor_buffer();
                } else if (current_menu == 3) { // Help
                    draw_status_bar("[Help] Not Updated Yet.");
                }

                current_menu = -1;
                return 1;
            case 27: // ESC
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
    // ncurses 내부 화면 크기 갱신
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    resizeterm(rows, cols);
    clear();  // stdscr 지우기

    // 에디터 윈도우 재생성
    draw_editor_window();

    // 전체 다시 그리기
    draw_menu_bar();
    render_editor_buffer();
    draw_status_bar("Window resized.");

    // 시그널 다시 등록
    signal(SIGWINCH, handle_resize);
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
    set_escdelay(25);  // 25ms로 줄임 (기본값은 보통 1000ms)
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
    init_pair(5, COLOR_YELLOW, -1);   // 문자열
    init_pair(6, COLOR_GREEN, -1);    // 주석
    init_pair(7, COLOR_RED, -1);      // 세미콜론
    init_pair(8, COLOR_MAGENTA, -1);  // 마젠타 키워드
    init_pair(9, COLOR_CYAN, -1);     // 시안 키워드

    draw_menu_bar();
    draw_editor_window();
    draw_editor_window();   // 테두리 먼저
    show_editor_logo();     // 로고 출력 후 키 입력 대기
    render_editor_buffer(); // 편집기 초기화
    
    // alarm for autosave
    signal(SIGALRM, autoSaveHandler);
    alarm(5);
    //draw_status_bar("Ctrl+L: menu | F10: exit | F5: compile & run | Ctrl+F: find");

    int ch;
    while ((ch = getch()) != KEY_F(10)) {
        // Alt+S 입력 감지: ESC → 's'
        if (ch == 27) { // ESC
            if (current_menu != -1) {
                // 드롭다운 메뉴가 열려 있다면 닫기만 하고 다음 키 안 받음
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);
                current_menu = -1;
                continue;
            }

            // Alt+조합키 감지용: 예) Alt+S
            int next = getch();  // 조합 키 처리
            if (next == 's' || next == 'S') {
                save_current_file();
                continue;
            }
            ungetch(next); // 아니면 다음 키를 되돌리기
        }
            if (ch == KEY_F(5)) {
            run_in_gnome_terminal();
            continue;
        }
        if (ch == 6) {
            search_text();
            continue;
        }

        if (!handle_menu_input(ch)) {
            handle_key_input(ch);
        }
    }


    endwin();
    return 0;
}

