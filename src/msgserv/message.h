#include "../utils/struct_server.h"
#include "../utils/utils.h"
#include "../utils/struct_message.h"
#include <alloca.h>

#define MESSAGE_CODE "MESSAGES"
#define SMESSAGE_CODE "SMESSAGES"

//TCP
uint_fast8_t tcp_fd_handle(list servers_list, matrix msg_matrix, fd_set *rfds, int (*STAT_FD)(int, fd_set *));
uint_fast8_t parse_messages(matrix msg_matrix);
uint_fast8_t handle_sget_messages(int fd, matrix msg_matrix);
uint_fast8_t share_last_message(list servers_list, matrix msg_matrix);
void server_treat_communications(item obj, void *cnt_array[]);

/*! \fn handle_client_comms(int fd, matrix msg_matrix)
	\brief handle_client_comms receives the comunications via udp from the client.
then interprets and sends back the requested info or saves the new message
	\param fd File descriptor for udp comms
	\param msg_matrix Structure to allocate messages
*/
//UDP
uint_fast8_t handle_client_comms(int fd, matrix msg_matrix);
uint_fast8_t handle_publish(matrix msg_matrix, char *input_buffer);
