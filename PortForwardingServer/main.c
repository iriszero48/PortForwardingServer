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
	struct sockaddr_in inSerAddr, inCliAddr;
	socklen_t socklen = sizeof inCliAddr;
	const int inSock = socket(AF_INET, SOCK_STREAM, 0);
	if (inSock < 0) err(1, "(in)Can't open socket %d.", ntohs(inSerAddr.sin_port));
	setsockopt(inSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	inSerAddr.sin_family = AF_INET;
	inSerAddr.sin_addr.s_addr = INADDR_ANY;
	inSerAddr.sin_port = htons(atoi(argv[1]));
	if (bind(inSock, (struct sockaddr *)&inSerAddr, sizeof inSerAddr) == -1)
	{
		close(inSock);
		err(1, "(in)Can't bind %d.", ntohs(inSerAddr.sin_port));
	}
	listen(inSock, 5);

	//Client listen
	struct sockaddr_in outSerAddr, outCliAddr;
	const int outSock = socket(AF_INET, SOCK_STREAM, 0);
	if (outSock < 0) err(1, "(out)Can't open socket %d.", ntohs(outSerAddr.sin_port));
	setsockopt(outSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	outSerAddr.sin_family = AF_INET;
	outSerAddr.sin_addr.s_addr = INADDR_ANY;
	outSerAddr.sin_port = htons(atoi(argv[2]));
	if (bind(outSock, (struct sockaddr *)&outSerAddr, sizeof(outSerAddr)) == -1)
	{
		close(outSock);
		err(1, "(out)Can't bind %d.", ntohs(outSerAddr.sin_port));
	}
	listen(outSock, 5);

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
		const int inCliFd = accept4(inSock, (struct sockaddr *)&inCliAddr, &socklen, SOCK_CLOEXEC);
		TimePrinter();
		printf("(in)Got connection %d.\n", ntohs(inSerAddr.sin_port));
		if (inCliFd == -1) perror("(in)Can't accept.");

		//Client accept
		const int outCliFd = accept4(outSock, (struct sockaddr *)&outCliAddr, &socklen, SOCK_CLOEXEC);
		TimePrinter();
		printf("(out)Got connection %d.\n", ntohs(outSerAddr.sin_port));
		if (outCliFd == -1) perror("(out)Can't accept.");

		//Swap data
		pthread_t i2oThread, o2iThread2;
		void* ret;
		int i2o[3] = { inCliFd, outCliFd, false };
		int o2i[3] = { outCliFd, inCliFd, true };
		if (pthread_create(&i2oThread, NULL, (void *)&Transfer, (void*)i2o) != 0)
			perror("(in2out)Can't create thread.");
		if (pthread_create(&o2iThread2, NULL, (void *)&Transfer, (void*)o2i) != 0)
			perror("(out2in)Can't create thread.");
		TimePrinter();
		printf("Swaping User-%s:%d <=> Client-%s:%d.\n", inet_ntoa(inCliAddr.sin_addr), ntohs(inCliAddr.sin_port),
			inet_ntoa(outCliAddr.sin_addr), ntohs(outCliAddr.sin_port));
		if (pthread_join(i2oThread, &ret) != 0) perror("(in2out)Can't join with thread.");
		if (pthread_join(o2iThread2, &ret) != 0) perror("(out2in)Can't join with thread.");
		TimePrinter();
		printf("Drop.\n");
	}
}
