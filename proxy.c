/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Wei Zeng			wz19@rice.edu
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
ssize_t Rio_readnb_w(rio_t* rp, void* usrbuf, size_t n);
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
	// errno = 0; // is this way of defining errno correct?
	if((ret = rio_readn(fd, userbuf, n)) < 0){
		fprintf(stderr, "ERROR: Read failure! %s\n", strerror(errno));
		return ret;
	}
	return ret;
}

ssize_t Rio_readnb_w(rio_t* rp, void* usrbuf, size_t n) {
  ssize_t ret;

  if ((ret = rio_readnb(rp, usrbuf, n)) < 0) {
    printf("Rio_readnb_w: rio_readnb failed\n");
    return 0;
  }
  return ret;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *userbuf, size_t maxlen){
	ssize_t ret;
	// errno = 0; // is this way of defining errno correct?
	if((ret = rio_readlineb(rp, userbuf, maxlen)) < 0){
		fprintf(stderr, "ERROR: Read failure! %s\n", strerror(errno));
		return ret;
	}
	return ret;

}


ssize_t Rio_writen_w(int fd, void *userbuf, size_t n){
	ssize_t ret;
	// errno = 0; // is this way of defining errno correct?
	ret = rio_writen(fd, userbuf, n);
	//printf("ret size %d, n size %d\n", (int)ret, (int)n);
	if((size_t)ret != n){
		
		//fprintf(stderr, "ERROR: Write failure! %s\n", strerror(errno));
		return ret;
	}
	return ret;
}

//   int open_clientfd_ts(char *hostname, int port)
// {
//     int clientfd, error;
//     struct addrinfo *ai;
//     struct sockaddr_in serveraddr;

//     if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
//         return -1; /* check errno for cause of error */


//     error = getaddrinfo(hostname, NULL, NULL, &ai);

//     if (error != 0) {
//         /* check gai_strerr for cause of error */
//         fprintf(stderr, "ERROR: %s", gai_strerror(error));
//         freeaddrinfo(ai);
//         return -1;
//     }

//     /* Fill in the server's IP address and port */
//     bzero((char *) &serveraddr, sizeof(serveraddr));
//     serveraddr.sin_family = AF_INET;
//     bcopy(ai->ai_addr,
//       (struct sockaddr *)&serveraddr, ai->ai_addrlen);
//     serveraddr.sin_port = htons(port);

//     /* Establish a connection with the server */
//     if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
//         return -1;

//     freeaddrinfo(ai);
//     return clientfd;
// }



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
			// DO WE NEED TO PERROR?
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

void write_logfile(char *inputlog)
{
	//printf("%s\n", "started writing to log file");
	FILE *file;
	file = fopen("proxy.log","a+");
	if(file == NULL){
		fprintf(stderr, "ERROR: file open error at %s\n", "proxy.log");
		return; // or exit?
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
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
    printf("%s\n", "done");
}


int
process_chunked(rio_t proxy_client_rio, int connfd)
{

	int chunk_size = 0;
	ssize_t rio_read_flag = 0;
	int size_read = 0;
	char buffer[MAXLINE];


	while ((rio_read_flag = Rio_readlineb_w(&proxy_client_rio, buffer, MAXLINE)) != 0) {
//		printf("a new start lalalal\n");
			if(rio_read_flag < 0){
				return -1; // kill thread
			}

			size_read += strlen(buffer);
			sscanf(buffer, "%x", &chunk_size);
//			printf("chunksize is %d\n", chunk_size);
//			printf("what we originally read is %s\n", buffer);
			Rio_writen_w(connfd, buffer, rio_read_flag);
			if (chunk_size == 0)
				break;

			/* If the chunk size is larger than MAXLINE */
			while (chunk_size > MAXLINE) {
				if((rio_read_flag = Rio_readnb_w(&proxy_client_rio, buffer, MAXLINE)) < 0){
					return -1; // kill thread
				}
				chunk_size -= MAXLINE;
				size_read += strlen(buffer);
//				printf("In chunk, read from server is: %s, its size is %lu, rio_read_flag is %lu\n", buffer, strlen(buffer), rio_read_flag);
//				printf("Now chunk size is %d\n", chunk_size);
				Rio_writen_w(connfd, buffer, rio_read_flag);
			}

			/* Read the last chunk */
			if((rio_read_flag = Rio_readnb_w(&proxy_client_rio, buffer, chunk_size)) < 0){
				return -1; // kill thread
			}

			size_read += strlen(buffer);
			Rio_writen_w(connfd, buffer, rio_read_flag);
//			printf("In chunk, read from server 2 is: %s, its size is %lu\n", buffer, strlen(buffer));

			/* Read the new line after the last chunk */
			if((rio_read_flag = Rio_readlineb_w(&proxy_client_rio, buffer, MAXLINE)) < 0){
				return -1; // kill thread
			}

			size_read += strlen(buffer);
			Rio_writen_w(connfd, buffer, rio_read_flag);
//			printf("In chunk, read from server is, the new line: %s\n", buffer);

		}

	return size_read;
}

int
process_unchunked(rio_t proxy_client_rio, int connfd, int content_size)
{
	//int size_to_read = content_size;
	int size_read = content_size;
	ssize_t rio_read_flag = 0;
	char buffer[MAXLINE];


	while (size_read < content_size) {
		rio_read_flag = Rio_readnb_w(&proxy_client_rio, buffer, (content_size - size_read) < MAXLINE ? (content_size - size_read) : MAXLINE);
		if(rio_read_flag < 0){
			return -1; // kill thread
		} else if (rio_read_flag == 0){
			break;
		}
		//printf("content_size is %d\n", content_size);
		printf("Response content from server is %s\n", buffer);
		size_read += rio_read_flag;
		//size_read += strlen(buffer);
		//size_to_read -= strlen(buffer);
		printf("what is size read: %d\n", size_read);
		//						strcat(read_content, buffer);
		//						printf("Response Content write to client is:\n%s", read_content);
		Rio_writen_w(connfd, buffer, rio_read_flag);
	}

	return size_read;

}



void doit(struct bundle *my_helper)
{
	/******************************* be sequential for a while ******************/
	Pthread_detach(pthread_self()); // mark myself as detach
	/******************************* be sequential for a while ******************/
	printf("Receiving request %d\n", my_helper->count);
	/* Local variables */
	int connfd = my_helper->fd;
	//char *malloc_target_addr;



	char *request_to_send = Malloc(sizeof(char) * MAXLINE);
	bzero(request_to_send, sizeof(char) * MAXLINE);
	//printf("request_to_send init %s\n", request_to_send);
	//char request_to_send[MAXLINE];
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

	//char *buffer = Malloc(sizeof(char) * MAXLINE);
	char buffer[MAXLINE];
	bzero(buffer, MAXLINE);
	char target_addr[MAXLINE];
	bzero(target_addr, MAXLINE);

	int port_num;
	ssize_t rio_read_flag;
	/* local variables of me being a client */
	int proxy_client_fd;
	int logfile_size = 0;
	int content_size = 0;
	rio_t proxy_client_rio;
	int http_version_flag = 0;

	int is_chunked = 0;

	/* Rio read init per descriptor */
	rio_t rio;
	Rio_readinitb(&rio, connfd);

	if (Rio_readlineb_w(&rio, buffer, MAXLINE) < 0){
		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		//Free(&(my_helper->fd));
//		Free((my_helper->clientaddr));
		Free(my_helper);
		Free(request_to_send);
		return;
	}

	/* separate elements of the request line */
	if(sscanf(buffer, "%s %s %s", request_method, request_uri, request_version) != 3){
		// error occured in sscanf
		printf("ERROR: request line formatting error.\n");
		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		//Free(&(my_helper->fd));
//		Free((my_helper->clientaddr));
		Free(my_helper);
		Free(request_to_send);
		return;
	}

	/* if not GET, just return */
	if(strcmp(request_method, "GET") != 0){
		fprintf(stderr, "ERROR: Proxy cannot handle %s\n", request_method);

       	clienterror(connfd, request_method, "501", "Not Implemented", "Proxy does not implement this method");

		Close(connfd); // ALWAYS REMEMBER TO CLOSE CONNECTION BEFORE RETURN
		//Free(&(my_helper->fd));
//        Free((my_helper->clientaddr));
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
		//Free(&(my_helper->fd));
//        Free((my_helper->clientaddr));
    Free(my_helper);
    Free(request_to_send);
		return;

	}

	//int target_addr_len = strlen(target_addr);

	//printf("target_addr after 2nd sscanf %s\n", target_addr);
	//malloc_target_addr = Malloc(sizeof(char) * target_addr_len);
	//strncpy(malloc_target_addr, target_addr, strlen(target_addr));
	//strlcat(target_addr,"\0");

	/* building the request line for sending to the real website server */
	strcat(request_method, " "); // because path all starts with "/" LALALALALAL
	strcat(request_path, " ");
	strcat(request_version, "\n");
	/* put the modified request line in to request_to_send */
	//printf("request 1 %s\n", request_to_send);
	strcat(request_to_send, request_method);
	//printf("request 2 %s\n", request_to_send);
	strcat(request_to_send, request_path);
	//printf("request 3 %s\n", request_to_send);
	strcat(request_to_send, request_version);
	//printf("request 4 %s\n", request_to_send);
	/* now i have the first line */

	//printf("target_addr before while %s\n",target_addr);

	while ((rio_read_flag = Rio_readlineb_w(&rio, buffer, MAXLINE)) != 0){
		/* error in reading headers*/
		if(rio_read_flag < 0){
			Close(connfd);
			//Free(&(my_helper->fd));
//			Free((my_helper->clientaddr));
			Free(my_helper);
			return; // kill thread
		}
		if(strcmp(buffer,"\r\n") != 0 ){
		//if(strcmp(buffer,"\r\n") != 0 || strcmp(buffer, "\r") != 0 || strcmp(buffer, "\n") != 0){
			// if not end of request
			if((strstr(buffer, "Connection: ") == NULL) && (strstr(buffer, "Proxy-Connection: ") == NULL)){
				/* if we don't have "Connection: " nor "Proxy-Connection: " */
                //				printf("No connection buffer:::: %s", buffer);

				strcat(request_to_send, buffer);
			} else {
				printf("[%d]Stripping header: %s\n", my_helper->count,buffer);
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


	//strncpy(malloc_target_addr, target_addr, target_addr_len);

	//printf("what is the tar_add: %s \n what is the port num %d\n", target_addr, port_num);
	if((proxy_client_fd = open_clientfd_ts(target_addr, port_num)) <0){
		/* Use Rio_writen? To print to terminal? or something? */
		Close(connfd);
		//Free(&(my_helper->fd));
//		Free((my_helper->clientaddr));
		Free(my_helper);
		Free(request_to_send);
		return;
	}
	/* if open client successed */
	// send the built request to the real server
	printf("[%d]Request to send is: \n%s",my_helper->count,request_to_send);
	printf("[%d] <---- request print finish \n",my_helper->count);
	Rio_writen_w(proxy_client_fd, request_to_send, strlen(request_to_send));
	printf("[%d] <---- request send \n",my_helper->count);
	/* Now I have send all my request to the real website */

	/* As a client, receive info. from the real server */
	Rio_readinitb(&proxy_client_rio, proxy_client_fd);
	/* read until /r/n, write to client simutaneously*/

	while ((rio_read_flag = Rio_readlineb_w(&proxy_client_rio, buffer, MAXLINE)) != 0) {
		if(rio_read_flag < 0){
			Close(connfd);
			//Free(&(my_helper->fd));
//			Free((my_helper->clientaddr));
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
	    //printf("response header read from server is: %s", buffer);
	    Rio_writen_w(connfd, buffer, strlen(buffer));
		if(strcmp(buffer,"\r\n") != 0) {
			if (strstr(buffer, "Content-Length: ") != NULL) {
				content_size = atoi(strstr(buffer, " ") + 1);
	           //printf("Content size is %d\n", content_size);
			}
	        if (strstr(buffer, "Transfer-Encoding: ") != NULL) {
	        	is_chunked = 1;
	        }
	    } else {
	    	break;
	    }
	}
	/* Now I have the response above HTML code all written to connfd */
	int n;
	while(1){
		n = Rio_readlineb_w(&proxy_client_rio, buffer, MAXLINE);
		//printf("reading html %s\n", buffer);
		Rio_writen_w(connfd, buffer, n);
		if (n == 0){
			break;
		}
	}
//	while (1) {
// 		//printf("first time reading buffer %s\n", buffer);
// 		/* Read and Write response content */
// 		int size_read = 0;

// 		if (is_chunked) {
// 			if ((size_read = process_chunked(proxy_client_rio, connfd)) < 0) {
//                 Close(proxy_client_fd);
//                 Close(connfd);
//                 //Free(&(my_helper->fd));
// //                Free((my_helper->clientaddr));
//                 Free(my_helper);
//                 Free(request_to_send);
//                 return; // kill thread
// 			}
// 			logfile_size += size_read;
// 			break;
// 		}
// 		/* If the response content is not chunked*/
// 		else {
// 			if ((size_read = process_unchunked(proxy_client_rio, connfd, content_size)) < 0) {
//                 Close(proxy_client_fd);
//                 Close(connfd);
//                 //Free(&(my_helper->fd));
// //                Free((my_helper->clientaddr));
//                 Free(my_helper);
//                 Free(request_to_send);
//                 return; // kill thread
// 			}
// 			logfile_size += size_read;

//       break;
// 		}

// 	}
	logfile_size = 5;
	Close(proxy_client_fd);
//	printf("[%d]log_string herer %s\n", my_helper->count, log_string);
//	printf("[%d]request_uri herherh%s\n", my_helper->count, request_uri);
//	printf("[%d]log size %d\n", my_helper->count, logfile_size);
//	printf("the stuff we need to print is %d\n", my_helper->clientaddr.sin_addr.s_addr);
	format_log_entry(log_string, &(my_helper->clientaddr), request_uri, logfile_size);

	logfile_size = 0; // reset to 0
	//printf("log string is %s\n", log_string);


	pthread_mutex_lock(&lock);
	write_logfile(log_string); // write this function!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	pthread_mutex_unlock(&lock);


	Close(connfd);
	//Free(&(my_helper->fd));
//	Free((my_helper->clientaddr));
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
//	struct sockaddr_in *clientaddr;
	int port;
	int listenfd;
	//int *connectionfd;
	socklen_t clientaddrlen;
	/******************************* be sequential for a while ******************/
	pthread_t tid;
	/******************************* be sequential for a while ******************/
	struct bundle *my_helper;
	int request_count = 0;

	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}

	/* init mutex lock */
	/******************************* be sequential for a while ******************/
	if (pthread_mutex_init(&lock, NULL) != 0 || pthread_mutex_init(&thread_lock, NULL) != 0)
   {
       printf("\n mutex init failed\n");
       return 1;
   }
	/******************************* be sequential for a while ******************/

	port = atoi(argv[1]); // get port num
	listenfd = Open_listenfd(port);
	/* forever loop */
	while(1){
		//connectionfd = Malloc(sizeof(int));

//		clientaddr = Malloc(sizeof(struct sockaddr_in));
		my_helper = Malloc(sizeof(struct bundle));


		clientaddrlen = sizeof(struct sockaddr_in);
		my_helper->fd = Accept(listenfd, (SA *)(&(my_helper->clientaddr)), &clientaddrlen);
		/* build the helper struct */
		//my_helper->fd = *connectionfd;
		//my_helper->clientaddr = clientaddr;
		my_helper->count = request_count;
		/******************************* be sequential for a while ******************/
		Pthread_create(&tid, NULL, (void *)doit, (void *)my_helper);

		/* atomically add to the request counter */
		pthread_mutex_lock(&thread_lock);
		request_count += 1;
		pthread_mutex_unlock(&thread_lock);
		/******************************* be sequential for a while ******************/

	}
	/******************************* be sequential for a while ******************/
	pthread_mutex_destroy(&thread_lock);
	pthread_mutex_destroy(&lock);
	/******************************* be sequential for a while ******************/
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
