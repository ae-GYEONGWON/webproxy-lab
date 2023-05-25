/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

// 입력 ./tiny 8000 / argc = 2, argv[0] = tiny, argv[1] = 8000
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    /* Open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다. 인자로 포트번호를 넘겨준다. */
    /* Open_listenfd는 요청받을 준비가된 듣기 식별자를 리턴한다. = listenfd */
    listenfd = Open_listenfd(argv[1]);
    /* 전형적인 무한 서버 루프를 실행 */
    while (1) {
        
        // accept 함수 인자에 넣기 위한 주소 길이를 계산
        clientlen = sizeof(clientaddr);

        /* 반복적으로 연결 요청을 접수 */
        // accept 함수는 1. 듣기 식별자, 2. 소켓주소구조체의 주소, 3. 주소(소켓구조체)의 길이를 인자로 받는다.
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept

        // Getaddrinfo는 호스트 이름: 호스트 주소, 서비스 이름: 포트 번호의 스트링 표시를 소켓 주소 구조체로 변환
        // Getnameinfo는 위를 반대로 소켓 주소 구조체에서 스트링 표시로 변환.
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        /* 트랜젝션을 수행 */
        doit(connfd);                                             //line:netp:tiny:doit
        /* 자신 쪽의 연결 끝을 닫는다. */
        Close(connfd);                                            //line:netp:tiny:close
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    
    // rio_readlineb를 위해 rio_t 타입(구조체)의 읽기 버퍼를 선언
    rio_t rio;

    /* Read request line and headers */
    /* Rio = Robust I/O */
    // rio_t 구조체를 초기화 해준다.
    Rio_readinitb(&rio, fd); // &rio 주소를 가지는 읽기 버퍼와 식별자 connfd를 연결한다.
    if (!Rio_readlineb(&rio, buf, MAXLINE))  // 버퍼에서 읽은 것이 담겨있다.
        return;
    sscanf(buf, "%s %s %s", method, uri, version);       // 버퍼에서 자료형을 읽는다, 분석한다.
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
        return;
    }

    /* GET method라면 읽어들이고, 다른 요청 헤더들을 무시한다. */
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    /* URI 를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다.*/
    is_static = parse_uri(uri, filename, cgiargs);       //line:netp:doit:staticcheck
    printf("uri : %s, filename : %s, cgiargs : %s \n", uri, filename, cgiargs);

    /* 만일 파일이 디스크상에 있지 않으면, 에러메세지를 즉시 클라이언트에게 보내고 메인 루틴으로 리턴 */
    if (stat(filename, &sbuf) < 0) {                     //line:netp:doit:beginnotfound
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }                                                    //line:netp:doit:endnotfound

    /* Serve static content */
    if (is_static) { 
        // 파일 읽기 권한이 있는지 확인하기
        // S_ISREG : 일반 파일인가? , S_IRUSR: 읽기 권한이 있는지? S_IXUSR 실행권한이 있는가?
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
            // 권한이 없다면 클라이언트에게 에러를 전달
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
	}
    // 그렇다면 클라이언트에게 파일 제공
	serve_static(fd, filename, sbuf.st_size);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
        /* 실행 가능한 파일인지 검증 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
            // 실행이 불가능하다면 에러를 전달
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't run the CGI program");
            return;
        }
        // 그렇다면 클라이언트에게 파일 제공.
	    serve_dynamic(fd, filename, cgiargs);            //line:netp:doit:servedynamic
    }
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
/* Tiny는 요청 헤더 내의 어떤 정보도 사용하지 않는다.
* 단순히 이들을 읽고 무시한다.
*/
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);

    /* strcmp 두 문자열을 비교하는 함수 */ 
    /* 헤더의 마지막 줄은 비어있기에 \r\n 만 buf에 담겨있다면 while문을 탈출한다.*/
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        //rio 설명에 나와있다 싶이 rio_readlineb는 \n를 만날때 멈춘다.
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
        // 멈춘 지점 까지 출력하고 다시 while
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;
    /* strstr 으로 cgi-bin이 들어있는지 확인하고 양수값을 리턴하면 dynamic content를 요구하는 것이기에 조건문을 탈출 */
    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
	strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
	strcat(filename, uri);  
    
    // 결과 cgiargs = "" 공백 문자열, filename = "./~~ or ./home.html"              //line:netp:parseuri:endconvert1
    // uri 문자열 끝이 / 일 경우 home.html을 filename에 붙여준다.
	if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
	    strcat(filename, "home.html");               //line:netp:parseuri:appenddefault
	return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
    // uri 예시: dynamic: /cgi-bin/adder?first=1213&second
	ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
	// index 함수는 문자열에서 특정 문자의 위치를 반환한다.
    // CGI인자 추출
    if (ptr) {
        // 물음표 뒤에 있는 인자 다 갖다 붙인다.
        // 인자로 주러진 값들을 cgiargs 변수에 넣는다.
	    strcpy(cgiargs, ptr+1); // 물음표 뒤 인자들 전부 넣기
        // 포인터는 문자열 마지막으로 바꾼다.
	    *ptr = '\0'; // uri물음표 뒤 다 없애기
	}
	else 
	    strcpy(cgiargs, ""); 
	strcpy(filename, "."); // 나머지 부분 상대 uri로 바꿈
	strcat(filename, uri); // ./uri가 된다.
	return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
    
    /* Send response headers to client */
    // 파일 접미어 검사해서 파일이름에서 타입 가지고?
    get_filetype(filename, filetype);       // 접미어를 통해 파일 타입 결정한다.
    // 클라이언트에게 응답 줄과 응답 헤더 보내기
    // 클라이언트에게 응답 보내기
    // 데이터를 클라이언트로 보내기 전에 버퍼로 임시로 가지고 있는다.
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    // rio_readn은 fd의 현재 파일 위치에서 메모리 위치 buf로 strlen(buf)바이트를 전송한다.
    // rio_writen은 usrfd에서 식별자 fd로 strlen(buf)바이트를 전송한다.
    printf("Response headers:\n");
    printf("%s", buf);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    // open read only 읽고
    // PROT_READ -> 페이지는 읽을 수만 있다.
    // 파일을 어떤 메모리 공간에 대응시키고 첫주소를 리턴
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //메모리로 넘기고
    srcp = malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    Close(srcfd); // 소스파일을 닫아준다.
    Rio_writen(fd, srcp, filesize);         // srcp에서 fd로 filesize만큼 전송한다.
    // Munmap(srcp, filesize);                 //mmap()으로 만들어진 맵핑을 제거하기 위한 시스템
    free(srcp)
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    /* 11.7 숙제 문제 - Tiny 가 MPG 비디오 파일을 처리하도록 하기. */
    else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf)); // HTTP status 보내기
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf)); // server name 보내기
  
    if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
