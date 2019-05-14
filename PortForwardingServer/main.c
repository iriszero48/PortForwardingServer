#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <err.h>
#include <pthread.h>
#include <stdbool.h>
#include <math.h>
#include <arpa/inet.h>

static uint64_t recvCount, sendCount;

void TimePrinter()
{
	time_t t = time(NULL);
	struct tm* lt = localtime(&t);
	fflush(stdout);
	printf("[ %d-%d-%d %d:%d:%d ] ", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min,
		lt->tm_sec);
}

void Show()
{
	while (true)
	{
		fflush(stdout);
		printf("\r\033[k");
		printf("recv:%.fkB - send:%.fkB", round(recvCount / 1024.), round(sendCount / 1024.));
		sleep(1);
	}
}

void Transfer(void* argv)
{
	void* buf[1024] = { 0 };
	int len;
	while ((len = read(((int*)argv)[0], buf, 1024)) > 0)
	{
		write(((int*)argv)[1], buf, len);
		if (((int*)argv)[2]) recvCount += len;
		else sendCount += len;
	}
	close(((int*)argv)[0]);
	close(((int*)argv)[1]);
}

int main(const int argc, char* argv[])
{
	if (argc != 3) err(1, "Usage: PortForwardingServer UserPort ClientPort.\n");
	TimePrinter();
	printf("init...\n");
	int one = 1;

	//User listen
	struct sockaddr_in userSerAddr, userCliAddr;
	socklen_t socklen = sizeof userCliAddr;
	const int userSock = socket(AF_INET, SOCK_STREAM, 0);
	if (userSock < 0) err(1, "(user)Can't open socket %d.", ntohs(userSerAddr.sin_port));
	setsockopt(userSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	userSerAddr.sin_family = AF_INET;
	userSerAddr.sin_addr.s_addr = INADDR_ANY;
	userSerAddr.sin_port = htons(atoi(argv[1]));
	if (bind(userSock, (struct sockaddr *)&userSerAddr, sizeof userSerAddr) == -1)
	{
		close(userSock);
		err(1, "(user)Can't bind %d.", ntohs(userSerAddr.sin_port));
	}
	listen(userSock, 5);

	//Client listen
	struct sockaddr_in clientSerAddr, clientCliAddr;
	const int clientSock = socket(AF_INET, SOCK_STREAM, 0);
	if (clientSock < 0) err(1, "(client)Can't open socket %d.", ntohs(clientSerAddr.sin_port));
	setsockopt(clientSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	clientSerAddr.sin_family = AF_INET;
	clientSerAddr.sin_addr.s_addr = INADDR_ANY;
	clientSerAddr.sin_port = htons(atoi(argv[2]));
	if (bind(clientSock, (struct sockaddr *)&clientSerAddr, sizeof(clientSerAddr)) == -1)
	{
		close(clientSock);
		err(1, "(client)Can't bind %d.", ntohs(clientSerAddr.sin_port));
	}
	listen(clientSock, 5);

	//Start log
	pthread_t logThread;
	if (pthread_create(&logThread, NULL, (void *)&Show, NULL) != 0) perror("(log)Can't create thread");
	else
	{
		TimePrinter();
		printf("(log)Thread start.\n");
	}

	while (true)
	{
		TimePrinter();
		printf("Link start...\n");

		//User accept
		const int userCliFd = accept4(userSock, (struct sockaddr *)&userCliAddr, &socklen, SOCK_CLOEXEC);
		TimePrinter();
		printf("(user)Got connection %d.\n", ntohs(userSerAddr.sin_port));
		if (userCliFd == -1) perror("(user)Can't accept.");

		//Client accept
		const int clientCliFd = accept4(clientSock, (struct sockaddr *)&clientCliAddr, &socklen, SOCK_CLOEXEC);
		TimePrinter();
		printf("(client)Got connection %d.\n", ntohs(clientSerAddr.sin_port));
		if (clientCliFd == -1) perror("(client)Can't accept.");

		//Swap data
		pthread_t user2clientThread, client2userThread2;
		void* ret;
		int user2client[3] = { userCliFd, clientCliFd, false };
		int client2user[3] = { clientCliFd, userCliFd, true };
		if (pthread_create(&user2clientThread, NULL, (void *)&Transfer, (void*)user2client) != 0)
			perror("(user2client)Can't create thread.");
		if (pthread_create(&client2userThread2, NULL, (void *)&Transfer, (void*)client2user) != 0)
			perror("(client2user)Can't create thread.");
		TimePrinter();
		printf("Swaping (user)%s:%d <=> (client)%s:%d.\n", inet_ntoa(userCliAddr.sin_addr), ntohs(userCliAddr.sin_port),
			inet_ntoa(clientCliAddr.sin_addr), ntohs(clientCliAddr.sin_port));
		if (pthread_join(user2clientThread, &ret) != 0) perror("(user2client)Can't join with thread.");
		if (pthread_join(client2userThread2, &ret) != 0) perror("(client2user)Can't join with thread.");
		TimePrinter();
		printf("Drop.\n");
	}
}
