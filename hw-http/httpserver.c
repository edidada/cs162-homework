#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue; // Only used by poolserver
int num_threads; // Only used by poolserver
int server_port; // Default value: 8000
char* server_files_directory;
char* server_proxy_hostname;
int server_proxy_port;

static int write_all(int fd, const void *data, size_t n) {
  const char *p = data;
  while (n) {
    ssize_t sent = write(fd, p, n);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) return -1;
    p += sent; n -= sent;
  }
  return 0;
}
static void error_response(int fd, int status) {
  http_start_response(fd, status);
  http_send_header(fd, "Content-Length", "0");
  http_end_headers(fd);
}
static void destroy_request(struct http_request *r) {
  if (r) { free(r->method); free(r->path); free(r); }
}
struct relay { int from, to; };
static void *relay_stream(void *arg) {
  struct relay *r = arg;
  char buffer[16384];
  while (1) {
    ssize_t n = read(r->from, buffer, sizeof buffer);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0 || write_all(r->to, buffer, (size_t)n) < 0) break;
  }
  /* Socket lifetime belongs to the handler, after both workers have joined. */
  shutdown(r->from, SHUT_RDWR); shutdown(r->to, SHUT_RDWR);
  return NULL;
}
#ifdef THREADSERVER
struct client_task { int fd; void (*handler)(int); };
static void *serve_client(void *arg) {
  struct client_task task = *(struct client_task *)arg;
  free(arg); task.handler(task.fd); return NULL;
}
#endif

/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's reponsibility to ensure that the file stored at `path` exists.
 */
void serve_file(int fd, char* path) {
  int file = open(path, O_RDONLY);
  struct stat st;
  if (file < 0) { error_response(fd, 404); return; }
  if (fstat(file, &st) < 0 || !S_ISREG(st.st_mode)) {
    close(file); error_response(fd, 404); return;
  }
  char length[64]; snprintf(length, sizeof length, "%lld", (long long)st.st_size);
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(path));
  http_send_header(fd, "Content-Length", length); http_end_headers(fd);
  char buffer[16384];
  while (1) {
    ssize_t n = read(file, buffer, sizeof buffer);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0 || write_all(fd, buffer, (size_t)n) < 0) break;
  }
  close(file);
}
void serve_directory(int fd, char* path) {
  DIR *dir = opendir(path);
  if (!dir) { error_response(fd, 404); return; }
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", "text/html"); http_end_headers(fd);
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".")) continue;
    size_t n = strlen(path) + 2 * strlen(entry->d_name) + 64;
    char *link = malloc(n);
    if (!link) break;
    /* Drop the filesystem dot prefix to produce root-relative links. */
    http_format_href(link, path[0] == '.' ? path + 1 : path, entry->d_name);
    int result = write_all(fd, link, strlen(link)); free(link);
    if (result < 0) break;
  }
  closedir(dir);
}

/*
 * Reads an HTTP request from client socket (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 *
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {
  struct http_request *request = http_request_parse(fd);
  char *path = NULL;
  if (!request || request->path[0] != '/') { error_response(fd, 400); goto done; }
  if (strcmp(request->method, "GET")) { error_response(fd, 405); goto done; }
  if (strstr(request->path, "..")) { error_response(fd, 403); goto done; }
  path = malloc(strlen(request->path) + 2);
  if (!path) { error_response(fd, 500); goto done; }
  sprintf(path, ".%s", request->path);
  struct stat st;
  if (stat(path, &st) < 0) error_response(fd, 404);
  else if (S_ISREG(st.st_mode)) serve_file(fd, path);
  else if (S_ISDIR(st.st_mode)) {
    char *index = malloc(strlen(path) + 12);
    if (!index) error_response(fd, 500);
    else {
      sprintf(index, "%s/index.html", path);
      if (stat(index, &st) == 0 && S_ISREG(st.st_mode)) serve_file(fd, index);
      else serve_directory(fd, path);
      free(index);
    }
  } else error_response(fd, 404);
done:
  free(path); destroy_request(request); close(fd);
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target_fd. HTTP requests from the client (fd) should be sent to the
 * proxy target (target_fd), and HTTP responses from the proxy target (target_fd)
 * should be sent to the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 *
 *   Closes client socket (fd) and proxy target fd (target_fd) when finished.
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and
  * opens a connection to it. Please do not modify.
  */
  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  // Use DNS to resolve the proxy target's IP address
  struct hostent* target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  // Create an IPv4 TCP socket to communicate with the proxy target.
  int target_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (target_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    close(fd);
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    close(target_fd);
    close(fd);
    exit(ENXIO);
  }

  char* dns_address = target_dns_entry->h_addr_list[0];

  // Connect to the proxy target.
  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status =
      connect(target_fd, (struct sockaddr*)&target_address, sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    destroy_request(http_request_parse(fd));

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(target_fd);
    close(fd);
    return;
  }

  /* TODO: PART 4 */
  /* PART 4 BEGIN */
  struct relay directions[2] = {{fd, target_fd}, {target_fd, fd}};
  pthread_t workers[2];
  int first = pthread_create(&workers[0], NULL, relay_stream, &directions[0]);
  int second = first ? first : pthread_create(&workers[1], NULL, relay_stream, &directions[1]);
  if (first || second) {
    shutdown(fd, SHUT_RDWR); shutdown(target_fd, SHUT_RDWR);
    if (!first) pthread_join(workers[0], NULL);
  } else { pthread_join(workers[0], NULL); pthread_join(workers[1], NULL); }
  close(target_fd); close(fd);
  /* PART 4 END */
}

#ifdef POOLSERVER
/*
 * All worker threads will run this function until the server shutsdown.
 * Each thread should block until a new request has been received.
 * When the server accepts a new connection, a thread should be dispatched
 * to send a response to the client.
 */
void* handle_clients(void* void_request_handler) {
  void (*request_handler)(int) = (void (*)(int))void_request_handler;
  /* (Valgrind) Detach so thread frees its memory on completion, since we won't
   * be joining on it. */
  pthread_detach(pthread_self());

  /* TODO: PART 7 */
  /* PART 7 BEGIN */
  for (;;) request_handler(wq_pop(&work_queue));
  return NULL;
  /* PART 7 END */
}

/*
 * Creates `num_threads` amount of threads. Initializes the work queue.
 */
void init_thread_pool(int num_threads, void (*request_handler)(int)) {

  /* TODO: PART 7 */
  /* PART 7 BEGIN */
  wq_init(&work_queue);
  for (int i = 0; i < num_threads; ++i) {
    pthread_t worker;
    if (pthread_create(&worker, NULL, handle_clients, (void *)request_handler)) {
      fprintf(stderr, "Cannot create pool worker\n"); exit(1);
    }
  }
  /* PART 7 END */
}
#endif

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int* socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  // Creates a socket for IPv4 and TCP.
  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) ==
      -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  // Setup arguments for bind()
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  /*
   * TODO: PART 1
   *
   * Given the socket created above, call bind() to give it
   * an address and a port. Then, call listen() with the socket.
   * An appropriate size of the backlog is 1024, though you may
   * play around with this value during performance testing.
   */

  /* PART 1 BEGIN */
  if (bind(*socket_number, (struct sockaddr *)&server_address, sizeof server_address) < 0 ||
      listen(*socket_number, 1024) < 0) { perror("bind/listen"); exit(1); }
  /* PART 1 END */
  printf("Listening on port %d...\n", server_port);

#ifdef POOLSERVER
  /*
   * The thread pool is initialized *before* the server
   * begins accepting client connections.
   */
  init_thread_pool(num_threads, request_handler);
#endif

  while (1) {
    client_socket_number = accept(*socket_number, (struct sockaddr*)&client_address,
                                  (socklen_t*)&client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr),
           client_address.sin_port);

#ifdef BASICSERVER
    /*
     * This is a single-process, single-threaded HTTP server.
     * When a client connection has been accepted, the main
     * process sends a response to the client. During this
     * time, the server does not listen and accept connections.
     * Only after a response has been sent to the client can
     * the server accept a new connection.
     */
    request_handler(client_socket_number);

#elif FORKSERVER
    /*
     * TODO: PART 5
     *
     * When a client connection has been accepted, a new
     * process is spawned. This child process will send
     * a response to the client. Afterwards, the child
     * process should exit. During this time, the parent
     * process should continue listening and accepting
     * connections.
     */

    /* PART 5 BEGIN */
    pid_t child = fork();
    if (child == 0) { close(*socket_number); request_handler(client_socket_number); _exit(0); }
    if (child < 0) perror("fork");
    close(client_socket_number);
    /* PART 5 END */

#elif THREADSERVER
    /*
     * TODO: PART 6
     *
     * When a client connection has been accepted, a new
     * thread is created. This thread will send a response
     * to the client. The main thread should continue
     * listening and accepting connections. The main
     * thread will NOT be joining with the new thread.
     */

    /* PART 6 BEGIN */
    struct client_task *task = malloc(sizeof *task);
    if (!task) { close(client_socket_number); continue; }
    task->fd = client_socket_number; task->handler = request_handler;
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t worker;
    if (pthread_create(&worker, &attr, serve_client, task)) {
      free(task); close(client_socket_number);
    }
    pthread_attr_destroy(&attr);
    /* PART 6 END */
#elif POOLSERVER
    /*
     * TODO: PART 7
     *
     * When a client connection has been accepted, add the
     * client's socket number to the work queue. A thread
     * in the thread pool will send a response to the client.
     */

    /* PART 7 BEGIN */
    wq_push(&work_queue, client_socket_number);
    /* PART 7 END */
#endif
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0)
    perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char* USAGE =
    "Usage: ./httpserver --files some_directory/ [--port 8000 --num-threads 5]\n"
    "       ./httpserver --proxy inst.eecs.berkeley.edu:80 [--port 8000 --num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);
#ifdef FORKSERVER
  signal(SIGCHLD, SIG_IGN);
#endif

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char* proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char* colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char* server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char* num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

#ifdef POOLSERVER
  if (num_threads < 1) {
    fprintf(stderr, "Please specify \"--num-threads [N]\"\n");
    exit_with_usage();
  }
#endif

  if (server_files_directory && chdir(server_files_directory) < 0) { perror("chdir"); return 1; }
  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
