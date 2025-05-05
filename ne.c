/*
<수정사항 1.3>
- 한 글자만 있는 라인에서, 백스페이스 입력시 지워지지 않던 버그 해결
- 화면 크기 변화시 cursor_i가 화면 바깥 쪽 아래에 박혀서 올라오지 않던 버그 해결
  근데 화면 크기 바꿀때마다 getch에 이상한 값이 저장되는 버그가 있음.
- 마우스 클릭시 그 클릭한 위치로 커서를 옮기는 기능 추가 
- ()나 {}, "", '' 같이 한 쪽만 입력해도 2개가 동시에 입력되는 기능을 추가
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <curses.h>
#include <dirent.h>
#include <sys/types.h>
#include <pthread.h>

//버퍼의 크기(필요에 따라 크기를 늘리거나 줄일 예정정)
#define MAX_LINE_LEN 100
#define MAX_LINES 256

//입력한 문자를 담을 버퍼
char buffer[MAX_LINES][MAX_LINE_LEN];

//라인 버퍼의 현재 위치를 알려주는 변수
int i = 0;
int j = 0;
//화면상의 커서의 위치를 알려주는 변수(cursor_j는 필요없다고 판단)
int cursor_i = 0;
//화면상에서 맨 첫줄에 출력할 라인 버퍼를 가리키는 변수
int first = 0;
//화면의 크기를 가져올 변수
int rows, cols;
//현재까지 작성한 총 줄의 갯수를 알려주는 변수
int totalLines = 0;
//복사한 문자열을 저장할 함수
char copiedStr[MAX_LINE_LEN];
//마우스 입력 이벤트를 감지하기 위한 변수
MEVENT event;

char currentFileName[256] = "";

void initProgram();
void saveFile();
void newFileOpen();
void backward();
void copy();
void paste();
void jump();
void notification();
void input();
void printScreen();
void exitProgram();

//화면의 위아래 스크롤 여부를 체크하는 함수
void checkUp();
void checkDown();

//화면의 크기가 변경될 경우 그에 맞춰서 스케일링 해주는 함수
void setWinSize();

void* saveFileThread(void*);
void autoSaveHandler();
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

const char* dataType[] = {"int", "unsigned", "signed", "double", "float",
"char", "short", "long", "void"};
const char* etc[] = {"sizeof", "typedef", "struct", "union", "enum", "extern", "static", "const"};
const char* memoryKeyword[] = {"malloc", "free", "calloc", "realloc"};
const char* controlKeyword[] = {"if", "else", "switch", "case", "default", "while", "for", "do", "continue", "break", "return"};

//프로그램 시작시 기본 설정 세팅
void initProgram(){
    initscr();
    cbreak();
    noecho(); //입력을 안 보여줌
    keypad(stdscr, TRUE); //Ctrl + q 같은 특수키 처리를 위한 세팅
    raw(); //이것도 특수키 처리를 위한 세팅
    getmaxyx(stdscr, rows, cols); //init시에 화면의 크기를 가져옴
    signal(SIGWINCH, setWinSize); //화면 크기 변화에 따른 signal세팅
    signal(SIGALRM, autoSaveHandler);
    alarm(5);
    mousemask(BUTTON1_PRESSED | BUTTON2_PRESSED, NULL); //마우스 클릭 이벤트 설정
}

void autoSaveHandler() {
    pthread_t t;
    
    if (pthread_create(&t, NULL, saveFileThread, NULL) != 0) 
        perror("can't create thread for autosave");
    else
        pthread_detach(t);

    alarm(5);
}

//현재 화면 사이즈를 조절했을 때, [A 같은 이상한 값이 버퍼에 들어가는 버그가 있음(해결 요망)
void setWinSize() {
	endwin();
	refresh();
	clear();
	getmaxyx(stdscr, rows, cols);
    flushinp();
    //화면을 줄였을 때, cursor_i가 화면 바깥의 값을 그대로 가리키는 버그가 있음
    //이를 고치기위해 cursor_i를 화면 안 으로 끄집어 올리는 코드를 작성
    if(cursor_i > rows - 1){
        cursor_i = rows - 1;
        i = first + rows - 1;
        j = strlen(buffer[i]);
    }
	printScreen();
}

void* saveFileThread(void* arg) {
    //이건 과제에서 저장하는 부분만 떼 온 것.
    int fd, count = 0;
    
    pthread_mutex_lock(&mutex);
    if((fd = creat(currentFileName, 0644)) == -1){
        perror("create error!");
        pthread_mutex_unlock(&mutex);
        pthread_exit(NULL);
    }

    while(count <= totalLines){
        //버퍼에서 한 줄식 뒤에 '\n'을 붙여서 파일에 저장
        copiedStr[0] = '\0';
        strcpy(copiedStr, buffer[count]);
        strcat(copiedStr, "\n\0");

        if(write(fd, copiedStr, strlen(copiedStr)) != strlen(copiedStr)){
            perror("wirte error!");
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
        count++;
    }
    close(fd);

    pthread_mutex_unlock(&mutex);
    return NULL;
}

//현재 작성중인 파일을 저장하는 함수
void saveFile(){
    //이건 과제에서 저장하는 부분만 떼 온 것.
    int fd, count = 0;
    
    pthread_mutex_lock(&mutex);
    if((fd = creat(currentFileName, 0644)) == -1){
        perror("create error!");
        pthread_mutex_unlock(&mutex);
        exit(1);
    }

    while(count <= totalLines){
        //버퍼에서 한 줄식 뒤에 '\n'을 붙여서 파일에 저장
        copiedStr[0] = '\0';
        strcpy(copiedStr, buffer[count]);
        strcat(copiedStr, "\n\0");

        if(write(fd, copiedStr, strlen(copiedStr)) != strlen(copiedStr)){
            perror("wirte error!");
            pthread_mutex_unlock(&mutex);
            exit(1);
        }
        count++;
    }

    close(fd);

    pthread_mutex_lock(&mutex);
}

//기존의 파일을 append모드로 열어서 뒤에서부터 수정할 수 있도록 하는 함수
void newFileOpen(char* filePath) {
    pthread_mutex_lock(&mutex);
    int idx = 0;
    int cur_row = 0;
    
	int fd = open(filePath, O_RDONLY);
	if (fd == -1) {
	    perror(filePath);
	    exit(1);
	} else {
		ssize_t bytes;
        char ch;
     
		while ((bytes = read(fd, &ch, 1)) > 0) {
            if (ch == '\n' || idx >= MAX_LINE_LEN - 1) {
                copiedStr[idx] = '\0';
                strcpy(buffer[cur_row++], copiedStr);
                idx = 0;
            } else {
                copiedStr[idx++] = ch;
            }
        }
    }
    if (idx > 0) {
        copiedStr[idx] = '\0';
        strcpy(buffer[cur_row++], copiedStr);
    }

    totalLines = cur_row - 1;
    close(fd);
    pthread_mutex_unlock(&mutex);
}

//Ctrl + Z 로 되돌리는 기능
void backward(){
	printScreen();	
}

//Ctrl + C 로 복사하는 기능
void copy(){
    pthread_mutex_lock(&mutex);
	strcpy(copiedStr, buffer[i]);
    pthread_mutex_unlock(&mutex);
}

//Ctrl + V 로 붙이는 기능
void paste() {
    pthread_mutex_lock(&mutex);

	strcpy(buffer[i], copiedStr);
	printScreen();

    pthread_mutex_unlock(&mutex);
}

//Ctrl + J 로 원하는 줄로 jump하는 기능
void jump() {
    
}

//동작의 성공, 오류, 특별한 알림을 출력하는 함수
void notification(char* message){

}

//문자의 입력, 지움, 줄바꿈 마다 그 업데이트된 내용을 출력
void printScreen(){
    pthread_mutex_lock(&mutex);
    int screen_i = 0;
    for(int buffer_i = first; buffer_i < first + rows; buffer_i++){
        move(screen_i, 0);
        clrtoeol();
        addstr(buffer[buffer_i]);
        screen_i++;
    }
    move(cursor_i, j);
    refresh();
    pthread_mutex_unlock(&mutex);
}

//현재의 buffer의 인덱스와 first를 비교해서 정확한 커서위치를 찾아주는 함수
//쉽게말해 i값에 변동이 있다? -> 무조건 이 함수들을 호출하면 됨 ㅇㅇ
void checkUp(){ //i값이 감소했을 때 호출(위로 스크롤)
    if(i < first && cursor_i == 0){
        first--;
    }
    else{
        cursor_i--;
    }
}
void checkDown(){ //i값이 증가했을 때 호출(아래로 스크롤)
    if(i > rows-1 && cursor_i == rows-1){
        first++;
    }
    else{
        cursor_i++;
    }
}

//내용을 입력받는 함수
void input(){
    while(1){
        printScreen();
        int ch = getch();
        
        //<특수키 입력 처리 부분>
        //Ctrl + Q 를 입력시 프로그램 종료
        if(ch == 17)
            break;
        //Ctrl + S 입력시 파일 저장
        else if(ch == 19)
            saveFile();
        else if (ch == 3)
			copy();
		else if (ch == 22)
			paste();
		else if (ch == 26)
			backward();

        pthread_mutex_lock(&mutex);
        //<방향키로 커서 조작하는 부분>
        if(ch == KEY_LEFT){
            if(j > 0){
                j--;
            }
            else if(i > 0 && j == 0){ //만약 맨 왼쪽에서 왼쪽키를 누르면 윗줄의 맨 뒤로 이동
                i--;
                j = strlen(buffer[i]);
                checkUp();
            }
        }
        else if(ch == KEY_RIGHT){
            if(j < strlen(buffer[i])){
                j++;
            }
            else if(i < totalLines && j == strlen(buffer[i])){ //만약 맨 오른쪽에서 오른쪽 키를 누르면 아랫줄의 맨 처음으로 이동
                i++;
                j = 0;
                checkDown();
            }
        }
        else if(ch == KEY_UP && i > 0){
            i--;
            j = strlen(buffer[i]);
            checkUp();
        }
        else if(ch == KEY_DOWN && i < totalLines){
            i++;
            j = strlen(buffer[i]);
            checkDown();
        }

        //<마우스로 커서 위치를 이동하는 부분>
        if(ch == KEY_MOUSE){
            if(getmouse(&event) == OK){
                if(event.bstate & BUTTON1_PRESSED && event.y <= totalLines){
                    cursor_i = event.y;
                    i = first + event.y;
                    if(event.x > strlen(buffer[i]))
                        j = strlen(buffer[i]);
                    else
                        j = event.x;
                }
            }
        }

        //<텍스트 입력 부분>
        //enter키 입력으로 줄 바꿈
        if(ch == '\n' || ch == KEY_ENTER){
            if(i == totalLines && j == strlen(buffer[i])){ //밑줄에 아무 문장도 존재하지 않고 맨 뒤에 있을경우우 그냥 newline처리
                i++;
                j = 0;
                totalLines++;
            }
            else { //엔터키를 누르면 뒤쪽 내용을 밑줄로 내림 
                totalLines++;
                for(int k = totalLines; k > i + 1; k--){
                    strcpy(buffer[k], buffer[k-1]);
                }
                strcpy(buffer[i+1], buffer[i] + j);
                buffer[i][j] = '\0';
                i++;
                j = 0;
            }
            checkDown();
        }
        //백스페이스 입력시 문자 지우기 처리
        else if((ch == 127 || ch == 8 || ch == KEY_BACKSPACE)){
            if(j > 0){
                j--;
                memmove(&buffer[i][j], &buffer[i][j+1], strlen(&buffer[i][j+1]) + 1);
            }
            else if(j == 0 && i > 0){ //만약 줄의 맨 앞에서 백스페이스를 누를시 윗 줄의 뒤에 이어 붙이는 기능
                i--;
                j = strlen(buffer[i]);
                strcat(buffer[i], buffer[i+1]); //윗줄 뒤에 붙이기
                for(int k = i + 1; k <= totalLines; k++){ //백스페이스한 줄 아랫놈들을 한칸씩 당김
                    strcpy(buffer[k], buffer[k+1]);
                }
                checkUp();
                totalLines--;
            }
        }
        //입력한 문자가 영어면 버퍼에 저장하고 출력
        else if (32 <= ch && ch <= 126){
            if(ch == '('){
                memmove(&buffer[i][j+2], &buffer[i][j], strlen(&buffer[i][j]) + 2);
                buffer[i][j++] = '(';
                buffer[i][j] = ')';
            }
            else if(ch == '{'){
                memmove(&buffer[i][j+2], &buffer[i][j], strlen(&buffer[i][j]) + 2);
                buffer[i][j++] = '{';
                buffer[i][j] = '}';
            }
            else if(ch == '\'' || ch == '\"'){
                memmove(&buffer[i][j+2], &buffer[i][j], strlen(&buffer[i][j]) + 2);
                buffer[i][j++] = ch;
                buffer[i][j] = ch;
            }
            else{
                memmove(&buffer[i][j+1], &buffer[i][j], strlen(&buffer[i][j]) + 1);
                buffer[i][j++] = ch;
            }
        }
        pthread_mutex_unlock(&mutex);
        //printScreen();
    }
}

//프로그램의 종료를 실행
void exitProgram(){
    saveFile(); //종료시 자동 저장
    endwin();
    exit(0);
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
        fprintf(stderr, "please command ./ne [filename]\n");
        exit(1);
    } else {
        DIR *dir_ptr;
        struct dirent *direntp;
        if ((dir_ptr = opendir("./")) == NULL)
            fprintf(stderr, "ne: cannot open %s\n", "./");
        else {
            while ((direntp = readdir(dir_ptr)) != NULL)
                if(strcmp(direntp->d_name, argv[1]) == 0)
                    newFileOpen(direntp->d_name);
            closedir(dir_ptr);
        }
    }

    strcpy(currentFileName, argv[1]);
    initProgram();
    input();
    exitProgram();
    pthread_mutex_destroy(&mutex);

    return 0;
}

