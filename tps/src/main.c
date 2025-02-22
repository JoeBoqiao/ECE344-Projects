#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int isNumber(char *str){
    while(*str){
        if((*str <= '9') && (*str >= '0')) str++;
        else return 0;
    }
    return 1;
}

char *make_path(char *pid){
    char *path = NULL;
    if(asprintf(&path, "/proc/%s/status", pid) != -1){
        return path;
    }
    return NULL;
}

int main() {
    printf("%5s %s\n","PID","CMD");
    DIR *dir = opendir("/proc");
    if(dir == NULL){
        perror("fail");
        return EXIT_FAILURE;
    }

    struct dirent *read_items;
    
    while((read_items = readdir(dir)) != NULL){

        if((read_items->d_type == DT_DIR) && (isNumber(read_items->d_name))){
            printf("%5s",read_items->d_name);
        } else{
            continue;
        }

        char *path = make_path(read_items->d_name);
        if(path){
            FILE *statusfile = fopen(path, "r");
            if(statusfile == NULL){
                perror("fail");
                return EXIT_FAILURE;
            }

            char line[500];
            char process_name[500];

            while(fgets(line,500,statusfile)){
                if(strncmp(line, "Name:", 5) == 0){
                    sscanf(line+6, "%499s", process_name);
                    break;
                }
                else{
                    perror("fail");
                    return EXIT_FAILURE;
                }
            }

            if(process_name[0]){
                printf(" %s\n", process_name);
            }

            fclose(statusfile);
        }
        free(path);

    }

    closedir(dir);
    return 0;
}
