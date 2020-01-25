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
	
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = AI_PASSIVE;   /* For wildcard IP address */

	struct addrinfo *result;

	char portStr[8];
	snprintf(portStr, sizeof(portStr), "%i", port);
	
	int error = getaddrinfo(nullptr, portStr, &hints, &result);
	if (error != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
	}

	int sockfd = -1;
	for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
			sockfd = socket(rp->ai_family, rp->ai_socktype,
					rp->ai_protocol);
		if (sockfd != -1) {

			if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
			else {
				perror("bind failed");
				close(sockfd);
				sockfd = -1;
			}
		}
	}
	freeaddrinfo(result);

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
