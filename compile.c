#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]){

    if(argc == 1){
        return 0;
    }
    
    char* fileName_c = argv[1];
    char fileName[128];
    strncpy(fileName, fileName_c, strlen(fileName_c) - 2);
    char command[128] = "gcc -o ";

    //파일을 gcc -o fileName fileName.c로 만드는 부분
    strcat(command, fileName);
    strcat(command, " ");
    strcat(command, fileName_c);

    //컴파일 실행
    int res = system(command);
    if(res == 0){
        // ./fileName 실행 커맨드를 만들고 실행
        char runCommand[128] = "./";
        strcat(runCommand, fileName);
        system(runCommand);
    }
    
    system("tmux select-pane -L");
    char c = getchar();
    return 0;
}