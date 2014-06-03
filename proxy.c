/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Wei Zeng	        wz19@rice.edu
 *     Jiafang Jiang 	jj26@rice.edu
 *
 */

#include "csapp.h"
sem_t mutex;
pthread_mutex_t lock;
pthread_mutex_t thread_lock;

/*
 * Function prototypes
 */
int	parse_uri(char *uri, char *target_addr, char *path, int *port);
void	format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
	    char *uri, int size);
ssize_t Rio_readn_w(int fd, void *userbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *userbuf, size_t maxlen);
ssize_t Rio_writen_w(int fd, void *userbuf, size_t n);
int open_clientfd_ts(char *hostname, int port);
void write_logfile(char *inputlog);

/* new struct */
struct bundle {
	int fd;
	struct sockaddr_in clientaddr;
	int count;
};

/* Wrapper for rio_readn */
ssize_t Rio_readn_w(int fd, void *userbuf, size_t n){
	ssize_t ret;
	if((ret = rio_readn(fd, userbuf, n)) < 0){
		fprintf(stderr, "ERROR: Read failure! %s\n", strerror(errno));
		return ret;
	}
	return ret;
}

/* Wrapper for rio_readlineb */
ssize_t Rio_readlineb_w(rio_t *rp, void *userbuf, size_t maxlen){
	ssize_t ret;
	if((ret = rio_readlineb(rp, userbuf, maxlen)) < 0){
		fprintf(stderr, "ERROR: Read failure! %s\n", strerror(errno));
		return ret;
	}
	return ret;

}

/* Wrapper for rio_writen */
ssize_t Rio_writen_w(int fd, void *userbuf, size_t n){
	ssize_t ret;
	ret = rio_writen(fd, userbuf, n);
	if((size_t)ret != n){
		return ret;
	}
	return ret;
}

/* 
 * Thread safe version of open_clientfd,
 * using getaddrinfo
 */
int open_clientfd_ts(char *hostname, int port)
{
	int clientfd;
	struct addrinfo *res, *loop_elem;
	int error;
	char portString[MAXLINE];

	/* create socket */
	if((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		return -1; /* check errno for cause of error */
	}
	/* create address */
	sprintf(portString, "%d", port);// convert port from int to string
	error = getaddrinfo(hostname, portString, NULL, &res);

	/* failed in getaddrinfo */
	if(error != 0){
		fprintf(stderr, "ERROR: getaddrinfo failure.%s\n", gai_strerror(error));
		return (-2);
	}

	/* obtain connection */
	for(loop_elem = res; loop_elem != NULL; loop_elem = loop_elem->ai_next){

		if(connect(clientfd, loop_elem->ai_addr, loop_elem->ai_addrlen) < 0){
			continue;
		}
		break; // we have established an connection

	}
	// here, we have no established any connection
	if(loop_elem == NULL){
		fprintf(stderr, "No connection, sorry.\n");
		return (-1);
	}

	freeaddrinfo(res);
	return clientfd;
}

/* Write format_log_entry to "proxy.log" */
void write_logfile(char *inputlog)
{
	FILE *file;
	file = fopen("proxy.log","a+");
	if(file == NULL){
		fprintf(stderr, "ERROR: file open error at %s\n", "proxy.log");
		return;
	}
	fprintf(file, "%s\n", inputlog);
	fclose(file);

	return;

}


/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
		 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Simple Proxy</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/* Handle Request from main function */
void doit(struct bundle *my_helper)
{
	Pthread_detach(pthread_self()); // mark myself as detach

	/* Local variables */
	int connfd = my_helper->fd;
	char *request_to_send = Malloc(sizeof(char) * MAXLINE);
	bzero(request_to_send, sizeof(char) * MAXLINE);
	char request_uri[MAXLINE];
	bzero(request_uri, MAXLINE);

	/* parse uri, then we have the following three */

	char request_method[MAXLINE];
	bzero(request_method, MAXLINE);
	char request_version[MAXLINE];
	bzero(request_version, MAXLINE);
	char request_path[MAXLINE];
	bzero(request_path, MAXLINE);
	/* log_string to write each time */
	char log_string[MAXLINE];
	bzero(log_string, MAXLINE);
	char buffer[MAXLINE];
	bzero(buffer, MAXLINE);
	char target_addr[MAXLINE];
	bzero(target_addr, MAXLINE);

	char stripping_header[MAXLINE];
	bzero(stripping_header, MAXLINE);

	char request_from_client[MAXLINE];
	bzero(request_from_client, MAXLINE);

	int port_num;
	ssize_t rio_read_flag;
	/* local variables of me being a client */
	int proxy_client_fd;
	int logfile_size = 0;
	int content_size = 0;
	rio_t proxy_client_rio;
	int http_version_flag = 0;

	/* Rio read init per descriptor */
	rio_t rio;
	Rio_readinitb(&rio, connfd);

	if (Rio_readlineb_w(&rio, buffer, MAXLINE) < 0){
		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		Free(my_helper);
		Free(request_to_send);
		return;
	}

	memcpy(request_from_client, buffer, strlen(buffer));
	/* separate elements of the request line */
	if(sscanf(buffer, "%s %s %s", request_method, request_uri, request_version) != 3){
		// error occured in sscanf
		printf("ERROR: request line formatting error.\n");
		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		Free(my_helper);
		Free(request_to_send);
		return;
	}

	/* if not GET, just return */
	if(strcmp(request_method, "GET") != 0){
		fprintf(stderr, "ERROR: Proxy cannot handle %s\n", request_method);
       	clienterror(connfd, request_method, "501", "Not Implemented", "Proxy does not implement this method");
		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		Free(my_helper);
		Free(request_to_send);
		return;
	}

	/* change the http_version_flag according to request_version */
	if(strstr(request_version, "1.1") == NULL){
		http_version_flag = 0;
	} else {
		http_version_flag = 1;
	}

	parse_uri(request_uri, target_addr, request_path, &port_num);

	/* need to sscanf again */
	if(sscanf(buffer, "%s %s %s", request_method, request_uri, request_version) != 3){
		// error occured in sscanf
		printf("ERROR: request line formatting error.\n");
		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		Free(my_helper);
		Free(request_to_send);
		return;

	}

	/* building the request line for sending to the real website server */
	strcat(request_method, " "); 
	strcat(request_path, " ");
	strcat(request_version, "\n");
	/* put the modified request line in to request_to_send */
	strcat(request_to_send, request_method);
	strcat(request_to_send, request_path);
	strcat(request_to_send, request_version);


	/* now i have the first line */

	while ((rio_read_flag = Rio_readlineb_w(&rio, buffer, MAXLINE)) != 0){
		/* error in reading headers*/
		if(rio_read_flag < 0){
			Close(connfd);
			Free(my_helper);
			Free(request_to_send);
			return; // kill thread
		}

		strcat(request_from_client, buffer);

		if(strcmp(buffer,"\r\n") != 0 ){
			// if not end of request
			if((strstr(buffer, "Connection: ") == NULL) && (strstr(buffer, "Proxy-Connection: ") == NULL)){
				/* if we don't have "Connection: " nor "Proxy-Connection: " */
				strcat(request_to_send, buffer);
			} else {				
				memcpy(stripping_header, buffer, strlen(buffer));
			}
		} else {
			// now we are at the end of the request, after this block of code, we will break out
			if(http_version_flag == 1){ // if HTTP of version 1.1, add this
				strcat(request_to_send, "Connection: close \n"); // manully add this
			}
			strcat(request_to_send, buffer); // add "\r\n" to request_to_send
			/* I have already read everything from request header!
			 * BREAK OUT!
			 */
			 break;
		}
	}
	/* now I have the all request to send*/
	/* Open clientfd, so I am a client now to the real server */
	printf("Request %d: Received request from %s (%s): \n%s \n %s \n", my_helper->count, inet_ntoa(my_helper->clientaddr.sin_addr), 
		inet_ntoa(my_helper->clientaddr.sin_addr), request_from_client, "***  End of Request ***\n");

	if((proxy_client_fd = open_clientfd_ts(target_addr, port_num)) <0){
		Close(connfd);
		Free(my_helper);
		Free(request_to_send);
		return;
	}
	/* if open client successed */
	printf("Stripping Header %s\r%s \n %s \n", stripping_header, request_to_send, "***  End of Request ***\n");
	// send the built request to the real server
	Rio_writen_w(proxy_client_fd, request_to_send, strlen(request_to_send));
	/* Now I have send all my request to the real website */

	/* As a client, receive info. from the real server */
	Rio_readinitb(&proxy_client_rio, proxy_client_fd);
	/* read until Rio_readline returns 0, write to client simutaneously*/

	while ((rio_read_flag = Rio_readlineb_w(&proxy_client_rio, buffer, MAXLINE)) != 0) {
		if(rio_read_flag < 0){
			Close(connfd);
			Free(my_helper);
			Free(request_to_send);
			return; // kill thread
		}

		/* 404 error */
		if(strstr(buffer, "404 Not Found") != NULL){
			clienterror(connfd, target_addr, "404", "Not Found", "This site is not found");
			break;
		}


		logfile_size += strlen(buffer);
	    Rio_writen_w(connfd, buffer, strlen(buffer));
		if(strcmp(buffer,"\r\n") != 0) {
			if (strstr(buffer, "Content-Length: ") != NULL) {
				content_size = atoi(strstr(buffer, " ") + 1);
			}
	    } else {
	    	break;
	    }
	}
	/* Now I have the response above HTML code all written to connfd */

	/* just read the content of HTML code */
	int n;
	while(1){
		n = Rio_readlineb_w(&proxy_client_rio, buffer, MAXLINE);
		Rio_writen_w(connfd, buffer, n);
		printf("Request %d: Fowarded %d bytes from end server to client\n", my_helper->count, n);
		logfile_size += n;
		if (n == 0){
			break;
		}
	}

	Close(proxy_client_fd);

	format_log_entry(log_string, &(my_helper->clientaddr), request_uri, logfile_size);

	logfile_size = 0; // reset to 0

	pthread_mutex_lock(&lock);
	write_logfile(log_string); // lock'n'write
	pthread_mutex_unlock(&lock);


	Close(connfd);
	Free(my_helper);
	Free(request_to_send);
	return;
}





/*
 * main - Main routine for the proxy program
 */
int
main(int argc, char **argv)
{
	/* ignore SIGPIPE to prevent crashing */
	Signal(SIGPIPE, SIG_IGN);

	/* local variables */
	int port;
	int listenfd;
	socklen_t clientaddrlen;
	pthread_t tid;
	struct bundle *my_helper;
	int request_count = 0;

	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}

	/* init mutex lock */
	if (pthread_mutex_init(&lock, NULL) != 0 || pthread_mutex_init(&thread_lock, NULL) != 0)
   {
       printf("\n mutex init failed\n");
       return 1;
   }

	port = atoi(argv[1]); // get port num
	listenfd = Open_listenfd(port);
	/* forever loop */
	while(1){
		my_helper = Malloc(sizeof(struct bundle));
		clientaddrlen = sizeof(struct sockaddr_in);
		my_helper->fd = Accept(listenfd, (SA *)(&(my_helper->clientaddr)), &clientaddrlen);
		
		// init request atomic count to be 0
		my_helper->count = request_count;
		/* Create a thread */
		Pthread_create(&tid, NULL, (void *)doit, (void *)my_helper);
		/* atomically add to the request counter */
		pthread_mutex_lock(&thread_lock);
		request_count += 1;
		pthread_mutex_unlock(&thread_lock);
	}
	/* Destroy the locks */
	pthread_mutex_destroy(&thread_lock);
	pthread_mutex_destroy(&lock);
	/* Return success. */
	return (0);
}


/*
 * parse_uri - URI parser
 *
 * Requires:
 *   The memory for hostname and pathname must already be allocated
 *   and should be at least MAXLINE bytes.  Port must point to a
 *   single integer that has already been allocated.
 *
 * Effects:
 *   Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 *   the host name, path name, and port.  Return -1 if there are any
 *   problems and 0 otherwise.
 */
int
parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
	char *hostbegin;
	char *hostend;
	int len, i, j;
	
	if (strncasecmp(uri, "http://", 7) != 0) {
		hostname[0] = '\0';
		return (-1);
	}

	/* Extract the host name. */
	hostbegin = uri + 7;
	hostend = strpbrk(hostbegin, " :/\r\n");
	if (hostend == NULL)
		hostend = hostbegin + strlen(hostbegin);
	len = hostend - hostbegin;
	strncpy(hostname, hostbegin, len);
	hostname[len] = '\0';
	
	/* Look for a port number.  If none is found, use port 80. */
	*port = 80;
	if (*hostend == ':')
		*port = atoi(hostend + 1);
	
	/* Extract the path. */
	for (i = 0; hostbegin[i] != '/'; i++) {
		if (hostbegin[i] == ' ')
			break;
	}
	if (hostbegin[i] == ' ')
		strcpy(pathname, "/");
	else {
		for (j = 0; hostbegin[i] != ' '; j++, i++)
			pathname[j] = hostbegin[i];
		pathname[j] = '\0';
	}

	return (0);
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * Requires:
 *   The memory for logstring must already be allocated and should be
 *   at least MAXLINE bytes.  Sockaddr must point to an allocated
 *   sockaddr_in structure.  Uri must point to a properly terminated
 *   string.
 *
 * Effects:
 *   A properly formatted log entry is stored in logstring using the
 *   socket address of the requesting client (sockaddr), the URI from
 *   the request (uri), and the size in bytes of the response from the
 *   server (size).
 */
void
format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri,
    int size)
{
	time_t now;
	char time_str[MAXLINE];
	unsigned long host;
	unsigned char a, b, c, d;

	/* Get a formatted time string. */
	now = time(NULL);
	strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z",
	    localtime(&now));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form.  Note that we could have used inet_ntoa, but chose not to
	 * because inet_ntoa is a Class 3 thread unsafe function that
	 * returns a pointer to a static variable (Ch 13, CS:APP).
	 */
	host = ntohl(sockaddr->sin_addr.s_addr);
	a = host >> 24;
	b = (host >> 16) & 0xff;
	c = (host >> 8) & 0xff;
	d = host & 0xff;

	/* Return the formatted log entry string */
	sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri,
	    size);
}

/*
 * The last lines of this file configures the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
