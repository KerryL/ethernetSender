// File:  main.cpp
// Date:  10/29/2018
// Auth:  K. Loux

// Local headers
#include "utilities/cppSocket.h"

// Standard C++ headers
#include <string>
#include <sstream>
#include <iomanip>
#include <cassert>

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

	s.append(rawMessage.substr(lastP));

	return s;
}

enum class Protocol
{
	TCP,
	UDP,
	UDPBroadcast,
	WOL,
	Unknown
};

bool SendEthernetMessage(const Protocol& protocol, const std::string& targetIP,
	const unsigned short& targetPort, const std::string& message, const bool& ignoreResponse)
{
	const CPPSocket::SocketType type([protocol]()
	{
		if (protocol == Protocol::TCP)
			return CPPSocket::SocketType::SocketTCPClient;
		return CPPSocket::SocketType::SocketUDPClient;
	}());

	CPPSocket socket(type);
	if (type == CPPSocket::SocketTCPClient)
	{
		if (!socket.Create(targetPort, targetIP))
			return false;
		if (!socket.TCPSend(reinterpret_cast<const CPPSocket::DataType*>(message.c_str()), message.length()))
			return false;
	}
	else
	{
		if (!socket.Create(0, std::string()))
			return false;
		if (protocol == Protocol::UDPBroadcast || protocol == Protocol::WOL)
		{
			int trueflag(1);
			if (!socket.SetOption(SOL_SOCKET, SO_BROADCAST, reinterpret_cast<CPPSocket::DataType*>(&trueflag), sizeof(trueflag)))
				return false;
		}
		if (!socket.UDPSend(targetIP.c_str(), targetPort, reinterpret_cast<const CPPSocket::DataType*>(message.c_str()), message.length()))
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
		{
			std::cout << "Received empty response (connection closed)\n";
			return true;
		}
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

std::string BuildMagicPacket(const std::string& macAddress)
{
	assert(macAddress.length() == 6);
	std::string magicPacket;
	magicPacket.append(6, static_cast<uint8_t>(0xFF));
	for (unsigned int i = 0; i < 16; ++i)
		magicPacket.append(macAddress);
	assert(magicPacket.length() == 102);
	return magicPacket;
}

std::string GetProtocolString(const Protocol& p)
{
	if (p == Protocol::TCP)
		return "TCP";
	else if (p == Protocol::UDP)
		return "UDP";
	else if (p == Protocol::UDPBroadcast)
		return "UDP Broadcast";
	else if (p == Protocol::WOL)
		return "WOL";
	return "Unknown";
}

struct Arguments
{
	std::string targetIP;
	unsigned short targetPort;
	Protocol protocol;
	bool ignoreResponse = false;
	std::string message;
};

bool ParseArguments(const int argc, char* argv[], Arguments& arguments)
{
	{
		std::istringstream ss(argv[2]);
		if ((ss >> arguments.targetPort).fail())
		{
			std::cerr << "Failed to parse target port\n";
			return false;
		}
	};

	arguments.protocol = Protocol::Unknown;
	const std::string protocolString(argv[3]);
	if (protocolString == std::string("tcp"))
		arguments.protocol = Protocol::TCP;
	else if (protocolString == std::string("udp"))
		arguments.protocol = Protocol::UDP;
	else if (protocolString == std::string("udp-broadcast"))
		arguments.protocol = Protocol::UDPBroadcast;
	else if (protocolString == std::string("wol"))
		arguments.protocol = Protocol::WOL;

	if (arguments.protocol == Protocol::Unknown)
	{
		std::cerr << "Unknown protocol specified\n";
		return false;
	}

	if (arguments.protocol == Protocol::UDPBroadcast || arguments.protocol == Protocol::WOL)
		arguments.targetIP = CPPSocket::GetBroadcastAddress(argv[1]);
	else
		arguments.targetIP = argv[1];

	if (arguments.targetIP.empty())
	{
		std::cerr << "Invalid IP address\n";
		return false;
	}

	arguments.ignoreResponse = arguments.protocol == Protocol::WOL;
	int firstPayloadArgument(4);
	if (!arguments.ignoreResponse)
	{
		const std::string ignoreFlag("--ignore-response");
		if (ignoreFlag.compare(argv[4]) == 0)
		{
			arguments.ignoreResponse = true;
			++firstPayloadArgument;
		}
	}

	std::cout << "Sending " << GetProtocolString(arguments.protocol) << " message to " << arguments.targetIP << ":" << arguments.targetPort << std::endl;

	for (int i = firstPayloadArgument; i < argc; ++i)
	{
		arguments.message.append(argv[i]);
		if (i + 1 < argc)
			arguments.message.append(" ");
	}

	arguments.message = AdjustMessage(arguments.message);// Resolve values specified as hex bytes
	if (arguments.protocol == Protocol::WOL)
	{
		if (arguments.message.length() != 6)
		{
			std::cerr << "MAC address must be exactly six bytes\n";
			return false;
		}
		arguments.message = BuildMagicPacket(arguments.message);
	}

	return true;
}

int main(int argc, char* argv[])
{
	if (argc < 5)
	{
		std::cout << "Usage:  " << argv[0] << " <ip address> <port> <tcp, udp, upd-broadcast> [--ignore-response] <payload>\n";
		std::cout << "        or, for Wake-On-LAN:";
		std::cout << "        " << argv[0] << " <ip address> <port> wol <MAC address>\n";
		std::cout << "        Use \\x## to represent a hex byte in the payload\n";
		return 1;
	}

	Arguments arguments;
	if (!ParseArguments(argc, argv, arguments))
		return 1;

	std::cout << "Message is '" << arguments.message << '\'' << std::endl;
	if (!SendEthernetMessage(arguments.protocol, arguments.targetIP, arguments.targetPort, arguments.message, arguments.ignoreResponse))
		return 1;
	return 0;
}
