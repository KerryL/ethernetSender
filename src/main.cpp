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
#include <chrono>

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

struct Arguments
{
	std::string targetIP;
	unsigned short targetPort;
	Protocol protocol;
	bool ignoreResponse = false;
	bool plainTextResponse = false;
	std::string message;
	unsigned int responseWait = 0;// [ms]
};

bool SendEthernetMessage(const Arguments& arguments)
{
	const CPPSocket::SocketType type([arguments]()
	{
		if (arguments.protocol == Protocol::TCP)
			return CPPSocket::SocketType::SocketTCPClient;
		return CPPSocket::SocketType::SocketUDPClient;
	}());

	CPPSocket socket(type);
	if (type == CPPSocket::SocketTCPClient)
	{
		if (!socket.Create(arguments.targetPort, arguments.targetIP))
			return false;
		if (!socket.TCPSend(reinterpret_cast<const CPPSocket::DataType*>(arguments.message.c_str()), arguments.message.length()))
			return false;
	}
	else
	{
		if (!socket.Create(0, std::string()))
			return false;
		if (arguments.protocol == Protocol::UDPBroadcast || arguments.protocol == Protocol::WOL)
		{
			int trueflag(1);
			if (!socket.SetOption(SOL_SOCKET, SO_BROADCAST, reinterpret_cast<CPPSocket::DataType*>(&trueflag), sizeof(trueflag)))
				return false;
		}
		if (!socket.UDPSend(arguments.targetIP.c_str(), arguments.targetPort, reinterpret_cast<const CPPSocket::DataType*>(arguments.message.c_str()), arguments.message.length()))
			return false;
	}

	std::cout << "Message sent, waiting for response" << std::endl;

	socket.SetBlocking(arguments.responseWait == 0);

	auto timerStart(std::chrono::steady_clock::now());
	while (!arguments.ignoreResponse)
	{
		struct sockaddr_in sender;
		const auto msgSize(socket.Receive(&sender));

		if (msgSize == SOCKET_ERROR)
		{
			if (arguments.responseWait > 0)
			{
				// If we didn't get data, check to see if our timer has elapsed; if not, keep trying for data
				const auto now(std::chrono::steady_clock::now());
				const auto thresholdDuration = arguments.responseWait * std::chrono::milliseconds{ 1 };

				if ((now - timerStart) < thresholdDuration)
					continue;
			}

			return false;
		}
		else if (msgSize == 0)
		{
			std::cout << "Received empty response (connection closed)\n";
			return true;
		}
		else
		{
			// Got good data; reset the timer
			timerStart = std::chrono::steady_clock::now();

			if (!arguments.plainTextResponse)
			{
				std::cout << "Response(" << msgSize << " bytes)";
				if (arguments.protocol != Protocol::TCP)
					std::cout << "from " << inet_ntoa(sender.sin_addr) << ":" << ntohs(sender.sin_port) << " =";
				std::cout << '\n';
			}

			const auto response(std::string(socket.GetLastMessage(), msgSize));

			if (arguments.plainTextResponse)
				std::cout << response;
			else
			{
				for (const auto& c : response)
					std::cout << "0x" << std::hex << std::setfill('0') << std::setw(2) << c << " ";
				std::cout << std::endl;
				std::cout << std::dec;
			}
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
		if (ignoreFlag.compare(argv[firstPayloadArgument]) == 0)
		{
			arguments.ignoreResponse = true;
			++firstPayloadArgument;
		}
	}

	if (!arguments.plainTextResponse)
	{
		const std::string plainTextFlag("--plain-text-response");
		if (plainTextFlag.compare(argv[firstPayloadArgument]) == 0)
		{
			arguments.plainTextResponse = true;
			++firstPayloadArgument;
		}
	}

	const std::string responseWaitFlag("--response-wait");
	if (responseWaitFlag.compare(argv[firstPayloadArgument]) == 0)
	{
		++firstPayloadArgument;
		std::istringstream ss(argv[firstPayloadArgument]);
		if (!(ss >> arguments.responseWait))
		{
			std::cerr << "Failed to parse " << responseWaitFlag << " argument\n";
			return false;
		}
		++firstPayloadArgument;
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
		std::cout << "Usage:  " << argv[0] << " <ip address> <port> <tcp, udp, upd-broadcast> [--ignore-response] [--plain-text-response] [--response-wait <ms to wait>] <payload>\n";
		std::cout << "        or, for Wake-On-LAN:";
		std::cout << "        " << argv[0] << " <ip address> <port> wol <MAC address>\n";
		std::cout << "        Use \\x## to represent a hex byte in the payload\n";
		return 1;
	}

	Arguments arguments;
	if (!ParseArguments(argc, argv, arguments))
		return 1;

	std::cout << "Message is '" << arguments.message << '\'' << std::endl;
	if (!SendEthernetMessage(arguments))
		return 1;
	return 0;
}
