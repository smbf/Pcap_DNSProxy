// This code is part of Pcap_DNSProxy
// Copyright (C) 2012-2014 Chengr28
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "Pcap_DNSProxy.h"

extern std::wstring Path;
extern Configuration Parameter;
extern PortTable PortList;

pcap_if *pThedevs = nullptr;

//Capture initialization
SSIZE_T __stdcall CaptureInitialization()
{
//Open all devices
	if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &pThedevs, NULL) == RETURN_ERROR)
	{
		PrintError(5, _T("Cannot open any NICs/Network Interface Cards"), NULL, NULL);
		return RETURN_ERROR;
	}

//Start capturing
	std::thread Capture(Capture, pThedevs);
	Capture.detach();
	return 0;
}

//Capture
SSIZE_T __stdcall Capture(const pcap_if *pDrive)
{
//Initialization
	pcap_t *pAdHandle = nullptr;
	pcap_pkthdr *pHeader = nullptr;
	const UCHAR *PacketData = nullptr;
	PWSTR ErrBuffer = nullptr, DrviceName = nullptr;
	PSTR ErrBuf = nullptr, Addr = nullptr, FilterRules = nullptr, Buffer = nullptr;
	try {
		ErrBuffer = new wchar_t[PCAP_ERRBUF_SIZE]();
		ErrBuf = new char[PCAP_ERRBUF_SIZE]();
		Addr = new char[PACKET_MAXSIZE/32]();
		FilterRules = new char[PACKET_MAXSIZE/16]();
		Buffer = new char[PACKET_MAXSIZE*THREAD_MAXNUM*THREAD_PARTNUM]();
		DrviceName = new wchar_t[PACKET_MAXSIZE/8]();
	}
	catch (std::bad_alloc)
	{
		PrintError(1, _T("Memory allocation failed"), NULL, NULL);

		delete[] ErrBuffer;
		delete[] ErrBuf;
		delete[] Addr;
		delete[] FilterRules;
		delete[] Buffer;
		delete[] DrviceName;
		TerminateService();
		return RETURN_ERROR;
	}
	memset(ErrBuffer, 0, sizeof(wchar_t) * PCAP_ERRBUF_SIZE);
	memset(ErrBuf, 0, PCAP_ERRBUF_SIZE);
	memset(Addr, 0, PACKET_MAXSIZE/32);
	memset(FilterRules, 0, PACKET_MAXSIZE/16);
	memset(Buffer, 0, PACKET_MAXSIZE*THREAD_MAXNUM*THREAD_PARTNUM);
	memset(DrviceName, 0, sizeof(wchar_t) * PACKET_MAXSIZE/8);

//Open device
	if ((pAdHandle = pcap_open(pDrive->name, 65536, PCAP_OPENFLAG_NOCAPTURE_LOCAL, TIME_OUT/4, NULL, ErrBuf)) == nullptr)
	{
		MultiByteToWideChar(CP_ACP, NULL, ErrBuf, -1, ErrBuffer, PCAP_ERRBUF_SIZE);
		PrintError(5, ErrBuffer, NULL, NULL);

		delete[] ErrBuffer;
		delete[] ErrBuf;
		delete[] Addr;
		delete[] FilterRules;
		delete[] Buffer;
		delete[] DrviceName;
		return RETURN_ERROR;
	}
	delete[] ErrBuf;

//Check device type
	MultiByteToWideChar(CP_ACP, NULL, pDrive->name, -1, DrviceName, PCAP_ERRBUF_SIZE);
	if (pcap_datalink(pAdHandle) != DLT_EN10MB) //Ethernet
	{
		static const PWSTR PcapDatalinkError = _T(" is not a Ethernet device");
		wcsncpy_s(ErrBuffer, PCAP_ERRBUF_SIZE, DrviceName, lstrlen(DrviceName));
		wcsncpy_s(ErrBuffer + lstrlen(DrviceName), PCAP_ERRBUF_SIZE - lstrlen(DrviceName), PcapDatalinkError, lstrlen(PcapDatalinkError));
		PrintError(5, ErrBuffer, NULL, NULL);

		delete[] ErrBuffer;
		delete[] Addr;
		delete[] FilterRules;
		delete[] Buffer;
		delete[] DrviceName;
		return RETURN_ERROR;
	}

//Set capture filter
	bpf_program FCode = {0};
	std::string sFilterRules = ("(src host ");

	if (Parameter.DNSTarget.IPv6 && Parameter.DNSTarget.IPv4) //Both of IPv4 and IPv6
	{
		std::string BothAddr = ("(");
		inet_ntop(AF_INET6, (PSTR)&Parameter.DNSTarget.IPv6Target, Addr, PACKET_MAXSIZE/32);
		BothAddr.append(Addr);
		memset(Addr, 0, PACKET_MAXSIZE/32);
		BothAddr.append(" or ");
		inet_ntop(AF_INET, (PSTR)&Parameter.DNSTarget.IPv4Target, Addr, PACKET_MAXSIZE/32);
		BothAddr.append(Addr);
		BothAddr.append(")");
		sFilterRules.append(BothAddr);
		sFilterRules.append(") or (pppoes and src host ");
		sFilterRules.append(BothAddr);
	}
	else if (Parameter.DNSTarget.IPv6) //IPv6 only
	{
		inet_ntop(AF_INET6, (PSTR)&Parameter.DNSTarget.IPv6Target, Addr, PACKET_MAXSIZE/32);
		sFilterRules.append(Addr);
		sFilterRules.append(") or (pppoes and src host ");
		sFilterRules.append(Addr);
	}
	else { //IPv4 only
		inet_ntop(AF_INET, (PSTR)&Parameter.DNSTarget.IPv4Target, Addr, PACKET_MAXSIZE/32);
		sFilterRules.append(Addr);
		sFilterRules.append(") or (pppoes and src host ");
		sFilterRules.append(Addr);
	}
	delete[] Addr;
	sFilterRules.append(")");
	memcpy(FilterRules, sFilterRules.c_str(), sFilterRules.length());

	if (pcap_compile(pAdHandle, &FCode, FilterRules, 1, (UINT)pDrive->addresses->netmask) == RETURN_ERROR)
    {
		PSTR PcapCompileError = pcap_geterr(pAdHandle);
		MultiByteToWideChar(CP_ACP, NULL, PcapCompileError, -1, ErrBuffer, PCAP_ERRBUF_SIZE);
		PrintError(5, ErrBuffer, NULL, NULL);

		delete[] ErrBuffer;
		delete[] FilterRules;
		delete[] Buffer;
		delete[] DrviceName;
        return RETURN_ERROR;
    }
	delete[] FilterRules;
	
    if (pcap_setfilter(pAdHandle, &FCode) == RETURN_ERROR)
    {
		PSTR PcapSetfilterError = pcap_geterr(pAdHandle);
		MultiByteToWideChar(CP_ACP, NULL, PcapSetfilterError, -1, ErrBuffer, PCAP_ERRBUF_SIZE);
		PrintError(5, ErrBuffer, NULL, NULL);

		delete[] ErrBuffer;
		delete[] Buffer;
		delete[] DrviceName;
        return RETURN_ERROR;
    }

//Copy device name from pThedevs
	if (pDrive->next != nullptr)
	{
		std::thread Capture(Capture, pDrive->next);
		Capture.detach();
	}
	else {
		pcap_freealldevs(pThedevs);
	}

//Start capture
	SSIZE_T Result = 0;
	size_t Index = 0, HeaderLength = sizeof(eth_hdr);
	while((Result = pcap_next_ex(pAdHandle, &pHeader, &PacketData)) >= 0)
	{
		switch (Result)
		{
			case RETURN_ERROR: //-1, An error occurred
			{
				static const PWSTR PcapNextExError = _T("An error occurred in ");
				wcsncpy_s(ErrBuffer, PCAP_ERRBUF_SIZE, PcapNextExError, lstrlen(PcapNextExError));
				wcsncpy_s(ErrBuffer + lstrlen(PcapNextExError), PCAP_ERRBUF_SIZE - lstrlen(PcapNextExError), DrviceName, lstrlen(DrviceName));
				PrintError(5, ErrBuffer, NULL, NULL);
				
				delete[] ErrBuffer;
				delete[] Buffer;
				delete[] DrviceName;
				return RETURN_ERROR;
			}break;
			case -2: //EOF was reached reading from an offline capture
			{
				static const PWSTR PcapNextExError = _T("EOF was reached reading from an offline capture in ");
				wcsncpy_s(ErrBuffer, PCAP_ERRBUF_SIZE, PcapNextExError, lstrlen(PcapNextExError));
				wcsncpy_s(ErrBuffer + lstrlen(PcapNextExError), PCAP_ERRBUF_SIZE - lstrlen(PcapNextExError), DrviceName, lstrlen(DrviceName));
				PrintError(5, ErrBuffer, NULL, NULL);
				
				delete[] ErrBuffer;
				delete[] Buffer;
				delete[] DrviceName;
				return RETURN_ERROR;
			}break;
			case FALSE: //0, The timeout set with pcap_open_live() has elapsed. In this case pkt_header and pkt_data don't point to a valid packet.
				continue;
			case TRUE: //1, The packet has been read without problems
			{
				memset(Buffer + PACKET_MAXSIZE*Index, 0, PACKET_MAXSIZE);

				eth_hdr *eth = (eth_hdr *)PacketData;
				HeaderLength = sizeof(eth_hdr);
				if (eth->Type == htons(ETHERTYPE_PPPOES)) //PPPoE(Such as ADSL, a part of school networks)
				{
					pppoe_hdr *pppoe = (pppoe_hdr *)(PacketData + HeaderLength);
					HeaderLength += sizeof(pppoe_hdr);
					if ((pppoe->Protocol == htons(PPPOETYPE_IPV4) || pppoe->Protocol == htons(PPPOETYPE_IPV6)) && //IPv4 or IPv6 over PPPoE
						pHeader->caplen - HeaderLength > 0)
					{
						memcpy(Buffer + PACKET_MAXSIZE*Index, PacketData + HeaderLength, pHeader->caplen - HeaderLength);
						std::thread IPMethod(IPLayer, Buffer + PACKET_MAXSIZE*Index, pHeader->caplen - HeaderLength, ntohs(eth->Type));
						IPMethod.detach();

						Index = (Index + 1)%(THREAD_MAXNUM*THREAD_PARTNUM);
					}
				}
				else if ((eth->Type == htons(ETHERTYPE_IP) || eth->Type == htons(ETHERTYPE_IPV6)) && //IPv4 or IPv6 (Such as LAN/WLAN/IEEE 802.1X, some Mobile Communications Standard drives which disguise as a LAN)
						pHeader->caplen - HeaderLength > 0)
				{
					memcpy(Buffer + PACKET_MAXSIZE*Index, PacketData + HeaderLength, pHeader->caplen - HeaderLength);
					std::thread IPMethod(IPLayer, Buffer + PACKET_MAXSIZE*Index, pHeader->caplen - HeaderLength, ntohs(eth->Type));
					IPMethod.detach();

					Index = (Index + 1)%(THREAD_MAXNUM*THREAD_PARTNUM);
				}
				else {
					continue;
				}
			}break;
			default:
				continue;
		}
	}

	delete[] ErrBuffer;
	delete[] Buffer;
	delete[] DrviceName;
	return 0;
}

//Network Layer(Internet Protocol/IP) process
SSIZE_T __stdcall IPLayer(const PSTR Recv, const size_t Length, const USHORT Protocol)
{
//Initialization
	PSTR Buffer = nullptr;
	try {
		Buffer = new char[PACKET_MAXSIZE]();
	}
	catch (std::bad_alloc) 
	{
		PrintError(1, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return RETURN_ERROR;
	}
	memset(Buffer, 0, PACKET_MAXSIZE);
	memcpy(Buffer, Recv, Length);

	if (Parameter.DNSTarget.IPv6 && (Protocol == PPPOETYPE_IPV6 || Protocol == ETHERTYPE_IPV6)) //IPv6
	{
		ipv6_hdr *ipv6 = (ipv6_hdr *)Buffer;

//Get Hop Limits from IPv6 DNS server
/*	//Length of packets
		if (Length > sizeof(ipv6_hdr) + sizeof(udp_hdr) + sizeof(dns_hdr) + sizeof(dns_qry) + 1)
			Parameter.HopLimitOptions.IPv6HopLimit = ipv6->HopLimit;
*/
	//ICMPv6 Protocol
		if (Parameter.ICMPOptions.ICMPSpeed > 0 && ipv6->NextHeader == IPPROTO_ICMPV6)
		{
			if (ICMPCheck(Buffer, Length, AF_INET6))
				Parameter.HopLimitOptions.IPv6HopLimit = ipv6->HopLimit;

			delete[] Buffer;
			return 0;
		}
	//TCP Protocol
		else if (ipv6->NextHeader == IPPROTO_TCP && Parameter.TCPOptions)
		{
			if (TCPCheck((Buffer + sizeof(ipv6_hdr))))
				Parameter.HopLimitOptions.IPv6HopLimit = ipv6->HopLimit;

			delete[] Buffer;
			return 0;
		}
//End
		else if (ipv6->NextHeader == IPPROTO_UDP)
		{
			udp_hdr *udp = (udp_hdr *)(Buffer + sizeof(ipv6_hdr));
		//Validate UDP checksum
			if (UDPChecksum(Buffer, Length, AF_INET6) != 0)
			{
				delete[] Buffer;
				return 0;
			}

			if (udp->Src_Port == htons(DNS_Port))
			{
			//Domain Test and DNS Options check and get Hop Limit form Domain Test
				bool SignHopLimit = false;
				if (DTDNSOCheck(Buffer + sizeof(ipv6_hdr) + sizeof(udp_hdr), SignHopLimit))
				{
					if (SignHopLimit)
						Parameter.HopLimitOptions.IPv6HopLimit = ipv6->HopLimit;
				}
				else {
					delete[] Buffer;
					return 0;
				}

			//Process
				if (ipv6->HopLimit > Parameter.HopLimitOptions.IPv6HopLimit - Parameter.HopLimitOptions.HopLimitFluctuation && ipv6->HopLimit < Parameter.HopLimitOptions.IPv6HopLimit + Parameter.HopLimitOptions.HopLimitFluctuation) //Hop Limit must not a ramdom number
				{
					DNSMethod(Buffer + sizeof(ipv6_hdr), Length - sizeof(ipv6_hdr), AF_INET6);

					delete[] Buffer;
					return 0;
				}
			}
		}
	}
	else if (Parameter.DNSTarget.IPv4 && (Protocol == PPPOETYPE_IPV4 || Protocol == ETHERTYPE_IP)) //IPv4
	{
		ipv4_hdr *ipv4 = (ipv4_hdr *)Buffer;
	//Validate IPv4 pcaket
		if (ipv4->IHL != 5 || //Standard IPv4 header
			GetChecksum((PUSHORT)Buffer, sizeof(ipv4_hdr)) != 0 || //Validate IPv4 header checksum
			Parameter.IPv4Options && (ipv4->TOS != 0 || ipv4->Flags != 0)) //TOS and Flags should not be set.
		{
			delete[] Buffer;
			return 0;
		}
	//End

//Get Hop Limits from IPv6 DNS server
/*	//Length of packets
		if (Length > sizeof(ipv4_hdr) + sizeof(udp_hdr) + sizeof(dns_hdr) + sizeof(dns_qry) + 1)
			Parameter.HopLimitOptions.IPv6HopLimit = ipv4->TTL;
*/
	//ICMP Protocol
		if (Parameter.ICMPOptions.ICMPSpeed > 0 && ipv4->Protocol == IPPROTO_ICMP)
		{
			if (ICMPCheck(Buffer, Length, AF_INET))
				Parameter.HopLimitOptions.IPv4TTL = ipv4->TTL;

			delete[] Buffer;
			return 0;
		}
	//TCP Protocol
		else if (ipv4->Protocol == IPPROTO_TCP && Parameter.TCPOptions)
		{
			if (TCPCheck((Buffer + sizeof(ipv4_hdr))))
				Parameter.HopLimitOptions.IPv4TTL = ipv4->TTL;

			delete[] Buffer;
			return 0;
		}
//End
		else if (ipv4->Protocol == IPPROTO_UDP)
		{
			udp_hdr *udp = (udp_hdr *)(Buffer + sizeof(ipv4_hdr));
		//Validate UDP checksum
			if (UDPChecksum(Buffer, Length, AF_INET) != 0)
			{
				delete[] Buffer;
				return 0;
			}
		//End

			if (udp->Src_Port == htons(DNS_Port))
			{
			//Domain Test and DNS Options check and get TTL form Domain Test
				bool SignHopLimit = false;
				if (DTDNSOCheck(Buffer + sizeof(ipv4_hdr) + sizeof(udp_hdr), SignHopLimit))
				{
					if (SignHopLimit)
						Parameter.HopLimitOptions.IPv4TTL = ipv4->TTL;
				}
				else {
					delete[] Buffer;
					return 0;
				}

			//Process
				if (ipv4->TTL > Parameter.HopLimitOptions.IPv4TTL - Parameter.HopLimitOptions.HopLimitFluctuation && ipv4->TTL < Parameter.HopLimitOptions.IPv4TTL + Parameter.HopLimitOptions.HopLimitFluctuation) //TTL must not a ramdom number
				{
					DNSMethod(Buffer + sizeof(ipv4_hdr), Length - sizeof(ipv4_hdr), AF_INET);

					delete[] Buffer;
					return 0;
				}
			}
		}
	}

	delete[] Buffer;
	return 0;
}

//TCP header options check
inline bool __stdcall ICMPCheck(const PSTR Buffer, const size_t Length, const size_t Protocol)
{
	if (Protocol == AF_INET6) //ICMPv6
	{
		icmpv6_hdr *icmp = (icmpv6_hdr *)(Buffer + sizeof(ipv6_hdr));
	//Validate ICMPv6 checksum
		if (ICMPv6Checksum(Buffer, Length) != 0)
			return false;
	//End
		
		if (icmp->Type == ICMPV6_REPLY && icmp->Code == 0 && //ICMPv6 reply
			icmp->ID == Parameter.ICMPOptions.ICMPID && icmp->Sequence == Parameter.ICMPOptions.ICMPSequence) //Validate ICMP packet
				return true;
	}
	else { //ICMP
		icmp_hdr *icmp = (icmp_hdr *)(Buffer + sizeof(ipv4_hdr));
	//Validate ICMP checksum
		if (GetChecksum((PUSHORT)(Buffer + sizeof(ipv4_hdr)), Length - sizeof(ipv4_hdr)) != 0)
			return false;
	//End

		if (icmp->Type == 0 && icmp->Code == 0 && //ICMP reply
			htons(icmp->ID) == Parameter.ICMPOptions.ICMPID && icmp->Sequence == Parameter.ICMPOptions.ICMPSequence && //Validate ICMP packet
			Length - sizeof(ipv4_hdr) - sizeof(icmp_hdr) == Parameter.PaddingDataOptions.PaddingDataLength - 1)
		{
	//Validate ICMP additional data
			PSTR icmp_data = (PSTR)icmp + sizeof(icmp_hdr);
			PSTR icmp_data_test = nullptr;
			try {
				icmp_data_test = new char[Parameter.PaddingDataOptions.PaddingDataLength + 3]();
			}
			catch (std::bad_alloc)
			{
				PrintError(1, _T("Memory allocation failed"), NULL, NULL);

				TerminateService();
				return false;
			}
			memset(icmp_data_test, 0, Parameter.PaddingDataOptions.PaddingDataLength + 3);

			memcpy(icmp_data_test, icmp_data, Parameter.PaddingDataOptions.PaddingDataLength - 1);
			if (memcmp(Parameter.PaddingDataOptions.PaddingData, icmp_data_test, Parameter.PaddingDataOptions.PaddingDataLength - 1) == 0)
			{
				delete[] icmp_data_test;
				return true;
			}
			
			delete[] icmp_data_test;
		}
	//End
	}

	return false;
}

//TCP header options check
inline bool __stdcall TCPCheck(const PSTR Buffer)
{
	tcp_hdr *tcp = (tcp_hdr *)Buffer;
	if ((tcp->Acknowledge == 0 && tcp->FlagsAll.Flags == 0x004 && tcp->Windows == 0 || //TCP Flags are 0x004(RST) which ACK shoule be 0 and Window size should be 0
		tcp->HeaderLength > 5 && tcp->FlagsAll.Flags == 0x012)) //TCP option usually should not empty(MSS, SACK_PERM and WS) whose Flags are 0x012(SYN/ACK).
		return true;

	return false;
}

//Domain Test and DNS Options check/DomainTestAndDNSOptionsCheck
inline bool __stdcall DTDNSOCheck(const PSTR Buffer, bool &SignHopLimit)
{
	dns_hdr *pdns_hdr = (dns_hdr *)Buffer;

//DNS Options part
	if (pdns_hdr->Questions == 0 || //Not any Answer Record
		pdns_hdr->Authority == 0 && pdns_hdr->Additional == 0 && pdns_hdr->Answer == 0 && //There are not any Records.
		Parameter.DNSOptions && (ntohs(pdns_hdr->Flags) & 0x0400) >> 10 == 1) //Responses are not authoritative when there not any Authoritative Nameservers Record(s)/Additional Record(s).
			return false;

	if (Parameter.DNSOptions && 
		(ntohs(pdns_hdr->Answer) > 0x0001 || //More than 1 Answer Record(s)
		pdns_hdr->Answer == 0 && (pdns_hdr->Authority != 0 || pdns_hdr->Additional != 0))) //Authority Record(s) and/or Additional Record(s)
			SignHopLimit = true;

	if (pdns_hdr->FlagsBits.RCode == 0x0003) //No Such Name
	{
		SignHopLimit = true;
		return true;
	}

//Initialization
	PSTR Result = nullptr, Compression = nullptr;
	try {
		Result = new char[PACKET_MAXSIZE/8]();
		Compression = new char[PACKET_MAXSIZE/8]();
	}
	catch (std::bad_alloc)
	{
		PrintError(1, _T("Memory allocation failed"), NULL, NULL);

		delete[] Result;
		delete[] Compression;
		TerminateService();
		return false;
	}
	memset(Result, 0, PACKET_MAXSIZE/8);
	memset(Compression, 0, PACKET_MAXSIZE/8);

//Domain Test part
	size_t Length = DNSQueryToChar(Buffer + sizeof(dns_hdr), Result);
	if (Parameter.DomainTestOptions.DomainTestCheck && 
		strcmp(Result, Parameter.DomainTestOptions.DomainTest) == 0 && pdns_hdr->ID == Parameter.DomainTestOptions.DomainTestID)
	{
		delete[] Result;
		delete[] Compression;
		SignHopLimit = true;
		return true;
	}

//Check DNS Compression
	DNSQueryToChar(Buffer + sizeof(dns_hdr) + Length + sizeof(USHORT)*2 + 1, Compression);
	if (Parameter.DNSOptions && strcmp(Result, Compression) == 0)
	{
		delete[] Result;
		delete[] Compression;
		return false;
	}

	delete[] Result;
	delete[] Compression;
	return true;
}

//Application Layer(Domain Name System/DNS) process
inline SSIZE_T __stdcall DNSMethod(const PSTR Recv, const size_t Length, const size_t Protocol)
{
//Initialization
	PSTR Buffer = nullptr;
	try {
		Buffer = new char[PACKET_MAXSIZE]();
	}
	catch (std::bad_alloc)
	{
		PrintError(1, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return RETURN_ERROR;
	}
	memset(Buffer, 0, PACKET_MAXSIZE);

	size_t DNSLen = Length - sizeof(udp_hdr);
	if (DNSLen > sizeof(dns_hdr) + sizeof(dns_qry) + sizeof(dns_a_record) - 1) //Responses must have more than one answer.
	{
		memcpy(Buffer, Recv + sizeof(udp_hdr), DNSLen);
	}
	else {
		delete[] Buffer;
		return 0;
	}

//DNS Responses which only have 1 Answer RR and no any Authority RRs or Additional RRs need to check.
	dns_hdr *pdns_hdr = (dns_hdr *)Buffer;
	dns_qry *pdns_qry = (dns_qry *)(Buffer + sizeof(dns_hdr) + (strlen(Buffer + sizeof(dns_hdr)) + 1));
	if (pdns_hdr->Answer == htons(0x0001) && pdns_hdr->Authority == 0 && pdns_hdr->Additional == 0)
	{
		if (pdns_qry->Classes == htons(Class_IN)) //Class IN
		{
		//Record(s) Type in responses check
			if (Parameter.DNSOptions)
			{
				PUSHORT AnswerName = (PUSHORT)(&pdns_qry->Classes + 1), AnswerType = AnswerName + 1;
				if (*AnswerName == htons(0xC00C) && *AnswerType != pdns_qry->Type) //Types in Queries and Answers are different.
				{
					delete[] Buffer;
					return 0;
				}
			}

		//Fake responses check
			if (Parameter.Blacklist)
			{
			//IPv6
				if (pdns_qry->Type == htons(AAAA_Records)) //AAAA Records
				{
					in6_addr *Addr = (in6_addr *)(Buffer + DNSLen - sizeof(in6_addr));
/*
				//Packets whose TTLs are 5 minutes are fake responses.
					PULONG TTL = (PULONG)(RequestBuffer + DNSLen - sizeof(in6_addr) - sizeof(USHORT) - sizeof(ULONG));
					if (ntohl(*TTL) == 300 || //Packets whose TTLs are 5 minutes are fake responses.
*/
				//About this list, see https://zh.wikipedia.org/wiki/IPv6#.E7.89.B9.E6.AE.8A.E4.BD.8D.E5.9D.80.
					if ((Addr->u.Word[0] == 0 && Addr->u.Word[1] == 0 && Addr->u.Word[2] == 0 && Addr->u.Word[3] == 0 && Addr->u.Word[4] == 0 && 
						((Addr->u.Word[5] == 0 && 
						((Addr->u.Word[6] == 0 && Addr->u.Word[7] == 0 || //Unspecified address(::, Section 2.5.2 in RFC 4291)
						Addr->u.Word[6] == 0 && Addr->u.Word[7] == htons(0x0001)) || //Loopback address(::1, Section 2.5.3 in RFC 4291)
						Addr->u.Word[5] == 0)) || //IPv4-Compatible Contrast address(::/96, Section 2.5.5.1 in RFC 4291)
						Addr->u.Word[5] == htons(0xFFFF))) || //IPv4-mapped address(::FFFF:0:0/96, Section 2.5.5 in RFC 4291)
						Addr->u.Word[0] == htons(0x2001) && 
						(Addr->u.Word[1] == 0 || //Teredo relay/tunnel address(2001::/32, RFC 4380)
						Addr->u.Byte[2] == 0 && Addr->u.Byte[3] <= htons(0x07) || //Sub-TLA IDs assigned to IANA(2001:0000::/29, Section 2 in RFC 4773)
						Addr->u.Byte[2] >= htons(0x01) && Addr->u.Byte[3] >= htons(0xF8) || //Sub-TLA IDs assigned to IANA(2001:01F8::/29, Section 2 in RFC 4773)
						Addr->u.Byte[3] >= htons(0x10) && Addr->u.Byte[3] <= htons(0x1F) || //Overlay Routable Cryptographic Hash IDentifiers/ORCHID address(2001:10::/28 in RFC 4843)
						Addr->u.Word[1] == htons(0x0DB8)) || //Contrast address prefix reserved for documentation(2001:DB8::/32, RFC 3849)
						Addr->u.Word[0] == htons(0x2002) || //6to4 relay/tunnel address(2002::/16, Section 2 in RFC 3056)
//						Addr->u.Word[0] == htons(0x0064) && Addr->u.Word[1] == htons(0xFF9B) && Addr->u.Word[2] == 0 && Addr->u.Word[3] == 0 && Addr->u.Word[4] == 0 && Addr->u.Word[5] == 0 || //Well Known Prefix(64:FF9B::/96, Section 2.1 in RFC 4773)
						Addr->u.Word[0] == htons(0x3FFE) || //6bone address(3FFE::/16, RFC 3701)
						Addr->u.Byte[0] == htons(0x5F) || //6bone address(5F00::/8, RFC 3701)
//						Addr->u.Byte[0] >= htons(0xFC) && Addr->u.Byte[0] <= htons(0xFD) || //Unique Local Unicast address/ULA(FC00::/7, Section 2.5.7 in RFC 4193)
						Addr->u.Byte[0] == htons(0xFE) && 
						(Addr->u.Byte[1] >= htons(0x80) && Addr->u.Byte[1] <= htons(0xBF) || //Link-Local Unicast Contrast address(FE80::/10, Section 2.5.6 in RFC 4291)
						Addr->u.Byte[1] >= htons(0xC0)) || //Site-Local scoped address(FEC0::/10, RFC 3879)
//						Addr->u.Byte[0] == htons(0xFF) || //Multicast address(FF00::/8, Section 2.7 in RFC 4291)
						Addr->u.Word[5] == htons(0x5EFE)) //ISATAP Interface Identifiers(Prefix::5EFE:0:0:0:0/64, Section 6.1 in RFC 5214)
					{
						delete[] Buffer;
						return 0;
					}
				}
			//IPv4
				else if (pdns_qry->Type == htons(A_Records)) //A Records
				{
					in_addr *Addr = (in_addr *)(Buffer + DNSLen - sizeof(in_addr));
/*
				//Packets whose TTLs are 5 minutes are fake responses.
					PULONG TTL = (PULONG)(RequestBuffer + DNSLen - sizeof(in_addr) - sizeof(USHORT) - sizeof(ULONG));
					if (ntohl(*TTL) == 300 || //Packets whose TTLs are 5 minutes are fake responses.
*/

				//About this list, see https://zh.wikipedia.org/wiki/%E5%9F%9F%E5%90%8D%E6%9C%8D%E5%8A%A1%E5%99%A8%E7%BC%93%E5%AD%98%E6%B1%A1%E6%9F%93#.E8.99.9A.E5.81.87IP.E5.9C.B0.E5.9D.80.
					if (Addr->S_un.S_addr == inet_addr("1.1.1.1") || Addr->S_un.S_addr == inet_addr("4.36.66.178") || Addr->S_un.S_addr == inet_addr("8.7.198.45") || Addr->S_un.S_addr == inet_addr("37.61.54.158") || 
						Addr->S_un.S_addr == inet_addr("46.82.174.68") || Addr->S_un.S_addr == inet_addr("59.24.3.173") || Addr->S_un.S_addr == inet_addr("64.33.88.161") || Addr->S_un.S_addr == inet_addr("64.33.99.47") || 
						Addr->S_un.S_addr == inet_addr("64.66.163.251") || Addr->S_un.S_addr == inet_addr("65.104.202.252") || Addr->S_un.S_addr == inet_addr("65.160.219.113") || Addr->S_un.S_addr == inet_addr("66.45.252.237") || 
						Addr->S_un.S_addr == inet_addr("72.14.205.99") || Addr->S_un.S_addr == inet_addr("72.14.205.104") || Addr->S_un.S_addr == inet_addr("78.16.49.15") || Addr->S_un.S_addr == inet_addr("93.46.8.89") || 
						Addr->S_un.S_addr == inet_addr("128.121.126.139") || Addr->S_un.S_addr == inet_addr("159.106.121.75") || Addr->S_un.S_addr == inet_addr("169.132.13.103") || Addr->S_un.S_addr == inet_addr("192.67.198.6") || 
						Addr->S_un.S_addr == inet_addr("202.106.1.2") || Addr->S_un.S_addr == inet_addr("202.181.7.85") || Addr->S_un.S_addr == inet_addr("203.98.7.65") || Addr->S_un.S_addr == inet_addr("203.161.230.171") || 
						Addr->S_un.S_addr == inet_addr("207.12.88.98") || Addr->S_un.S_addr == inet_addr("208.56.31.43") || Addr->S_un.S_addr == inet_addr("209.36.73.33") || Addr->S_un.S_addr == inet_addr("209.145.54.50") || 
						Addr->S_un.S_addr == inet_addr("209.220.30.174") || Addr->S_un.S_addr == inet_addr("211.94.66.147") || Addr->S_un.S_addr == inet_addr("213.169.251.35") || Addr->S_un.S_addr == inet_addr("216.221.188.182") || 
						Addr->S_un.S_addr == inet_addr("216.234.179.13") || Addr->S_un.S_addr == inet_addr("243.185.187.39") || 
					//About this list, see https://zh.wikipedia.org/wiki/IPv4#.E7.89.B9.E6.AE.8A.E7.94.A8.E9.80.94.E7.9A.84.E5.9C.B0.E5.9D.80.
						Addr->S_un.S_un_b.s_b1 == 0 || //Current network whick only valid as source address(0.0.0.0/8, Section 3.2.1.3 in RFC 1122)
//						Addr->S_un.S_un_b.s_b1 == 0x0A || //Private class A address(10.0.0.0/8, Section 3 in RFC 1918)
						Addr->S_un.S_un_b.s_b1 == 0x7F || //Loopback address(127.0.0.0/8, Section 3.2.1.3 in RFC 1122)
						Addr->S_un.S_un_b.s_b1 == 0xA9 && Addr->S_un.S_un_b.s_b2 >= 0xFE || //Link-local address(169.254.0.0/16, RFC 3927)
//						Addr->S_un.S_un_b.s_b1 == 0xAC && Addr->S_un.S_un_b.s_b2 >= 0x10 && Addr->S_un.S_un_b.s_b2 <= 0x1F || //Private class B address(172.16.0.0/16, Section 3 in RFC 1918)
						Addr->S_un.S_un_b.s_b1 == 0xC0 && (Addr->S_un.S_un_b.s_b2 == 0 && (Addr->S_un.S_un_b.s_b3 == 0 || //Reserved for IETF protocol assignments address(192.0.0.0/24, Section 3 in RFC 5735)
						Addr->S_un.S_un_b.s_b3 == 0x02) || //TEST-NET-1 address(192.0.2.0/24, Section 3 in RFC 5735)
						Addr->S_un.S_un_b.s_b2 == 0x58 && Addr->S_un.S_un_b.s_b3 == 0x63) || //6to4 relay/tunnel address(192.88.99.0/24, Section 2.3 in RFC 3068)
//						Addr->S_un.S_un_b.s_b1 == 0xC0 && Addr->S_un.S_un_b.s_b2 == 0xA8 || //Private class C address(192.168.0.0/24, Section 3 in RFC 1918)
						Addr->S_un.S_un_b.s_b1 == 0xC6 && 
						(Addr->S_un.S_un_b.s_b2 == 0x12 || //Benchmarking Methodology for Network Interconnect Devices address(198.18.0.0/15, Section 11.4.1 in RFC 2544)
						Addr->S_un.S_un_b.s_b2 == 0x33 && Addr->S_un.S_un_b.s_b3 == 0x64) || //TEST-NET-2(198.51.100.0/24, Section 3 in RFC 5737)
						Addr->S_un.S_un_b.s_b1 == 0xCB && Addr->S_un.S_un_b.s_b2 == 0 && Addr->S_un.S_un_b.s_b3 == 0x71 || //TEST-NET-3(203.0.113.0/24, Section 3 in RFC 5737)
//						Addr->S_un.S_un_b.s_b1 == 0xE0 || //Multicast address(224.0.0.0/4, Section 2 in RFC 3171)
						Addr->S_un.S_un_b.s_b1 >= 0xF0) //Reserved for future use address(240.0.0.0/4, Section 4 in RFC 1112) and Broadcast address(255.255.255.255/32, Section 7 in RFC 919/RFC 922)
					{
						delete[] Buffer;
						return 0;
					}
				}
			}
		}
	}

//Send
	udp_hdr *udp = (udp_hdr *)Recv;
	PortList.MatchToSend(Buffer, udp->Dst_Port, DNSLen);

	delete[] Buffer;
	return 0;
}

//Match port(s) process
SSIZE_T PortTable::MatchToSend(const PSTR Buffer, const USHORT RequestPort, const size_t Length)
{
//Match port
	SOCKET_DATA *SystemPort = nullptr;
	try {
		SystemPort = new SOCKET_DATA();
	}
	catch (std::bad_alloc)
	{
		PrintError(1, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return RETURN_ERROR;
	}
	memset(SystemPort, 0, sizeof(SOCKET_DATA));

	size_t Index = 0;
	for (Index = 0;Index < THREAD_MAXNUM*THREAD_PARTNUM;Index++)
	{
		if (RequestPort == SendPort[Index])
		{
			*SystemPort = RecvData[Index];

			memset(&RecvData[Index], 0, sizeof(SOCKET_DATA));
			SendPort[Index] = 0;
			break;
		}
	}

//Send to localhost
	if (Index >= THREAD_MAXNUM*THREAD_PARTNUM/2) //TCP area
	{
		PSTR TCPBuffer = nullptr;
		try {
			TCPBuffer = new char[PACKET_MAXSIZE]();
		}
		catch (std::bad_alloc)
		{
			PrintError(1, _T("Memory allocation failed"), NULL, NULL);

			delete SystemPort;
			TerminateService();
			return RETURN_ERROR;
		}
		memset(TCPBuffer, 0, PACKET_MAXSIZE);
		USHORT DataLength = htons((USHORT)Length);

		memcpy(TCPBuffer, &DataLength, sizeof(USHORT));
		memcpy(TCPBuffer + sizeof(USHORT), Buffer, Length);
		send(SystemPort->Socket, TCPBuffer, (int)(Length + sizeof(USHORT)), NULL);
		delete[] TCPBuffer;
	}
	else { //UDP
		sendto(SystemPort->Socket, Buffer, (int)Length, NULL, (PSOCKADDR)&(SystemPort->Sockaddr), SystemPort->AddrLen);
	}

//Cleanup socket
	for (Index = 0;Index < THREAD_PARTNUM;Index++)
	{
		if (SystemPort->Socket == Parameter.LocalSocket[Index])
		{
			delete SystemPort;
			return 0;
		}
	}

	closesocket(SystemPort->Socket);
	delete SystemPort;
	return 0;
}