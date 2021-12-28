#include "threadpool.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/dir.h> 
#include <ctype.h>
#include <sys/stat.h>
#define _GNU_SOURCE
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define MAX4 4000
#define MAX1 1000

void notfound(int cli_fd);
void found(int cli_fd, char* path);
void notfound(int cli_fd);
void notsup(int cli_fd);
void dircont(char *f, char *src,int cli_fd);
void badreq(int cli_fd);
void forbidden(int cli_fd);
void internal(int cli_fd);
void file(int cli_fd, char *path, char *src,long size);

char *get_mime_type(char *name){
    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}
void checkANDgo(char *dst, char *src,int cli_fd){//Need to check permissions
    char copy[MAX4];
    strncpy(copy, dst, MAX4*sizeof(char));
    char *pLast = strrchr(dst, '/');
    ++pLast;
    char *second_last = NULL;
    int type = 0;//File
    if(*pLast == 0){
        type = 1;//Directory
        second_last = pLast;
        --second_last;
        --second_last;
        while(*second_last != '/')
            --second_last;
        ++second_last;
    }
    char *token = strtok(dst, "/");
    while(token != NULL){
        struct stat buf;
        if(stat(token, &buf) < 0){
            notfound(cli_fd);
            chdir(src);
            return;//Fail
        }
        if(token == second_last && type == 1){//DIR
            if(!S_ISDIR(buf.st_mode)){
                notfound(cli_fd);
                chdir(src);
                return;
            }
            DIR *dp = opendir(token);
            struct dirent *entry;
            struct stat statbuf;
            if(!dp) {
                internal(cli_fd);
                chdir(src);
                return;
            }
            chdir(token);
            while((entry = readdir(dp)) != NULL) {
                lstat(entry->d_name,&statbuf);
                if(strcmp(".",entry->d_name) == 0 || strcmp("..",entry->d_name) == 0)
                    continue;
                if(strcmp("index.html", entry->d_name) == 0){
                    if((statbuf.st_mode && S_IROTH) && (buf.st_mode && S_IXOTH)){
                        file(cli_fd, "index.html", src, statbuf.st_size);
                        closedir(dp);
                        chdir(src);
                        return;
                    }
                    forbidden(cli_fd);
                    closedir(dp);
                    chdir(src);
                    return;
                }
            }
            //Didn't find index.html
            closedir(dp);
            chdir(src);
            dircont(copy, src, cli_fd);
            return;
        }
        if(type == 0 && token == pLast){//File
            if(!(buf.st_mode && S_IROTH)){
                forbidden(cli_fd);
                chdir(src);
                return;
            }
            file(cli_fd, token, src, buf.st_size);
            chdir(src);
            return;
        }        
        if(!(buf.st_mode && S_IXOTH)){
            forbidden(cli_fd);
            chdir(src);
            return;
        }
        int fail = 0;
        fail = chdir(token);
        if(fail != 0){
            notfound(cli_fd);
            chdir(src);
            return;
        }
        token = strtok(NULL, "/");
    }
    chdir(src);
}
void dircont(char *f, char *src,int cli_fd){//Directory visualization
    chdir(src);
    char *p = strrchr(f, '/');
    char *p2 = p;
    
    char *token = f;
    char copy[MAX4];
    strncpy(copy,f, MAX4*sizeof(char));
    if(p && strcmp(f, "/") != 0){
        ++p;
        if(*p == 0){
            --p2;
            while(*p2 != '/')
                --p2;
            ++p2;
        }
        token = strtok(f, "/");
        int fail = 0;
        struct stat buf;
        while(token != NULL){
            if(stat(token, &buf) < 0){
                internal(cli_fd);
                return;//Fail
            }
            
            if(token == p || token == p2)
                break;
            if(!(buf.st_mode && S_IXOTH)){
                forbidden(cli_fd);
                return;
            }
            fail = chdir(token);
            if(fail != 0){
                notfound(cli_fd);
                return;
            }
            token = strtok(NULL, "/");
        }
        fail = chdir(token);
        if(fail != 0){
            notfound(cli_fd);
            return;
        }
        if(!(buf.st_mode && S_IROTH)){
            forbidden(cli_fd);
            return;
        }
        if(!S_ISDIR(buf.st_mode)){
            badreq(cli_fd);
            return;
        }

    }
    time_t now;
    char entity[MAX1];
    char *buf1 = "HTTP/1.1 200 OK\r\nServer: webserver/1.0\r\nDate: ";
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf2 = "\r\nContent-Type: text/html\r\nContent-Length: %ld";
    char *buf3 = "\r\nLast-Modified: ";
    char *buf4 = "\r\nConnection: close\r\n\r\n";
    long len = 0;
    char *buf5 = "<HTML>\r\n<HEAD><TITLE>Index of ";
    len += strlen(buf5);
    char *bufpath = copy;
    len += strlen(bufpath);
    char *buf6 = "</TITLE></HEAD>\r\n<BODY>\r\n<H4>Index of ";
    len += strlen(buf6);
    len += strlen(bufpath);
    char *buf7 = "</H4>\r\n<table CELLSPACING=8>\r\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n";
    len += strlen(buf7);
    
    char *fin = "\r\n</table>\r\n\r\n<HR>\r\n\r\n<address>webserver/1.0</ADDRESS>\r\n\r\n</BODY></HTML>\r\n";
    len += strlen(fin);
    for(int i = 0; i < 2; i++){
        char s[100];
        getcwd(s, 100);
        DIR *dp;
        if(strcmp(f, "/") != 0){
            chdir("..");
            getcwd(s, 100);
            dp = opendir(token);
        }
        else
            dp = opendir(src);
            
        if(!dp) {
            internal(cli_fd);
            chdir(src);
            return;
        }
        char LMbuf[128];
        struct dirent *entry;
        struct stat statbuf;
        
        if(stat(token, &statbuf) < 0){
            internal(cli_fd);
            chdir(src);
            return;
        }
        strftime(LMbuf, sizeof(LMbuf), RFC1123FMT, gmtime(&statbuf.st_mtime));
        
        
        if(i != 0){
            write(cli_fd, buf1, strlen(buf1));
            write(cli_fd, timebuf, strlen(timebuf));
            memset(entity, 0, MAX1*sizeof(char));
            sprintf(entity, buf2, len);
            write(cli_fd, entity, strlen(entity));
            write(cli_fd, buf3, strlen(buf3));
            write(cli_fd, LMbuf, strlen(LMbuf));
            write(cli_fd, buf4, strlen(buf4));
            write(cli_fd, buf5, strlen(buf5));
            write(cli_fd, bufpath, strlen(bufpath));
            write(cli_fd, buf6, strlen(buf6));
            write(cli_fd, bufpath, strlen(bufpath));
            write(cli_fd, buf7, strlen(buf7));
        }
        int fail = 0;
        if(strcmp(f, "/") != 0)
            fail = chdir(token);
        if(fail != 0){
            notfound(cli_fd);
            chdir(src);
            return;
        }
        while((entry = readdir(dp)) != NULL) {
            lstat(entry->d_name,&statbuf);
            char *e1 = "<tr>\r\n<td><A HREF= \"%s\">%s</A></td><td>";
            char *name = entry->d_name;
            char tempT[128];
            strftime(tempT, sizeof(tempT), RFC1123FMT, gmtime(&statbuf.st_mtime));
            char *e2 = "</td>\r\n<td>";
            char *esize = "%ld";
            long size = statbuf.st_size;
            char *e3 = "</td>\r\n</tr>\r\n\r\n";
            if(i == 0){
                memset(entity, 0, MAX1*sizeof(char));
                sprintf(entity, e1, name, name);
                len += strlen(entity);
                len += strlen(e2);
                memset(entity, 0, MAX1*sizeof(char));
                sprintf(entity, esize, size);
                if(statbuf.st_mode && S_IXOTH){
                    len += strlen(tempT);
                    len += strlen(entity);
                }
                len += strlen(e3);
            }else{
                memset(entity, 0, MAX1*sizeof(char));
                sprintf(entity, e1, name, name);
                write(cli_fd, entity, strlen(entity));
                memset(entity, 0, MAX1*sizeof(char));
                sprintf(entity, esize, size);
                if(statbuf.st_mode && S_IXOTH){
                    write(cli_fd, tempT, strlen(tempT));
                    write(cli_fd, e2, strlen(e2));
                    write(cli_fd, entity, strlen(entity));
                }else
                    write(cli_fd, e2, strlen(e2));
                
                write(cli_fd, e3, strlen(e3));
            }
        }
        closedir(dp);
    }
    write(cli_fd, fin, strlen(fin));
    close(cli_fd);
    chdir(src);
}
void notsup(int cli_fd){//Method other than GET
    char *buf1 = "HTTP/1.1 501 Not supported\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf3 = "\r\nContent-Type: text/html\r\nContent-Length: 129\r\nConnection: close\r\n\r\n";
    char *buf4 = "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n<BODY><H4>501 Not supported</H4>\r\nMethod is not supported.\r\n</BODY></HTML>\r\n";
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, buf3, strlen(buf3));
    write(cli_fd, buf4, strlen(buf4));
    close(cli_fd);
}
void badreq(int cli_fd){//GET but syntax err
    char *buf1 = "HTTP/1.1 400 Bad Request\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf3 = "\r\nContent-Type: text/html\r\nContent-Length: 113\r\nConnection: close\r\n\r\n";
    char *buf4 = "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\r\n<BODY><H4>400 Bad request</H4>\r\nBad Request.\r\n</BODY></HTML>\r\n";
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, buf3, strlen(buf3));
    write(cli_fd, buf4, strlen(buf4));
    close(cli_fd);
}
void notfound(int cli_fd){//Path doesn't exist
    char *buf1 = "HTTP/1.1 404 Not found\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf3 = "\r\nContent-Type: text/html\r\nContent-Length: 112\r\nConnection: close\r\n\r\n";
    char *buf4 = "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n<BODY><H4>404 Not Found</H4>\r\nFile not found.\r\n</BODY></HTML>\r\n";    
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, buf3, strlen(buf3));
    write(cli_fd, buf4, strlen(buf4));
    close(cli_fd);
}
void found(int cli_fd, char* path){//Directory but forgot / in the end
    char *buf1 = "HTTP/1.1 302 Found\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *loc = "\r\nLocation: ";
    char *buf3 = "\r\nContent-Type: text/html\r\nContent-Length: 123\r\nConnection: close\r\n\r\n";
    char *buf4 = "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\r\n<BODY><H4>302 Found</H4>\r\nDirectories must end with a slash.\r\n</BODY></HTML>\r\n";
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, loc, strlen(loc));
    write(cli_fd, path, strlen(path));
    write(cli_fd, "/", 1);
    write(cli_fd, buf3, strlen(buf3));
    write(cli_fd, buf4, strlen(buf4));
    close(cli_fd);
}
void forbidden(int cli_fd){//Paths if file but no perms or not reg
    char *buf1 = "HTTP/1.1 403 Forbidden\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf3 = "\r\nContent-Type: text/html\r\nContent-Length: 111\r\nConnection: close\r\n\r\n";
    char *buf4 = "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n<BODY><H4>403 Forbidden</H4>\r\nAccess denied.\r\n</BODY></HTML>\r\n";
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, buf3, strlen(buf3));
    write(cli_fd, buf4, strlen(buf4));
    close(cli_fd);
}
void internal(int cli_fd){//Internal fail
    char *buf1 = "HTTP/1.1 500 Internal Server Error\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf3 = "\r\nContent-Type: text/html\r\nContent-Length: 144\r\nConnection: close\r\n\r\n";
    char *buf4 = "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\r\n<BODY><H4>500 Internal Server Error</H4>\r\nSome server side error.\r\n</BODY></HTML>\r\n";
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, buf3, strlen(buf3));
    write(cli_fd, buf4, strlen(buf4));
    close(cli_fd);
}
void file(int cli_fd, char* path, char *src,long size){
    FILE* fd = fopen(path, "r");
    int r = 0;
    unsigned char wbuff[MAX4];
    char *buf1 = "HTTP/1.1 200 OK\r\nServer: webserver/1.0\r\nDate: ";
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    char *buf3 = "\r\nContent-Type: ";
    char *type = get_mime_type(path);
    char contentL[100];
    sprintf(contentL, "\r\nContent-Length: %ld", size);
    char *buf4 = "\r\nConnection: close\r\n\r\n";
    write(cli_fd, buf1, strlen(buf1));
    write(cli_fd, timebuf, strlen(timebuf));
    write(cli_fd, buf3, strlen(buf3));
    if(type)
        write(cli_fd, type, strlen(type));
    write(cli_fd, contentL, strlen(contentL));
    write(cli_fd, buf4, strlen(buf4));
    
    while((r = fread(wbuff, sizeof(unsigned char), MAX4, fd)) != 0){
        if(r == -1){
            internal(cli_fd);
            return;
        }
        if(r == 0)
            break;
        write(cli_fd, wbuff, sizeof(unsigned char)* r);
        memset(wbuff, 0, MAX4*sizeof(unsigned char));
    }
    fclose(fd);
    chdir(src);
    close(cli_fd);
}
void f(void* x){
    int len = 0, k = 1, cli_fd = *(int*)x;
    int br_flag = -1;
    char content[MAX4];
    char server_path[MAX4];
    char req_path[MAX4];
    if(getcwd(server_path, MAX4) == NULL){
        internal(cli_fd);//500
        return;
    }

    len = read(cli_fd, content ,MAX4*sizeof(char));
    if(len < 0){
        internal(cli_fd);//500
        return;
    }
    if(len == 0){
        close(cli_fd);
        return;
    }
    int j = 0;
    while(j < len){
        if((j + 1) < len && content[j] == '\r' && content[j + 1] == '\n'){
            content[j + 2] = 0;//NULL
            len = j + 2;
            break;
        }
        ++j;
    }
    memset(content + len, 0, sizeof(char)*(MAX4 - len));
    char* token = strtok(content, " ");
    if(strcmp(token, "GET") != 0){
        notsup(cli_fd);
        return;
    }

    while(token != NULL && br_flag != 0) {
        token = strtok(NULL, " ");
        ++k;
        if(k == 4 && !token)
            break;
        if(k == 5){
            br_flag = 0;
            break;
        }
        
        if(k == 2){
            if(token[0] != '/'){
                br_flag = 0;
                break;
            }
            int t_len = strlen(token);
            for(int i = 0; i < t_len; i++){
                if((i + 1 < t_len) && token[i] == '/' && token[i + 1] == '/'){
                    br_flag = 0;
                    break;
                }
            }
        }
        if(k == 3 && strcmp(token, "HTTP/1.1\r\n") != 0 && strcmp(token, "HTTP/1.0\r\n") != 0)
            br_flag = 0;
        if(br_flag == 0)
            break;
    }
    if(br_flag == 0){
        badreq(cli_fd);
        return;
    }
    k = 0;
    char *p = strstr(content + 4, "/");
    while(*p != 0){
        req_path[k++] = *p;
        ++p;
    }
    req_path[k] = 0;//Null
    if(strcmp(req_path, "/favicon.ico") == 0){
        notfound(cli_fd);
        return;
    }
    
    if(k == 1){
        dircont("/", server_path,cli_fd);
        chdir(server_path);
        return;
    }
    p = strstr(req_path, "/");
    char *pE = strrchr(req_path, '/');
    int ind = (int)(pE - p);
    chdir(server_path);
    if(ind != (strlen(req_path) - 1)){//Forgot / in the end OR wanting file
        char copy[MAX4];
        memset(copy, 0, sizeof(char)*MAX4);
        strncpy(copy, req_path, sizeof(char)*k);
        struct stat buf;
        char *last = strrchr(copy, '/');
        ++last;
        char *token = strtok(copy, "/");
        int fail = 0;
        
        while (token != NULL){   
            if(token == last)
                break;
            
            fail = chdir(token);
            if(fail != 0){
                notfound(cli_fd);
                chdir(server_path);
                return;
            }
            token = strtok(NULL, "/");
        }
        if(access(token, F_OK ) != 0 ){
            notfound(cli_fd);
            chdir(server_path);
            return;
        }
        if(stat(token, &buf) < 0){
            internal(cli_fd);
            chdir(server_path);
            return;
        }
        if(S_ISDIR(buf.st_mode)){
            found(cli_fd, req_path);
            chdir(server_path);
            return;
        }
        else if(!S_ISREG(buf.st_mode)){
            forbidden(cli_fd);
            chdir(server_path);
            return;
        }

    }
    chdir(server_path);
    checkANDgo(req_path, server_path, cli_fd);
}

int main(int argc, char* argv[]){
    // int argc = 4;
    // char argv[4][30] = {"server", "1080", "3", "10"};
    if(argc != 4){
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(1);
    }
    for(int i = 0; i < strlen(argv[1]); i++)
        if(isdigit(argv[1][i]) == 0){
           printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
           exit(1); 
        }
    for(int i = 0; i < strlen(argv[2]); i++)
        if(isdigit(argv[2][i]) == 0){
           printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
           exit(1); 
        }
    for(int i = 0; i < strlen(argv[3]); i++)
        if(isdigit(argv[3][i]) == 0){
           printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
           exit(1); 
        }
    int port = atoi(argv[1]);
    if(port < 1){
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(1);
    }
    int p_size = atoi(argv[2]);
    int max_req = atoi(argv[3]);
    threadpool* t = create_threadpool(p_size);
    int *cli_arr = (int*)malloc(sizeof(int)*max_req);
    if(!cli_arr){
        destroy_threadpool(t);
        perror("Cannot allocate memory.\n");
    }
    memset(cli_arr, 0, sizeof(int)*max_req);
    int sockfd;
    struct sockaddr_in servaddr;
    sockfd = socket(PF_INET, SOCK_STREAM, 0); 
    if(sockfd == -1){
        destroy_threadpool(t);
        free(cli_arr);
        perror("Socket failed.\n");
    }
    bzero(&servaddr, sizeof(servaddr)); 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(port);
    socklen_t serv_len = sizeof(servaddr); 
    if(bind(sockfd, (const struct sockaddr*)&servaddr, serv_len) < 0){
        destroy_threadpool(t);
        free(cli_arr);
        perror("Bind failed.\n");
    }
    
    if((listen(sockfd, max_req)) < 0){
        destroy_threadpool(t);
        free(cli_arr);
        perror("Listen failed.\n");
    }
    printf("Listening on %d...\n", port); 

    for(int i = 0; i < max_req; i++){
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        cli_arr[i] = accept(sockfd, (struct sockaddr*)&cli, &len); 
        if(cli_arr[i] < 0){
            destroy_threadpool(t);
            free(cli_arr);
            perror("Accept failed.\n");
        }
        printf("Server acccepted client %d\n", cli_arr[i]);
        dispatch(t, (dispatch_fn)f, &cli_arr[i]);
    }
    destroy_threadpool(t);
    close(sockfd);
    free(cli_arr);
    return 0;
}