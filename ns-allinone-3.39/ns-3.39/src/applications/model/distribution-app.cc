//
// Copyright (c) 2006 Georgia Tech Research Corporation
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: George F. Riley<riley@ece.gatech.edu>
//

// ns3 - On/Off Data Source Application class
// George F. Riley, Georgia Tech, Spring 2007
// Adapted from ApplicationOnOff in GTNetS.

#include "distribution-app.h"

#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/packet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include <cmath>

#include <iostream>
namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Distributionapp");

NS_OBJECT_ENSURE_REGISTERED(Distributionapp);

TypeId
Distributionapp::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::Distributionapp")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<Distributionapp>()
            .AddAttribute("DataRate",
                          "The data rate in on state.",
                          DataRateValue(DataRate("500kb/s")),
                          MakeDataRateAccessor(&Distributionapp::m_cbrRate),
                          MakeDataRateChecker())
            .AddAttribute("PacketSize",
                          "The size of packets sent in on state",
                          UintegerValue(512),
                          MakeUintegerAccessor(&Distributionapp::m_pktSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("Remote",
                          "The address of the destination",
                          AddressValue(),
                          MakeAddressAccessor(&Distributionapp::m_peer),
                          MakeAddressChecker())
            .AddAttribute("Local",
                          "The Address on which to bind the socket. If not set, it is generated "
                          "automatically.",
                          AddressValue(),
                          MakeAddressAccessor(&Distributionapp::m_local),
                          MakeAddressChecker())
            .AddAttribute("MaxBytes",
                          "The total number of bytes to send. Once these bytes are sent, "
                          "no packet is sent again, even in on state. The value zero means "
                          "that there is no limit.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&Distributionapp::m_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("Protocol",
                          "The type of protocol to use. This should be "
                          "a subclass of ns3::SocketFactory",
                          TypeIdValue(UdpSocketFactory::GetTypeId()),
                          MakeTypeIdAccessor(&Distributionapp::m_tid),
                          MakeTypeIdChecker())
            .AddAttribute("EnableSeqTsSizeHeader",
                          "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Distributionapp::m_enableSeqTsSizeHeader),
                          MakeBooleanChecker())
            .AddAttribute("Interval",
                        "The time to wait between packets",
                        DoubleValue(0),
                        MakeDoubleAccessor(&Distributionapp::m_interval),
                        MakeDoubleChecker<double>())
            .AddAttribute("PacketGen",
                        "Specifies the packet arrival distribution type. Available options are: CBR, Exp, Uni",
                        StringValue("Constant"),
                        MakeStringAccessor(&Distributionapp::m_packetgentype),
                        MakeStringChecker())
            .AddAttribute("SCV_tia",
                        "SCV_tia is the parameter of the SCV distribution",
                        DoubleValue(1),
                        MakeDoubleAccessor(&Distributionapp::m_scv_tia),
                        MakeDoubleChecker<double>())
            .AddAttribute("SCV_pkt",
                        "SCV_param is the parameter of the SCV distribution",
                        DoubleValue(1),
                        MakeDoubleAccessor(&Distributionapp::m_scv_pkt),
                        MakeDoubleChecker<double>())
            .AddAttribute("ArrivalGen",
                        "Distribution of the packet size. Available options are: CBR, Exp, Uni",
                        StringValue("Constant"),
                        MakeStringAccessor(&Distributionapp::m_arrivalgentype),
                        MakeStringChecker())
            .AddAttribute("PacketSizeMax",
                        "Bound for the packet size",
                        DoubleValue(59000),
                        MakeDoubleAccessor(&Distributionapp::m_pktSizeMax),
                        MakeDoubleChecker<double>())
            .AddAttribute("DSCP",
                        "DSCP value to set in the IP header of generated packets (0-63)",
                        UintegerValue(0),
                        MakeUintegerAccessor(&Distributionapp::m_dscp),
                        MakeUintegerChecker<uint8_t>(0))
            .AddAttribute("PacketSizeMin",
                        "Bound for the packet size",
                        DoubleValue(0),
                        MakeDoubleAccessor(&Distributionapp::m_pktSizeMin),
                        MakeDoubleChecker<double>())

            .AddTraceSource("Tx",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&Distributionapp::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&Distributionapp::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback")
            .AddTraceSource("TxWithSeqTsSize",
                            "A new packet is created with SeqTsSizeHeader",
                            MakeTraceSourceAccessor(&Distributionapp::m_txTraceWithSeqTsSize),
                            "ns3::PacketSink::SeqTsSizeCallback");
    return tid;
}

Distributionapp::Distributionapp()
    : m_socket(nullptr),
      m_connected(false),
      m_residualBits(0),
      m_totBytes(0)
{
    NS_LOG_FUNCTION(this);

    
}

Distributionapp::~Distributionapp()
{
    NS_LOG_FUNCTION(this);
}

void
Distributionapp::SetMaxBytes(uint64_t maxBytes)
{
    NS_LOG_FUNCTION(this << maxBytes);
    m_maxBytes = maxBytes;
}

Ptr<Socket>
Distributionapp::GetSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}



void
Distributionapp::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_socket = nullptr;
    m_unsentPacket = nullptr;



    // chain up
    Application::DoDispose();
}




void
Distributionapp::InitializeParams()
{
    

    if (m_packetgentype == "Exp")
    {
        m_packet_gen = CreateObject<ExponentialRandomVariable>();
        m_packet_gen->SetAttribute("Mean", DoubleValue(m_pktSize));
        m_packet_gen->SetAttribute("Bound", DoubleValue(m_pktSizeMax));
    }
    else if (m_packetgentype == "Uni")
    {
        m_packet_gen = CreateObject<UniformRandomVariable>();
        std::cout << m_pktSizeMin << " " << m_pktSizeMax << std::endl;
        m_packet_gen->SetAttribute("Min", DoubleValue(m_pktSizeMin));
        m_packet_gen->SetAttribute("Max", DoubleValue(m_pktSizeMax));

    }else if(m_packetgentype == "Gamma")
    {
        double alfa =  1/m_scv_pkt;
        double beta = m_pktSize/alfa;    
        m_packet_gen = CreateObject<GammaRandomVariable>();
        m_packet_gen->SetAttribute("Alpha", DoubleValue(alfa));
        m_packet_gen->SetAttribute("Beta", DoubleValue(beta));
    }
    else
    {
        m_packet_gen = CreateObject<ConstantRandomVariable>();
        m_packet_gen->SetAttribute("Constant", DoubleValue(m_pktSize));
    }

    if (m_arrivalgentype == "Exp")
    {
        m_time_id = CreateObject<ExponentialRandomVariable>();
        m_time_id->SetAttribute("Mean", DoubleValue(m_interval));
    }
    else if(m_arrivalgentype == "Gamma")
    {
        double alfa =  1/m_scv_tia;
        double beta = m_interval/alfa;    
        m_time_id = CreateObject<GammaRandomVariable>();
        m_time_id->SetAttribute("Alpha", DoubleValue(alfa));
        m_time_id->SetAttribute("Beta", DoubleValue(beta));
    }
    else if (m_arrivalgentype == "Uni")
    {
        m_time_id = CreateObject<UniformRandomVariable>();
        m_time_id->SetAttribute("Min", DoubleValue(0));
        m_time_id->SetAttribute("Max", DoubleValue(m_interval));
    }
    else
    {
        m_time_id = CreateObject<ConstantRandomVariable>();
        m_time_id->SetAttribute("Constant", DoubleValue(m_interval));
        std::cout << "Interval: " << m_interval << std::endl;
    }


    // std::cout << "Application configured with packet generation " << m_packetgentype << " and arrival generation " << m_arrivalgentype << std::endl;
}


// Application Methods
void
Distributionapp::StartApplication() // Called at time specified by Start
{
    NS_LOG_FUNCTION(this);


    // Create the socket if not already
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);
        m_socket->SetAttribute("RcvBufSize", UintegerValue(0));
        int ret = -1;

        if (!m_local.IsInvalid())
        {
            NS_ABORT_MSG_IF((Inet6SocketAddress::IsMatchingType(m_peer) &&
                             InetSocketAddress::IsMatchingType(m_local)) ||
                                (InetSocketAddress::IsMatchingType(m_peer) &&
                                 Inet6SocketAddress::IsMatchingType(m_local)),
                            "Incompatible peer and local address IP version");
            ret = m_socket->Bind(m_local);
        }
        else
        {
            if (Inet6SocketAddress::IsMatchingType(m_peer))
            {
                ret = m_socket->Bind6();
            }
            else if (InetSocketAddress::IsMatchingType(m_peer) ||
                     PacketSocketAddress::IsMatchingType(m_peer))
            {
                ret = m_socket->Bind();
            }
        }

        if (ret == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }

        m_socket->SetConnectCallback(MakeCallback(&Distributionapp::ConnectionSucceeded, this),
                                     MakeCallback(&Distributionapp::ConnectionFailed, this));

        m_socket->Connect(m_peer);
        m_socket->SetAllowBroadcast(true);
        m_socket->ShutdownRecv();
    }
    m_cbrRateFailSafe = m_cbrRate;
    

    if (m_connected)
    {   
        InitializeParams();
        m_sendEvent = Simulator::Schedule(Seconds(m_time_id->GetValue()), &Distributionapp::SendPacket, this);
    }
}

void
Distributionapp::StopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->Close();
    }
    else
    {
        NS_LOG_WARN("Distributionapp found null socket to close in StopApplication");
    }
}



void
Distributionapp::SendPacket()
{
    NS_LOG_FUNCTION(this);

    unsigned size  = 0;
 
    NS_ASSERT(m_sendEvent.IsExpired());

    Ptr<Packet> packet;
    if (m_enableSeqTsSizeHeader)
    {
        Address from;
        Address to;
        m_socket->GetSockName(from);
        m_socket->GetPeerName(to);
        SeqTsSizeHeader header;
        header.SetSeq(m_seq++);
        header.SetSize(m_pktSize);
        NS_ABORT_IF(m_pktSize < header.GetSerializedSize());
        packet = Create<Packet>(m_pktSize - header.GetSerializedSize());
        // Trace before adding header, for consistency with PacketSink
        m_txTraceWithSeqTsSize(packet, from, to, header);
        packet->AddHeader(header);
    }
    else
    {
      
        size  = m_packet_gen->GetValue();
        if (size > m_pktSizeMax){ // To bound the packet size 
            size = 0;
        }
        if (size == 0){ // If the packet size is 0, then we dont send anything
            m_sendEvent = Simulator::Schedule(Seconds(0), &Distributionapp::SendPacket, this);
            return;
        }
      
        // std::cout << m_packet_gen->GetValue() << std::endl;
        packet = Create<Packet>(size);
    }

    // std::cout << "Distributionapp::SendPacket() " << Simulator::Now().GetFemtoSeconds() << std::endl;
    m_socket->SetIpTos(m_dscp << 2); // Set DSCP to 0x04 (AF11) for testing purposes
    int actual = m_socket->Send(packet);
    if ((unsigned)actual == size)
    {  
        m_txTrace(packet);
        m_totBytes += m_pktSize;
        m_unsentPacket = nullptr;
        Address localAddress;
        m_socket->GetSockName(localAddress);
        if (InetSocketAddress::IsMatchingType(m_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " poisson application sent "
                                   << packet->GetSize() << " bytes to "
                                   << InetSocketAddress::ConvertFrom(m_peer).GetIpv4() << " port "
                                   << InetSocketAddress::ConvertFrom(m_peer).GetPort()
                                   << " total Tx " << m_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, InetSocketAddress::ConvertFrom(m_peer));
        }
        else if (Inet6SocketAddress::IsMatchingType(m_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6() << " port "
                                   << Inet6SocketAddress::ConvertFrom(m_peer).GetPort()
                                   << " total Tx " << m_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, Inet6SocketAddress::ConvertFrom(m_peer));
        }
    }
    else
    {
        NS_LOG_DEBUG("Unable to send packet; actual " << actual << " size " << m_pktSize
                                                      << "; caching for later attempt");
        m_unsentPacket = packet;
    }
 
       NS_LOG_FUNCTION(this);

    if (m_maxBytes == 0 || m_totBytes < m_maxBytes)
    {
        m_sendEvent = Simulator::Schedule(Seconds((m_time_id->GetValue())), &Distributionapp::SendPacket, this);      
    }
    else
    { 
        StopApplication();
    }
}

void
Distributionapp::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    m_connected = true;
}

void
Distributionapp::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Can't connect");
}

} // Namespace ns3
