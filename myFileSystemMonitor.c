// gcc -g -finstrument-functions myFileSystemMonitor.c -o myFileSystemMonitor -lpthread -lcli
// ./myFileSystemMonitor -d /../..

// netcat -l -u -p 10000
// telnet 0 10000

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>
#include <execinfo.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <getopt.h>
#include <semaphore.h>
#include <libcli.h>

#define MAX_EVENT_MONITOR 2048
#define NAME_LEN 32
#define MONITOR_EVENT_SIZE (sizeof(struct inotify_event))
#define BUFFER_LEN MAX_EVENT_MONITOR *(MONITOR_EVENT_SIZE + NAME_LEN)
#define BT_BUF_SIZE 2048
#define PORT 10000

int fd, sock, tsock, watch_desc, flag = 0, x=0, on = 1, BT_flag=0;
FILE *ptrFile;
struct cli_def *cli;
struct sockaddr_in addr;
char BT_buffer[BT_BUF_SIZE];
sem_t semaphore;

void backtracefunc() {                   
	int nptrs=0;
	void *buffer[BT_BUF_SIZE];
	char **strings;
	char BT_string[100];

    for (int k = 0; k < BT_BUF_SIZE; k++) {
        BT_buffer[k] = 0;
        buffer[k] = 0;
    }
	for (int k = 0; k < 100; k++) {
        BT_string[k] = 0;
    }
		
	nptrs = backtrace(buffer, BT_BUF_SIZE);

	sprintf( BT_string, "backtrace() returned %d addresses\n", nptrs);
	strcat(BT_buffer, BT_string);

	strings = backtrace_symbols(buffer, nptrs);
	if (strings == NULL) {
		perror("backtrace_symbols");
		exit(EXIT_FAILURE);
	}	
	
	for (int j = 0; j < nptrs; j++) {
		strcat(BT_buffer, strings[j]);
		strcat(BT_buffer, "\n");
	}
	free(strings);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *this_fn, void *call_site) {
    if(BT_flag==1) {
		BT_flag = 0;
    	backtracefunc();
       	sem_post(&semaphore);
    }
}

int cmd_backtrace(struct cli_def *cli, const char *command, char *argv[], int argc) {
    BT_flag = 1;
	backtracefunc();	
	sem_wait(&semaphore);
	cli_print(cli, BT_buffer);
    return CLI_OK;
}

void indexClose() {
    fprintf(ptrFile, "</BODY>\n</HTML>");
    fclose(ptrFile);
}

void indexOpen() {
    ptrFile = fopen("index.html", "r");
    if (!ptrFile) {
        ptrFile = fopen("index.html", "w");
        if (!ptrFile)
            printf("index.html not open\n");
        fprintf(ptrFile, "<HTML>\n");
        fprintf(ptrFile, "<HEAD> <TITLE> Unix Project </TITLE> </HEAD>\n");
        fprintf(ptrFile, "<BODY>\n");

        indexClose();
    }
    else
        fclose(ptrFile);

    ptrFile = fopen("index.html", "a");
    if (!ptrFile)
        printf("index.html not open\n");

    fseek(ptrFile, -15, SEEK_END);
    ftruncate(fileno(ptrFile), ftello(ptrFile));
}

void cleanExit() {
    indexClose();
    inotify_rm_watch(fd, watch_desc);
    close(fd);
    close(sock);
    close(tsock);
    cli_done(cli);
    exit(EXIT_SUCCESS);
}

void telnetFunc() {
    while ((x = accept(tsock, NULL, 0))) {
        printf(" * accepted connection from %s\n", inet_ntoa(addr.sin_addr));
	    close(tsock);
	    cli_loop(cli, x);
	}
    pthread_exit(0);
}

void fileSystemMonitor() {
    char buffer[BUFFER_LEN];
    int nsent;
    time_t my_time;
    struct tm* timeinfo;
    time(&my_time);
    int i=0;
    char access[50];

    while(1) {
        i=0;
        int total_read = read(fd, buffer, BUFFER_LEN);
        if(total_read < 0)
            printf("read error\n");

        while(i < total_read) {
            struct inotify_event *event = (struct inotify_event*)&buffer[i];
            timeinfo = localtime(&my_time);
            time_t result = time(NULL);

            for (int j = 0; j < 50; j++)
                access[j] = 0;
            
            if(event->len) {
                if(event->mask & IN_CREATE) {
                    strcpy(access, "WRITE");
                    if(event->mask & IN_ISDIR)
                        printf("Directory \"%s\" was created\n", event->name);
                    else 
                        printf("File \"%s\" was created\n", event->name);
                }
                else if(event->mask & IN_MODIFY) {
                    strcpy(access, "WRITE");
                    if(event->mask & IN_ISDIR)
                        printf("Directory \"%s\" was modified\n", event->name);
                    else 
                        printf("File \"%s\" was modified\n", event->name);
                }
                else if(event->mask & IN_DELETE) {
                    strcpy(access, "NO_WRITE");
                    if(event->mask & IN_ISDIR)
                        printf("Directory \"%s\" was deleted\n", event->name);
                    else 
                        printf("File \"%s\" was deleted\n", event->name);
                }
                else if(event->mask & IN_MOVE) {
                    strcpy(access, "NO_WRITE");
                    if(event->mask & IN_ISDIR)
                        printf("Directory \"%s\" was moved\n", event->name);
                    else 
                        printf("File \"%s\" was moved\n", event->name);
                }

                i+= MONITOR_EVENT_SIZE + event->len;

                send(sock, "\nFILE ACCESSED: ", 16, 0);
                send(sock, event->name, event->len, 0);   
                send(sock, "\nACCESSED: ", 10, 0); 
                send(sock, access, 50, 0); 
                send(sock, "\nTIME OF ACCESS: ", 18, 0);  
                send(sock, ctime(&result), 100, 0);
                send(sock, "\n", 2, 0);

                indexOpen();

                fprintf(ptrFile, "<p>FILE ACCESSED: %s </p>\n", event->name);
                fprintf(ptrFile, "<p>ACCESS: %s </p>\n", access);
                fprintf(ptrFile, "<p>TIME OF ACCESS: %d/%d/%d : %d:%d </p>\n", timeinfo->tm_mday, timeinfo->tm_mon+1, timeinfo->tm_year+1900, timeinfo->tm_hour, timeinfo->tm_min);
                fprintf(ptrFile, "<br><br>\n\n");

                indexClose();
            } 
        }    
    }
    cleanExit();
}

int main(int argc, char const *argv[]) {

    signal(SIGABRT, cleanExit);
    signal(SIGINT, cleanExit);
	
	cli = cli_init();
	cli_register_command(cli, NULL, "backtrace", cmd_backtrace, PRIVILEGE_UNPRIVILEGED, MODE_EXEC, "backtrace command");

	int opt = 0;
    extern char *optarg;
    char dir_name[200];
    long IP_ADDR = 0;
    char* curr_dir[PATH_MAX];
    getcwd(curr_dir, sizeof(curr_dir));

    while( (opt = getopt(argc, argv, "d:i:")) != -1) {
        switch (opt) {
            case 'd': {                                             // directory
                if(strcmp(optarg, curr_dir) != 0)
                    strcpy(dir_name, optarg);
                else
                    printf("directory not good\n");
                break;
            }
            default:
                break;
        }
    }

    struct sockaddr_in s={0};
    s.sin_family = AF_INET;
    s.sin_port = htons(PORT);
    s.sin_addr.s_addr = htonl(INADDR_ANY);

    memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    fd = inotify_init();
    if(fd<0) 
        printf("notify not initialized\n");

    watch_desc = inotify_add_watch(fd, dir_name, IN_CREATE | IN_MODIFY | IN_MOVE | IN_DELETE );
    if(watch_desc == -1)
        printf("coule not add watch\n");

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(connect(sock, (struct sockaddr*)&s, sizeof(s)) < 0) {
        printf("socket connection error\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("successfully connected\n");

    if ((tsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return 1;
	}
	setsockopt(tsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (bind(tsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}
	if (listen(tsock, 50) < 0) {
		perror("listen");
		return 1;
	}
	printf("Listening on port %d\n", PORT);

    indexOpen();
    indexClose();

    pthread_t id;
    int ret;
    ret = pthread_create(&id, NULL, &telnetFunc, NULL);
    if(ret != 0)
        printf("thread not created\n");

    fileSystemMonitor();
    
    cleanExit();
    return(EXIT_SUCCESS);
}                                     