// Because netcat is garbage at listening on UDP



#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

using std::cout; using std::cerr; using std::endl;


void daSocket(int port) {
	
	
	struct sockaddr_in6 servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin6_family = AF_INET6;
	servaddr.sin6_addr = in6addr_any;
	servaddr.sin6_port = htons(port);
	
	int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);	
	if (sockfd != -1) {
		int no = 0;     
		if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) < 0) {
			perror("setsockopt");
		}

		if (bind(sockfd, (sockaddr *) &servaddr, sizeof(servaddr)) != 0) {
			perror("bind failed");
			close(sockfd);
			sockfd = -1;
		}
	}

	if (sockfd == -1) {
		std::cerr << "Could not connect" << std::endl;
		return;
	}

	while (true) {
		char buf[66537];
		ssize_t recieveSize = recvfrom(sockfd, buf, sizeof(buf) - 1,
		0, nullptr, nullptr);
		if (recieveSize > 0) {
			
			write(STDOUT_FILENO, buf, recieveSize);
		}
		else if (recieveSize < 0) {
			perror("Recieve error");
		}
		else std::cerr << "empty packet??" << std::endl;
	}
}

void usageAndExit() {
	cerr << "Non-ass-udp-listener: Listens for all udp packets sent to specified port, sending it to stdout. Because netcat is garbage at that.\nUsage: naudpl <port>" << endl;
	exit(1);
}

int main(int argc, const char * argv[]) {
	
	if (argc < 2) usageAndExit();
	int port = atoi(argv[1]);
	if (port <= 0) usageAndExit();
	
	cerr << "listening for udp packets at port " << port << endl;
	
	while (true) daSocket(port);
	
	return 0;
}
