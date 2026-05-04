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

#include "poisson-app.h"

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

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PoissonApplication");

NS_OBJECT_ENSURE_REGISTERED(PoissonApplication);

TypeId
PoissonApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::PoissonApplication")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<PoissonApplication>()
            .AddAttribute("DataRate",
                          "The data rate in on state.",
                          DataRateValue(DataRate("500kb/s")),
                          MakeDataRateAccessor(&PoissonApplication::m_cbrRate),
                          MakeDataRateChecker())
            .AddAttribute("PacketSize",
                          "The size of packets sent in on state",
                          UintegerValue(512),
                          MakeUintegerAccessor(&PoissonApplication::m_pktSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("Remote",
                          "The address of the destination",
                          AddressValue(),
                          MakeAddressAccessor(&PoissonApplication::m_peer),
                          MakeAddressChecker())
            .AddAttribute("Local",
                          "The Address on which to bind the socket. If not set, it is generated "
                          "automatically.",
                          AddressValue(),
                          MakeAddressAccessor(&PoissonApplication::m_local),
                          MakeAddressChecker())
            .AddAttribute("MaxBytes",
                          "The total number of bytes to send. Once these bytes are sent, "
                          "no packet is sent again, even in on state. The value zero means "
                          "that there is no limit.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&PoissonApplication::m_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("Protocol",
                          "The type of protocol to use. This should be "
                          "a subclass of ns3::SocketFactory",
                          TypeIdValue(UdpSocketFactory::GetTypeId()),
                          MakeTypeIdAccessor(&PoissonApplication::m_tid),
                          MakeTypeIdChecker())
            .AddAttribute("EnableSeqTsSizeHeader",
                          "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                          BooleanValue(false),
                          MakeBooleanAccessor(&PoissonApplication::m_enableSeqTsSizeHeader),
                          MakeBooleanChecker())
            .AddAttribute("Interval",
                        "The time to wait between packets",
                        DoubleValue(0),
                        MakeDoubleAccessor(&PoissonApplication::m_interval),
                        MakeDoubleChecker<double>())
            .AddAttribute("PacketGen",
                        "Distribution of the packet size",
                        StringValue("CBR"),
                        MakeStringAccessor(&PoissonApplication::m_packetgentype),
                        MakeStringChecker())   
            .AddAttribute("PacketSizeBound",
                        "Bound for the packet size",
                        DoubleValue(60000),
                        MakeDoubleAccessor(&PoissonApplication::m_pktSizeBound),
                        MakeDoubleChecker<double>())

            .AddTraceSource("Tx",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&PoissonApplication::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&PoissonApplication::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback")
            .AddTraceSource("TxWithSeqTsSize",
                            "A new packet is created with SeqTsSizeHeader",
                            MakeTraceSourceAccessor(&PoissonApplication::m_txTraceWithSeqTsSize),
                            "ns3::PacketSink::SeqTsSizeCallback");
    return tid;
}

PoissonApplication::PoissonApplication()
    : m_socket(nullptr),
      m_connected(false),
      m_residualBits(0),
      m_totBytes(0)
{
    m_time_id = CreateObject<ExponentialRandomVariable>();
    m_packet_gen = CreateObject<ExponentialRandomVariable>();
    NS_LOG_FUNCTION(this);
}

PoissonApplication::~PoissonApplication()
{
    NS_LOG_FUNCTION(this);
}

void
PoissonApplication::SetMaxBytes(uint64_t maxBytes)
{
    NS_LOG_FUNCTION(this << maxBytes);
    m_maxBytes = maxBytes;
}

Ptr<Socket>
PoissonApplication::GetSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}



void
PoissonApplication::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_socket = nullptr;
    m_unsentPacket = nullptr;
    // chain up
    Application::DoDispose();
}




// Application Methods
void
PoissonApplication::StartApplication() // Called at time specified by Start
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

        m_socket->SetConnectCallback(MakeCallback(&PoissonApplication::ConnectionSucceeded, this),
                                     MakeCallback(&PoissonApplication::ConnectionFailed, this));

        m_socket->Connect(m_peer);
        m_socket->SetAllowBroadcast(true);
        m_socket->ShutdownRecv();
    }
    m_cbrRateFailSafe = m_cbrRate;

    if (m_connected)
    {

        m_time_id->SetAttribute("Mean", DoubleValue (m_interval));
        if (m_packetgentype == "Exp"){
            m_packet_gen->SetAttribute("Mean", DoubleValue (m_pktSize));
            m_packet_gen->SetAttribute("Bound", DoubleValue (m_pktSizeBound));
        }
        m_sendEvent = Simulator::Schedule(Seconds(0), &PoissonApplication::SendPacket, this);
        // std::cout << "Connected" << std::endl;
    }
}

void
PoissonApplication::StopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->Close();
    }
    else
    {
        NS_LOG_WARN("PoissonApplication found null socket to close in StopApplication");
    }
}



void
PoissonApplication::SendPacket()
{
    NS_LOG_FUNCTION(this);

    unsigned size;
 
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
        if (m_packetgentype == "Exp"){
            size  = m_packet_gen->GetValue();
                if (size == 0){
                    // std::cout << "Sent: " << m_totBytes << " Max: " << m_maxBytes << std::endl; 
                    if (m_packetgentype == "cte"){
                        m_sendEvent = Simulator::Schedule(Seconds(m_interval), &PoissonApplication::SendPacket, this);
                    }else{
                        double time = m_time_id->GetValue();
                        // printf("Time: %f Seconds\n", time);
                        // std::cout << Seconds(time) << std::endl;    
                        m_sendEvent = Simulator::Schedule(Seconds((m_time_id->GetValue())), &PoissonApplication::SendPacket, this);
                    }
            
                return;
            }
        }else{
            size = m_pktSize;
        }
        // std::cout << m_packet_gen->GetValue() << std::endl;
        packet = Create<Packet>(size);
    }

    // std::cout << "PoissonApplication::SendPacket() " << Simulator::Now().GetFemtoSeconds() << std::endl;
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
        // std::cout << "Sent: " << m_totBytes << " Max: " << m_maxBytes << std::endl; 
        if (m_packetgentype == "cte"){
            m_sendEvent = Simulator::Schedule(Seconds(m_interval), &PoissonApplication::SendPacket, this);
        }else{
            double time = m_time_id->GetValue();
            // printf("Time: %f Seconds\n", time);
            // std::cout << Seconds(time) << std::endl;    
            m_sendEvent = Simulator::Schedule(Seconds((m_time_id->GetValue())), &PoissonApplication::SendPacket, this);
        }
           
    }
    else
    { 
        StopApplication();
    }
}

void
PoissonApplication::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    m_connected = true;
}

void
PoissonApplication::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Can't connect");
}

} // Namespace ns3
