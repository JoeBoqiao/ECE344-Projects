#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

struct ssp_node {
    pid_t pid;
    int ssp_id;
    char *name;
    int status;
};

struct ssp_node Node[256];
struct ssp_node ADNode[256];
int count = 0;
int next_id = 0;
int orphan_count = 0;  

void handle_signal() {
    
    int status;
    
  while (1) {
        pid_t orphan_pid = waitpid(-1, &status, WNOHANG); 
        if (orphan_pid <= 0) {
            break;
        }

        bool found = false;
        for (int i = 0; i < count; i++) {
            if (Node[i].pid == orphan_pid) {
                found = true;
                if (WIFEXITED(status)) {
                    Node[i].status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    Node[i].status = WTERMSIG(status) + 128;
                }
                break;
            }
        }

        if (!found && orphan_count < 256) {  
            ADNode[orphan_count].pid = orphan_pid;
            ADNode[orphan_count].ssp_id = next_id++; 
            ADNode[orphan_count].name = strdup("<unknown>");  
            if (WIFEXITED(status)) {
                ADNode[orphan_count].status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                ADNode[orphan_count].status = WTERMSIG(status) + 128;
            }
            orphan_count++;
        }
    }
}

void register_signal(int signum)
{
  struct sigaction new_action = {0};
  sigemptyset(&new_action.sa_mask);
  new_action.sa_handler = handle_signal;
  new_action.sa_flags = SA_RESTART;
  if (sigaction(signum, &new_action, NULL) == -1) {
    int err = errno;
    perror("sigaction");
    exit(err);
  }
}


void ssp_init() {
    prctl(PR_SET_CHILD_SUBREAPER, 1);

    for(int i = 0; i < 256; i++){
        Node[i].pid = 10;
        Node[i].ssp_id = -1;
        if (Node[i].name != NULL) {
            free(Node[i].name);  
            Node[i].name = NULL;
        }
        Node[i].status = -1;
    }
    
    count = 0;
    next_id = 0;

    register_signal(SIGCHLD);
    signal(SIGPIPE, SIG_IGN);
}



int ssp_create(char *const *argv, int fd0, int fd1, int fd2) {
    if(count > 256){
        fprintf(stderr, "Maximum process limit reached");
        return -1;
    }

    pid_t pid = fork();

    if(pid == 0){
        if (fd0 != -1) dup2(fd0, 0);   
        if (fd1 != -1) dup2(fd1, 1);  
        if (fd2 != -1) dup2(fd2, 2);  

        DIR *dir = opendir("/proc/self/fd");
        struct dirent* entry;
        if(!dir){
        perror("opendir");
        return -1;
        }

        while((entry = readdir(dir)) != NULL){
            if(entry->d_type == DT_LNK){
                int fd = atoi(entry -> d_name);

                if (fd != fd0 && fd != fd1 && fd != fd2 && fd > 2){
                    close(fd);
                }
            }
        }
        closedir(dir);  

        execvp(argv[0], argv);

        perror("execvp failed");
        _exit(errno);
    }
    else if(pid >0){
        // printf("Created process %d with ssp_id %d\n", pid, next_id);
        Node[count].pid = pid;
        Node[count].ssp_id = next_id;
        Node[count].name = strdup(argv[0]);
        if (Node[count].name == NULL) {
            perror("strdup failed");
            exit(EXIT_FAILURE);
        }
        Node[count].status = -1;
        
        next_id++;
        count++;
        return next_id - 1;
    }
    else {
        perror("fork failed");
        return -1;
    }
}

int ssp_get_status(int ssp_id) {
    for (int i = 0; i < count; i++) {
        if (Node[i].ssp_id == ssp_id) {
            if (Node[i].status == -1) {
                int status;
                pid_t result = waitpid(Node[i].pid, &status, WNOHANG);  
                if (result == 0) {  
                    return -1;
                } else if (result > 0) {
                    if (WIFEXITED(status)) {
                        Node[i].status = WEXITSTATUS(status);  
                    } else if (WIFSIGNALED(status)) {
                        Node[i].status = WTERMSIG(status) + 128;  
                    }
                } else {
                    perror("waitpid failed");
                    return -1;
                }
            }
            return Node[i].status;
        }
    }
    fprintf(stderr, "Error: Process with ssp_id %d not found.\n", ssp_id);
    return -1;
}

void ssp_send_signal(int ssp_id, int signum) {
    for(int i = 0; i < count; i++){
        if(Node[i].ssp_id == ssp_id){

            int status;
            pid_t result = waitpid(Node[i].pid, &status, WNOHANG);

            if(result == 0){
                if(kill(Node[i].pid, signum)==0){
                    // printf("Signal %d sent to process %d (ssp_id: %d, name: %s)\n",
                    //    signum, ssp_id, ssp_id, Node[i].name);
                }
                else {
                    perror("Failed to send signal");
                }
            }
            return;
        }
    }
    fprintf(stderr, "Error: Process with ssp_id %d not found.\n", ssp_id);
}

void ssp_wait() {
    int status;

    for(int i = 0; i < count; i++){
        if(Node[i].status == -1){
            pid_t result = waitpid(Node[i].pid, &status, 0);

            if (result == -1) {
                perror("waitpid failed");
                continue;
            }

            if (WIFEXITED(status)) { 
                Node[i].status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) { 
                Node[i].status = WTERMSIG(status) + 128;
            } 
            
            if (Node[i].status < 0 || Node[i].status > 255) {
                fprintf(stderr, "Error: Process %d (ssp_id: %d) exited with invalid status: %d\n",
                        Node[i].pid, Node[i].ssp_id, Node[i].status);
            }
        }
        
        
    }

    


}

void ssp_print() {
    int longest_name = 0;
    
    for(int i = 0; i < count; i++){
        int len = strlen(Node[i].name);
        if (len > longest_name) {
            longest_name = len;
        }
    }

    if (longest_name <= 3) {
        longest_name = 3;  
    }

    if (longest_name <= 3) {
        longest_name = 3;  
    }

    for (int i = 0; i < orphan_count; i++) {
        int len = strlen(ADNode[i].name);
        if (len > longest_name) {
            longest_name = len;
        }
    }

    printf("%7s %-*s %s\n", "PID", longest_name, "CMD", "STATUS");

    for (int i = 0; i < count; i++) {

        int status = Node[i].status;
        printf("%7d %-*s %d\n", Node[i].pid, longest_name, Node[i].name, status);
        
        
    }

    for (int i = 0; i < orphan_count; i++) {
        printf("%7d %-*s %d\n", ADNode[i].pid, longest_name, ADNode[i].name, ADNode[i].status);
    }
}
