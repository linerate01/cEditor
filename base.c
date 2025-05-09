#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    
    if(argc == 1){
        printf("Usage: ./base [FileName]\n");
        return 0;
    }

    char* fileName = argv[1];
    char command[128] = "tmux new-session -s nice_editor ./ne ";
    strcat(command, fileName);

    system(command);

    return 0;
}