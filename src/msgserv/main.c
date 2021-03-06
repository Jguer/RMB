#include <errno.h>
#include <sys/timerfd.h>
#include "identity.h"
#include "message.h"

bool g_exit = false;

void usage(char* name) {
    fprintf(stdout, "Example Usage: %s –n name –j ip -u upt –t tpt [-i siip] [-p sipt] [–m m] [–r r] %s \n", name, _VERBOSE_OPT_SHOW );
    fprintf(stdout, "Arguments:\n"
            "\t-n\t\tserver name\n"
            "\t-j\t\tserver ip\n"
            "\t-u\t\tserver udp port\n"
            "\t-t\t\tserver tcp port\n"
            "\t-i\t\t[identity server ip (default:tejo.tecnico.ulisboa.pt)]\n"
            "\t-p\t\t[identity server port (default:59000)]\n"
            "\t-m\t\t[max server storage (default:200)]\n"
            "\t-r\t\t[register interval (default:10)]\n"
            "%s", _VERBOSE_OPT_INFO);
    fprintf(stdout, "To force exit send ^C[CTRL+C] twice\n");
}

void put_fd_set(int fd, fd_set *rfds) {
    FD_SET(fd, rfds);
    return;
}
int is_fd_set(int fd, fd_set *rfds) {
    return FD_ISSET(fd, rfds);
}

void ignore_sigpipe()
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    act.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &act, NULL);
}

void handle_intsignal(int sig) {
    if (g_exit == true){
        fprintf(stderr, KRED "\nuser forced exit [SIGINT]\n" KNRM);
        signal(sig, SIG_IGN);
        exit(EXIT_FAILURE);
    }
    g_exit = true;
    fprintf(stderr, KCYN "\nuser requested exit\n" KNRM);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_intsignal);
    ignore_sigpipe();

    int_fast8_t oc, err = 1;
    char *name = NULL;
    char *ip = NULL;
    u_short udp_port = 0;
    u_short tcp_port = 0;
    g_lc = 0;

    char id_server_ip[STRING_SIZE] = "tejo.tecnico.ulisboa.pt";
    char id_server_port[STRING_SIZE] = "59000";
    char buffer[STRING_SIZE];

    int_fast16_t m = 200, r = 10;
    bool is_join_complete = false;
    bool print_prompt = true;
    fd_set rfds;

    int_fast16_t tcp_listen_fd = -1, udp_global_fd = -1, udp_register_fd = -1, timer_fd = -1, max_fd = -1;
    uint_fast8_t exit_code = EXIT_SUCCESS;

    int_fast32_t read_size = 0;
    bool daemon_mode = false;

    srand(time(NULL));
    // Treat options
    while ((oc = getopt(argc, argv, "n:j:u:t:i:p:m:r:hvd")) != -1) { //Command-line args parsing, 'i' and 'p' args required for both
        switch (oc) {
            case 'd':
                daemon_mode = true;
                break;
            case 'n':
                name = (char *)alloca(strlen(optarg) +1);
                strncpy(name, optarg, strlen(optarg) + 1); //optarg has the string corresponding to oc value
                break;
            case 'j':
                ip = (char *)alloca(strlen(optarg) +1);
                strncpy(ip, optarg, strlen(optarg) + 1);
                break;
            case 'u':
                udp_port = (u_short)atoi(optarg);
                break;
            case 't':
                tcp_port = (u_short)atoi(optarg);
                break;
            case 'i':
                strncpy(id_server_ip, optarg, strlen(optarg) + 1);
                break;
            case 'p':
                strncpy(id_server_port, optarg, strlen(optarg) + 1);
                break;
            case 'm':
                m = atoi(optarg);
                break;
            case 'r':
                r = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                exit_code = EXIT_FAILURE;
                goto PROGRAM_EXIT;
                break;
            case ':':
                /* missing option argument */
                fprintf(stderr, "%s: option '-%c' requires an argument\n",
                        argv[0], optopt);
                break;
            case 'v':
                _VERBOSE_OPT_CHECK;
            case '?':
            default:
                usage(argv[0]);
                exit_code = EXIT_SUCCESS;
                goto PROGRAM_EXIT;
        }
    }

    if ((NULL == name) || (NULL == ip) || (0 == udp_port) || (0 == tcp_port)){
        printf("Required arguments not present\n");
        usage(argv[0]);
        exit_code = EXIT_SUCCESS;
        goto PROGRAM_EXIT;
    }


    struct itimerspec new_timer = {{r,0}, {r,0}};
    server host = new_server(name, ip, udp_port, tcp_port); //host parameters

    /* Start Timer */
    timer_fd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd == -1) {
        printf(KRED "Unable to create timer.\n" KNRM);
    }

    udp_global_fd = init_udp(host); //Initiates UDP connection
    tcp_listen_fd = init_tcp(host); //Initiates TCP connection

    if ( 0 >= udp_global_fd || 0 >= tcp_listen_fd){
        fprintf(stdout, KYEL "Cannot initializate UDP and/or TCP connections\n" KGRN
                "Check the ip address and ports\n" KNRM) ;
        g_exit = 1;
        exit_code = EXIT_FAILURE;
    }

    matrix msg_matrix = create_matrix(m);
    list msgsrv_list = create_list();

    fprintf(stdout, KBLU "Server Parameters:" KNRM " %s:%s:%d:%d\n"
            KBLU "Identity Server:" KNRM " %s:%s\n"
            KGRN "Prompt@NotConnected > " KNRM
            ,name, ip, udp_port, tcp_port, id_server_ip, id_server_port);
    fflush(stdout);

    if (daemon_mode && !g_exit) {
        err = handle_join(msgsrv_list, &udp_register_fd, host, id_server_ip, id_server_port);
        if (err) {
            fprintf(stderr, KRED "Unable to join. Error code %d\n" KNRM, err);
        } else {
            is_join_complete = true;
            timerfd_settime (timer_fd, 0, &new_timer, NULL);
        }
    }
    // Processing Loop
    while(!g_exit) {
        print_prompt = false;
        //Clear the set
        FD_ZERO(&rfds);

        //Add tcp_listen_fd, udp_global_fd and stdio to the socket set
        FD_SET(STDIN_FILENO, &rfds);
        if (is_join_complete) {
            FD_SET(timer_fd, &rfds);
            FD_SET(udp_global_fd, &rfds);
            FD_SET(tcp_listen_fd, &rfds);
            max_fd = tcp_listen_fd > udp_global_fd ? tcp_listen_fd : udp_global_fd;
            max_fd = timer_fd > max_fd ? timer_fd : max_fd;
        } else {
            max_fd = STDIN_FILENO;
        }

        //Removes the bad servers and sets the good in fd_set rfds.
        max_fd = remove_bad_servers(msgsrv_list, host, max_fd, &rfds, put_fd_set);

        //wait for one of the descriptors is ready
        int activity = select(max_fd + 1 , &rfds, NULL, NULL, NULL); //Select, threading function
        if(0 > activity){
            if (_VERBOSE_TEST) printf("error on select\n%d\n", errno);
            break;
        }

        if (FD_ISSET(timer_fd, &rfds)) { //if the timer is triggered
            update_reg(udp_register_fd, id_server);
            timerfd_settime (timer_fd, 0, &new_timer, NULL);
        }


        if ( 0 != tcp_new_comm(msgsrv_list, tcp_listen_fd, &rfds, is_fd_set)) { //Starts connection for incoming requests
            break;
        }

        //if something happened on other socket we must process it
        if (FD_ISSET(STDIN_FILENO, &rfds)) { //Stdio input
            print_prompt = true;
            read_size = read(0, buffer, STRING_SIZE);
            if (0 > read_size) {
                if (_VERBOSE_TEST) printf("error reading from stdio\n");
                break;
            } else if (0 == read_size || (1 == read_size && '\n' == buffer[0])){
                fprintf(stderr, KRED"please input something\n" KNRM);
            } else {       
                if ('\n' == buffer[read_size - 1]) buffer[read_size - 1] = '\0'; //switches \n to \0
                //User options input: show_servers, exit, publish message, show_latest_messages n;
                if (strcasecmp("join", buffer) == 0 || 0 == strcmp("1", buffer)) {
                    if (!is_join_complete) { //Register on idServer
                        err = handle_join(msgsrv_list, &udp_register_fd, host, id_server_ip, id_server_port);
                        if (err && 1 != g_exit) {
                            fprintf(stderr, KRED "Unable to join. Error code %d\n" KNRM, err);
                        } else {
                            is_join_complete = true;
                            timerfd_settime (timer_fd, 0, &new_timer, NULL);
                        }
                    }
                    else {
                        printf(KGRN "Already joined!\n" KNRM);
                    }
                } else if (0 == strcasecmp("show_servers", buffer) || 0 == strcmp("2", buffer)) {
                    if (msgsrv_list != NULL && 0 != get_list_size(msgsrv_list)) print_list(msgsrv_list, print_server);
                    else printf("No registered servers\n");
                } else if (0 == strcasecmp("show_messages", buffer) || 0 == strcmp("3", buffer)) {
                    if (0 == get_size(msg_matrix) && false == get_overflow(msg_matrix)) {
                        printf("0 messages received\n");
                    } else {
                        print_matrix(msg_matrix, print_message);
                    }
                } else if (0 == strcasecmp("exit", buffer) || 0 == strcmp("4", buffer)) {
                    g_exit = true;
                    print_prompt = false;
                } else {
                    fprintf(stderr, KRED "%s is an unknown operation\n" KNRM, buffer);
                }
            }
        }

        if (FD_ISSET(udp_global_fd, &rfds)){ //UDP communications handling
            err = handle_client_comms(udp_global_fd, msg_matrix);
            if (2 == err) {
                share_last_message(msgsrv_list, msg_matrix);
            }
        }

        for_each_element(msgsrv_list, server_treat_communications, (void*[]){(void *)msg_matrix, (void *)&rfds});

        if (print_prompt && 1 != g_exit) {
            if (is_join_complete) fprintf(stdout, KGRN "\nPrompt@%s > " KNRM, get_name(host));
            else fprintf(stdout, KGRN "Prompt@NotConnected > " KNRM);
            fflush(stdout);
        }
    }

    close_fd(tcp_listen_fd);
    close_fd(udp_global_fd);
    close_fd(udp_register_fd);
    close_fd(timer_fd);
    free_server(host);
    free_list(msgsrv_list, free_server);
    free_matrix(msg_matrix, free_message);
    freeaddrinfo(id_server);
PROGRAM_EXIT:
    return exit_code;
}
