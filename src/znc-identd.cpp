#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <poll.h>
#include <signal.h>

using namespace std;

const int SUCCEED = 0;
const int FAIL = 1;

map<uint32_t, string> ident_map;

void Exit(const string &reason, int code)
{
	cout << "EXIT " << reason << endl;
	exit(code);
}

uint32_t GetPortKey(int local, int remote)
{
	return (local & 0xFFFF) | ((remote & 0xFFFF) << 16);
}

void ProcessControlMessage()
{
	string line;

	int local_port, remote_port;
	string ident_string;
	string dummy;

	getline(cin, line);

	if (line == "exit")
	{
		Exit("Termination requested.", 0);
	}

	if (line == "reset")
	{
		cout << "Clearing all entries from ident map." << endl;
		ident_map.clear();
		return;
	}

	istringstream in(line);
	in >> local_port;
	if (!in)
	{
		cout << "Invalid first token for control message '" << line << "'" << endl;
		return;
	}

	in >> remote_port;
	if (!in)
	{
		cout << "Invalid second token for control message '" << line << "'" << endl;
		return;
	}

	in >> ident_string;
	if (ident_string.length())
	{
		cout << "Adding entry to ident map: l:" << local_port << " r:" << remote_port << " " << ident_string << endl;
		ident_map[GetPortKey(local_port, remote_port)] = ident_string;
	}
	else
	{
		cout << "Removing entry from ident map: l:" << local_port << " r:" << remote_port << endl;
		ident_map.erase(GetPortKey(local_port, remote_port));
	}

	return;
}

void HandleIdentRequest(int sock)
{
	timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;

	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *) &tv, sizeof tv) < 0)
	{
		cout << "Could not set recieve timeout for spawned socket, aborting." << endl;
		return;
	}

	char buffer[512];

	int received = 0;
	int recv_code = 0;

	for (int i = 0; i < 3; ++i)
	{
		recv_code = recv(sock, buffer + received, 511 - received, 0);
		if (recv_code <= 0) break;

 		received += recv_code;
		buffer[received] = 0;
		if (received == 511) break;

		if (find(buffer, buffer + received, '\n') != buffer + received) break;
	}

	istringstream in(buffer);
	int local_port;
	int remote_port;
	in >> local_port;
	in.ignore(512, ',');
	in >> remote_port;
	if (!in)
	{
		cerr << "Client sent invalid ident request." << endl;
		return;
	}

	ostringstream out;
	map<uint32_t, string>::iterator it = ident_map.find(GetPortKey(local_port, remote_port));
	if (it == ident_map.end()) out << local_port << "," << remote_port << ":ERROR:NO-USER\r\n";
	else out << local_port << "," << remote_port << ":USERID:ZNC:" << it->second << "\r\n";

	send(sock, out.str().data(), out.str().length(), 0);
	cout << "Serviced ident request: " << out.str() << flush;
	return;
}

int main()
{
	const int yes = true;
	const int no = false;

	sockaddr_in6 addr;

	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock < 0) Exit("Could not create socket.", 1);

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) Exit("Could not set SO_REUSEADDR.", 1);
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(int)) < 0) Exit("Could not unset IPV6_V6ONLY.", 1);

	struct sigaction response;
	sigaction(SIGPIPE, NULL, &response);
	response.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &response, NULL);

	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_any;
	addr.sin6_port = htons(113);

	if (bind(sock, (sockaddr *) &addr, sizeof(addr)) < 0) Exit("Could not bind socket.", 1);
	if (listen(sock, 32) < 0) Exit("Could not listen on socket.", 1);

	// we are now ready to begin main loop

	pollfd descriptors[2];
	descriptors[0].fd = STDIN_FILENO;
	descriptors[1].fd = sock;
	descriptors[0].events = POLLIN;
	descriptors[1].events = POLLIN;

	sigset_t sigmask;
	sigfillset(&sigmask);

	for (;;)
	{
		descriptors[0].revents = 0;
		descriptors[1].revents = 0;
		int ppoll_result = ppoll(descriptors, 2, NULL, &sigmask);
		if (ppoll_result < 0) Exit("ppoll() failed.", 1);

		if (descriptors[0].revents != 0)
		{
			ProcessControlMessage();
			continue;
		}
		else if (descriptors[1].revents != 0)
		{
			int newsock = accept(sock, NULL, NULL);
			HandleIdentRequest(newsock);
			close(newsock);
			continue;
		}
	}

	return 0; // never reached
}
