// File:  main.cpp
// Date:  10/29/2018
// Auth:  K. Loux

// Local headers
#include "utilities/cppSocket.h"

// Standard C++ headers
#include <string>
#include <sstream>
#include <iomanip>

std::string AdjustMessage(const std::string& rawMessage)
{
	std::string s;
	std::string::size_type p(0), lastP(0);
	while (p = rawMessage.find("\\x", lastP), p != std::string::npos)
	{
		s.append(rawMessage.substr(lastP, p - lastP));
		p += 2;
		lastP = p + 2;

		const unsigned char byte(static_cast<unsigned char>(strtoul(rawMessage.substr(p, 2).c_str(), NULL, 16)));
		s.append(std::string(1, byte));
	}

	return s;
}

bool SendEthernetMessage(const bool& useTCP, const bool& broadcast, const std::string& targetIP,
	const unsigned short& targetPort, const std::string& message, const bool& ignoreResponse)
{
	const CPPSocket::SocketType type([useTCP]()
	{
		if (useTCP)
			return CPPSocket::SocketType::SocketTCPClient;
		return CPPSocket::SocketType::SocketUDPClient;
	}());

	CPPSocket socket(type);
	const auto adjMessage(AdjustMessage(message));

	if (useTCP)
	{
		if (!socket.Create(targetPort, targetIP))
			return false;
		if (!socket.TCPSend(reinterpret_cast<const CPPSocket::DataType*>(adjMessage.c_str()), adjMessage.length()))
			return false;
	}
	else
	{
		if (!socket.Create(0, std::string()))
			return false;
		if (broadcast)
		{
			int trueflag(1);
			if (!socket.SetOption(SOL_SOCKET, SO_BROADCAST, reinterpret_cast<CPPSocket::DataType*>(&trueflag), sizeof(trueflag)))
				return false;
		}
		if (!socket.UDPSend(targetIP.c_str(), targetPort, reinterpret_cast<const CPPSocket::DataType*>(adjMessage.c_str()), adjMessage.length()))
			return false;
	}

	std::cout << "Message sent, waiting for response" << std::endl;

	socket.SetBlocking(true);
	while (!ignoreResponse)
	{
		struct sockaddr_in sender;
		const auto msgSize(socket.Receive(&sender));
		if (msgSize == SOCKET_ERROR)
		{
			std::cout << "Receive failed:  " << socket.GetErrorString() << std::endl;
			return false;
		}
		else if (msgSize == 0)
			std::cout << "Received empty response\n";
		else
		{
			std::cout << "Response(" << msgSize << " bytes) from " << inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << " =\n";
			const auto response(std::string(socket.GetLastMessage(), msgSize));
			for (const auto& c : response)
				std::cout << "0x" << std::hex << std::setfill('0') << std::setw(2) << c << " ";
			std::cout << std::endl;
			std::cout << std::dec;
		}
	}

	return true;
}

int main(int argc, char* argv[])
{
	if (argc < 5)
	{
		std::cout << "Usage:  " << argv[0] << " <ip address> <port> <tcp or udp> [--ignore-response] <payload>\n";
		std::cout << "        Use \\x## to represent a hex byte\n";
		return 1;
	}

	const std::string targetIP(argv[1]);
	const unsigned short targetPort([argv]() -> unsigned short
	{
		std::istringstream ss(argv[2]);
		unsigned short port;
		if ((ss >> port).fail())
			return 0;
		return port;
	}());

	if (targetPort == 0)
	{
		std::cerr << "Failed to parse port nubmer\n";
		return 1;
	}

	const bool useTCP([argv]()
	{
		const std::string protocol(argv[3]);
		return protocol.compare("tcp") == 0;
	}());

	const bool broadcast([argv]()
	{
		const std::string protocol(argv[3]);
		return protocol.compare("udp-broadcast") == 0;
	}());

	const std::string ignoreFlag("--ignore-response");
	bool ignoreResponse(false);
	int firstPayloadArgument(4);
	if (ignoreFlag.compare(argv[4]) == 0)
	{
		ignoreResponse = true;
		++firstPayloadArgument;
	}

	std::cout << "Sending ";
	if (useTCP)
		std::cout << "TCP";
	else
		std::cout << "UDP";
	std::cout << " message to " << targetIP << ":" << targetPort;
	
	if (broadcast)
		std::cout << " (broadcast)";
	std::cout << std::endl;

	std::string message;
	for (int i = firstPayloadArgument; i < argc; ++i)
	{
		message.append(argv[i]);
		if (i + 1 < argc)
			message.append(" ");
	}

	std::cout << "Message is '" << message << '\'' << std::endl;
	if (!SendEthernetMessage(useTCP, broadcast, targetIP, targetPort, message, ignoreResponse))
		return 1;
	return 0;
}
