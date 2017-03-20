#include <time.h>
#include <errno.h>
#include <sys/timerfd.h>

#include "message.h"
#include "identity.h"

void usage(char* name) {
    fprintf(stdout, "Example Usage: %s [-i siip] [-p sipt] %s \n", name, _VERBOSE_OPT_SHOW);
    fprintf(stdout, "Arguments:\n"
            "\t-i\t\t[server ip]\n"
            "\t-p\t\t[server port]\n"
            "%s", _VERBOSE_OPT_INFO);
}

int main(int argc, char *argv[]) {
    char server_ip[STRING_SIZE] = "tejo.tecnico.ulisboa.pt";
    char server_port[STRING_SIZE] = "59000";

    srand(time(NULL));
    // Treat options
    int_fast8_t oc  = 0;
    while ((oc = getopt(argc, argv, "i:p:v")) != -1) { //Command-line args parsing, 'i' and 'p' args required for both
        switch (oc) {
            case 'i':
                strncpy(server_ip, optarg, STRING_SIZE); //optarg has the string corresponding to oc value
                break;
            case 'p':
                strncpy(server_port, optarg, STRING_SIZE); //optarg has the string corresponding to oc value
                break;
            case ':':
                /* missing option argument */
                fprintf(stderr, "%s: option '-%c' requires an argument\n",
                        argv[0], optopt);
                break;
            case 'v':
                _VERBOSE_OPT_CHECK;
            case '?': //Left blank on purpose, help option
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    fprintf(stdout, KBLU "Identity Server:" KNRM " %s:%s\n", server_ip, server_port);
    struct addrinfo *id_server = get_server_address(server_ip, server_port);
    if (NULL == id_server) {
        fprintf(stderr, KRED "Unable to parse id server address from:\n %s:%s", server_ip, server_port);
        return EXIT_FAILURE;
    }

    int_fast32_t outgoing_fd = -1;
    int_fast32_t binded_fd = -1;
    int_fast32_t timer_fd = -1;
    list *msgservers_lst;
    server *sel_server;

    struct sockaddr_in server_addr = { 0 , .sin_port = 0};
    socklen_t addr_len;
    uint_fast32_t to_alloc;
    int read_size;
    struct itimerspec new_timer = {
        {SERVER_TEST_TIME_SEC,SERVER_TEST_TIME_nSEC},   //Interval of time
        {SERVER_TEST_TIME_SEC,SERVER_TEST_TIME_nSEC}    //Stop time
        };

    uint_fast8_t exit_code = EXIT_SUCCESS;

    if (1 == init_program(id_server, &outgoing_fd,
                &binded_fd, &msgservers_lst, &sel_server,
                &new_timer, &timer_fd)){
        exit_code = EXIT_FAILURE;
        goto PROGRAM_EXIT_INIT;
    }

    char *response_buffer;
    fd_set rfds;
    uint_fast8_t err = EXIT_SUCCESS;
    uint_fast8_t max_fd = -1; // Max fd number.    

    bool server_not_answering = false;
    server *old_server = NULL;
    uint_fast8_t ban_counter = 0;
    uint_fast16_t msg_num = 0; // Number of messages asked
    char op[STRING_SIZE];
    char input_buffer[STRING_SIZE];

    if (sel_server != NULL) {
        fprintf(stdout, KGRN "Prompt > " KNRM);
        fflush(stdout);
    }

    // Interactive loop
    while (true) {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(binded_fd, &rfds);
        FD_SET(timer_fd, &rfds);

        max_fd = binded_fd > max_fd ? binded_fd : max_fd;
        max_fd = timer_fd > max_fd ? timer_fd : max_fd;

        if (NULL == sel_server || err || server_not_answering) {
            fprintf(stderr, KYEL "Searching\n" KNRM);
            sleep(3);

            if (SERVER_BAN_TIME < ban_counter) {
                if (sel_server != NULL) free_server(sel_server);
                sel_server = NULL;
                if (old_server != NULL) free_server(old_server);
                old_server = NULL;
                server_not_answering = false;
                ban_counter = 0;
                continue;
            }

            if (server_not_answering) {
            	if(sel_server != NULL) old_server = copy_server(old_server, sel_server);

                if (old_server == NULL) {
                    goto PROGRAM_EXIT;
                }
            }

            free_list(msgservers_lst, free_server);
            msgservers_lst = fetch_servers(outgoing_fd, id_server);
            if (server_not_answering) {
                rem_awol_server(msgservers_lst, old_server);
            }
            sel_server = select_server(msgservers_lst);
            if (sel_server != NULL) {
                fprintf(stderr, KGRN "Connected to new server\n" KNRM);
                fprintf(stdout, KGRN "Prompt > " KNRM);
                fflush(stdout);
                server_not_answering = false;
                ban_counter = 0;
                if(old_server != NULL) free_server(old_server);
            } else {
                fprintf(stderr, KYEL "No servers available..." KNRM);
                fflush(stdout);
            }

            ban_counter ++;
            continue;
        }

        int activity = select(max_fd + 1 , &rfds, NULL, NULL, NULL); //Select, threading function
        if (0 > activity) {
            printf("error on select\n%d\n", errno);
            exit_code = EXIT_FAILURE;
            goto PROGRAM_EXIT;
        }

        if (FD_ISSET(timer_fd, &rfds)) { //if the timer is triggered
            uint_fast8_t server_test_status = exec_server_test();

            if (1 == server_test_status) {
                printf(KYEL "Server not answering\n" KNRM);
                fflush(stdout);
                server_not_answering = true;
            }

            timerfd_settime (timer_fd, 0, &new_timer, NULL);
            continue;
        }

        if (FD_ISSET(binded_fd, &rfds)) { //UDP receive
            addr_len = sizeof(server_addr);
            if ((msg_num +1) * RESPONSE_SIZE > to_alloc) {
                to_alloc = (msg_num + 1) * RESPONSE_SIZE;
                response_buffer = (char*)realloc(response_buffer, sizeof(char) * to_alloc);
                if (NULL == response_buffer) {
                    memory_error("Unable to realloc buffer\n");
                }
            }

            bzero(response_buffer, to_alloc);

            read_size = recvfrom(binded_fd, response_buffer, sizeof(char)*to_alloc, 0,
                    (struct sockaddr *)&server_addr, &addr_len);

            if (-1 == read_size) {
                if (_VERBOSE_TEST) fprintf(stderr, KRED "Failed UPD receive from %s\n" KNRM, inet_ntoa(server_addr.sin_addr));
                goto UDP_END;
            }
            if (_VERBOSE_TEST){
                puts(response_buffer);
                fflush(stdout);
            }
            sscanf(response_buffer, "%s\n" , op);
            if(0 == strcmp(op, "MESSAGES")){
                if( false == server_not_answering ){
                    printf("Last %zu messages:\n", msg_num);
                    printf("%s",&response_buffer[10]);
                    fflush(stdout);
                }
                else{
                    server_not_answering = false;
                    goto UDP_TEST_END;
                }
            }
            else{
                server_not_answering = true;
                sel_server = NULL;
            }

UDP_END:
            fprintf(stdout, KGRN "Prompt > " KNRM);
            fflush(stdout);
        }
UDP_TEST_END:

        if (FD_ISSET(STDIN_FILENO, &rfds)) { //Stdio input
            scanf("%s%*[ ]%140[^\t\n]" , op, input_buffer); // Grab word, then throw away space and finally grab until \n

            //User options input: show_servers, exit, publish message, show_latest_messages n;
            if (0 == strcasecmp("show_servers", op) || 0 == strcmp("0", op)) {
                print_list(msgservers_lst, print_server);
            } else if (0 == strcasecmp("publish", op) || 0 == strcmp("1", op)) {
                if (0 == strlen(input_buffer)) {
                    continue;
                }
                err = publish(binded_fd, sel_server, input_buffer);
                err = ask_for_messages(binded_fd, sel_server, 0);
                ask_server_test();

            } else if (0 == strcasecmp("show_latest_messages", op) || 0 == strcmp("2", op)) {
                int msg_num_test = atoi(input_buffer);
                if( 0 < msg_num_test) {
                    msg_num = msg_num_test;
                    err = ask_for_messages(binded_fd, sel_server, msg_num);
                    ask_server_test();
                }
                else {
                    printf(KRED "%s is invalid value, must be positive\n" KNRM, input_buffer);
                    msg_num_test = 0;
                }
            } else if (0 == strcasecmp("exit", op) || 0 == strcmp("3", op)) {
                exit_code = EXIT_SUCCESS;
                goto PROGRAM_EXIT;
            } else {
                fprintf(stderr, KRED "%s is an unknown operation\n" KNRM, op);
            }

            bzero(op, STRING_SIZE);
            bzero(input_buffer, STRING_SIZE);

            fprintf(stdout, KGRN "Prompt > " KNRM);
            fflush(stdout);
        }
    }

PROGRAM_EXIT:
    free(response_buffer);
PROGRAM_EXIT_INIT:
    freeaddrinfo(id_server);
    free_incoming_messages();
    free_list(msgservers_lst, free_server);
    close(outgoing_fd);
    close(binded_fd);
    return exit_code;
}
