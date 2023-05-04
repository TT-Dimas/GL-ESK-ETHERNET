
#include "main.h"
#include "lwip.h"
#include "sockets.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>


#define PORTNUM 5678UL
#define USE_UDP_SERVER_PRINTF 1

#define UDP_CLIENT_ADRESS "192.168.0.105"

#if (USE_UDP_SERVER_PRINTF == 1)
#include <stdio.h>
#define UDP_SERVER_PRINTF(...) do { printf("[udp_server.c: %s: %d]: ",__func__, __LINE__);printf(__VA_ARGS__); } while (0)
#else
#define UDP_SERVER_PRINTF(...)
#endif

static struct sockaddr_in serv_addr, client_addr;
static int socket_fd;
static uint16_t nport;
static int addr_len;


static int UdpServerInit(void) {
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd == -1) {
		UDP_SERVER_PRINTF("socket() error\n");
		return -1;
	}

	nport = PORTNUM;
	nport = htons((uint16_t )nport);

	bzero(&serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = nport;

	if (bind(socket_fd, (struct sockaddr* )&serv_addr, sizeof(serv_addr))
			== -1) {
		UDP_SERVER_PRINTF("bind() error\n");
		close(socket_fd);
		return -1;
	}

	UDP_SERVER_PRINTF("Server is ready\n");

	return 0;
}

typedef enum {
	COMMAND_ERROR_UNKNOWN = -1,
	COMMAND_ERROR_WRONG_FORMAT = - 2,
	COMMAND_ERROR_LED_NUMBER = -3,
	COMMAND_ERROR_LED_CMD = -4,
	COMMAND_ERROR_TEXT = -5,
	COMMAND_OK = 0,

}commandErrorTypedef;



static commandErrorTypedef commandHandler(const uint8_t *buffer, size_t len){


    int num;
	char cmd[sizeof("sversion")];
	uint8_t *message[] = { "OK\r\n", "ERROR\r\n" };
	uint8_t *led_status[] = { "OFF\r\n", "ON\r\n"};

	typedef enum {
		COMMAND_LED = 0,
		COMMAND_TEXT = 1,
	} commandTypedef;

	typedef enum {
		OK = 0,
		ERROR = 1,
	} messageReternedTypedef;

	commandTypedef command;

	Led_TypeDef led[4] = { LED3, LED4, LED5, LED6 };

	if (sscanf(buffer, "led%d %s", &num, cmd) == 2) {
		command = COMMAND_LED;
	} else if (sscanf(buffer, "%s", cmd) == 1) {
		command = COMMAND_TEXT;
	} else{
		sendto(socket_fd, message[ERROR], strlen(message[ERROR]), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);
		return COMMAND_ERROR_WRONG_FORMAT;
	}
	if (command == COMMAND_LED) {
		if (num < 3 || num > 6) {
			sendto(socket_fd, message[ERROR], strlen(message[ERROR]), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);
			return COMMAND_ERROR_LED_NUMBER;
		}

		if (strncmp("on", cmd, sizeof(cmd)) == 0) {
			BSP_LED_On(led[num - 3]);
			printf("LED[%d] ON\nOK\n", num);
		} else if (strncmp("off", cmd, sizeof(cmd)) == 0) {
			BSP_LED_Off(led[num - 3]);
			printf("LED[%d] OFF\nOK\n", num);
		} else if (strncmp("toggle", cmd, sizeof(cmd)) == 0) {
			BSP_LED_Toggle(led[num - 3]);
			printf("LED[%d] TOGGLE\nOK\n", num);
		} else if (strncmp("status", cmd, sizeof(cmd)) == 0) {
			char msg[16];
			int status = (int)BSP_LED_Status(led[num - 3]);
			snprintf(msg, sizeof(msg), "LED%d %s", num, led_status[!!status]);
			printf("LED%d %s\n",num, led_status[!!status]);
			sendto(socket_fd, msg, strlen(msg), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);
		}else {
			sendto(socket_fd, message[ERROR], strlen(message[ERROR]), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);
			return COMMAND_ERROR_LED_CMD;
		}
	}
	else if (command == COMMAND_TEXT) {
		if (strncmp("sversion", cmd, sizeof(cmd)) == 0) {
			char msg[] = "udp_srv_dmytro_synko_16.04.2023\n";
			printf("%s", msg);
			sendto(socket_fd, msg, strlen(msg), MSG_DONTWAIT,
					(const struct sockaddr* )&client_addr, addr_len);
		}
		else {
			sendto(socket_fd, message[ERROR], strlen(message[ERROR]), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);
			return COMMAND_ERROR_TEXT;
		}
	}
	else{
		sendto(socket_fd, message[ERROR], strlen(message[ERROR]), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);
		return COMMAND_ERROR_UNKNOWN;
	}
	sendto(socket_fd, message[OK], strlen(message[OK]), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len);

	return COMMAND_OK;
}

void StartUdpServerTask(void const *argument) {




	osDelay(5000); // wait 5 sec to init lwip stack

	if (UdpServerInit() < 0) {
		UDP_SERVER_PRINTF("udpSocketServerInit() error\n");
		return;
	}
	UDP_SERVER_PRINTF("udpSocketServerInit() OK\n");
	for (;;) {
		bzero(&client_addr, sizeof(client_addr));
		addr_len = sizeof(client_addr);


		//Example of code from https://man7.org/linux/man-pages/man2/select.2.html
		fd_set rfds;
		struct timeval tv;
		int retval;

		/* Watch stdin (fd 0) to see when it has input. */

		FD_ZERO(&rfds);
		FD_SET(socket_fd, &rfds);

		/* Wait up to five seconds. */

		tv.tv_sec = 5;
		tv.tv_usec = 0;

		retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
		/* Don't rely on the value of tv now! */

		if (retval == -1) {
			close(socket_fd);
			break;

		} else if (retval) {

			if (FD_ISSET(socket_fd, &rfds)) {

				ssize_t received;
				uint8_t buffer[32];
				const size_t buf_size = sizeof(buffer);
				received = recvfrom(socket_fd, buffer, buf_size, MSG_DONTWAIT,
						(struct sockaddr* ) &client_addr,
						(socklen_t* ) &addr_len);
				if (received > 0) {
					commandErrorTypedef returnedVal;

					if ((returnedVal = commandHandler(buffer, received)) != COMMAND_OK) {
						UDP_SERVER_PRINTF("commandHandler() return error code = %d\n", returnedVal);
					}
				}

			}

		} else
			printf("No data within five seconds.\n");

	}
}
