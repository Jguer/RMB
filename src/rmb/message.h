#ifndef MESSAGEH
#define MESSAGEH

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "utils.h"
#include "server.h"

int publish(server *sel_server, char *msg);
server *select_server(list *server_list);
#endif
