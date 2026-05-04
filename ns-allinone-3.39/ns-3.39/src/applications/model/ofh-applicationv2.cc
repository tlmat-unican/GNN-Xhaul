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

#include "ofh-applicationv2.h"

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

NS_LOG_COMPONENT_DEFINE("ofhapplication");

NS_OBJECT_ENSURE_REGISTERED(ofhapplication);

TypeId
ofhapplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::ofhapplication")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<ofhapplication>()
            .AddAttribute("U-PacketSize",
                          "The size of packets sent in on state",
                          UintegerValue(512),
                          MakeUintegerAccessor(&ofhapplication::m_u_pktSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("C-PacketSize",
                         "The size of packets sent in on state",
                          UintegerValue(512),
                          MakeUintegerAccessor(&ofhapplication::m_c_pktSize),
                            MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("U-Local",
                          "The Address on which to bind the socket. If not set, it is generated "
                          "automatically.",
                          AddressValue(),
                          MakeAddressAccessor(&ofhapplication::m_u_local),
                          MakeAddressChecker())
            .AddAttribute("C-Local",
                          "The Address on which to bind the socket. If not set, it is generated "
                          "automatically.",
                          AddressValue(),
                          MakeAddressAccessor(&ofhapplication::m_c_local),
                          MakeAddressChecker())
            .AddAttribute("C-Plane",
                          "The address of the destination",
                          AddressValue(),
                          MakeAddressAccessor(&ofhapplication::m_c_peer),
                          MakeAddressChecker())
            .AddAttribute("U-Plane",
                          "The address of the destination",
                          AddressValue(),
                          MakeAddressAccessor(&ofhapplication::m_u_peer),
                          MakeAddressChecker())
            .AddAttribute("U-MaxBytes",
                          "The total number of bytes to send. Once these bytes are sent, "
                          "no packet is sent again, even in on state. The value zero means "
                          "that there is no limit.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&ofhapplication::m_u_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("C-MaxBytes",
                          "The total number of bytes to send. Once these bytes are sent, "
                          "no packet is sent again, even in on state. The value zero means "
                          "that there is no limit.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&ofhapplication::m_c_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("CUPlane", 
                        "Enable both planes Control and User Plane (CU-Plane)",
                        BooleanValue(false),
                        MakeBooleanAccessor(&ofhapplication::m_enableCPlane),
                        MakeBooleanChecker())                  


            
            .AddAttribute("Protocol",
                          "The type of protocol to use. This should be "
                          "a subclass of ns3::SocketFactory",
                          TypeIdValue(UdpSocketFactory::GetTypeId()),
                          MakeTypeIdAccessor(&ofhapplication::m_tid),
                          MakeTypeIdChecker())
            .AddAttribute("EnableSeqTsSizeHeader",
                          "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                          BooleanValue(false),
                          MakeBooleanAccessor(&ofhapplication::m_enableSeqTsSizeHeader),
                          MakeBooleanChecker())
            .AddAttribute("U-Interval",
                        "A RandomVariableStream used to pick the time to wait between user plane packets",
                        DoubleValue(0),
                        MakeDoubleAccessor(&ofhapplication::m_u_interval),
                        MakeDoubleChecker<double>())
            .AddAttribute("Trafficpattern",
                        "",
                        StringValue("Constant"),
                        MakeStringAccessor(&ofhapplication::type_of_pattern),
                        MakeStringChecker())
            .AddAttribute("PacketDistribution",
                        "",
                        StringValue("Constant"),
                        MakeStringAccessor(&ofhapplication::type_of_packetsize),
                        MakeStringChecker())

            .AddAttribute("C-Interval",
                        "A RandomVariableStream used to pick the time to wait between user plane packets",
                        DoubleValue(0),
                        MakeDoubleAccessor(&ofhapplication::m_c_interval),
                        MakeDoubleChecker<double>())
            .AddTraceSource("Tx",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&ofhapplication::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&ofhapplication::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback")
            .AddTraceSource("TxWithSeqTsSize",
                            "A new packet is created with SeqTsSizeHeader",
                            MakeTraceSourceAccessor(&ofhapplication::m_txTraceWithSeqTsSize),
                            "ns3::PacketSink::SeqTsSizeCallback");
    return tid;
}

ofhapplication::ofhapplication()
    :   m_u_socket(nullptr),
        m_u_connected(false),
        m_u_residualBits(0),
        m_u_lastStartTime(Seconds(0)),
        m_u_totBytes(0),
        m_u_unsentPacket(nullptr),
        m_c_socket(nullptr),
        m_c_connected(false),
        m_c_residualBits(0),
        m_c_lastStartTime(Seconds(0)),
        m_c_totBytes(0),
        m_c_unsentPacket(nullptr)
{
   
    m_u_packetSize_gen = CreateObject<ExponentialRandomVariable>();
    m_u_packetSize_gen->SetAttribute("Mean", DoubleValue(m_u_pktSize));
    m_c_packetSize_gen = CreateObject<ExponentialRandomVariable>();
    m_c_packetSize_gen->SetAttribute("Mean", DoubleValue(m_c_pktSize));
    NS_LOG_FUNCTION(this);
}

ofhapplication::~ofhapplication()
{
    NS_LOG_FUNCTION(this);
}

void
ofhapplication::SetMaxBytes(uint64_t maxBytes)
{
    NS_LOG_FUNCTION(this << maxBytes);
    m_u_maxBytes = maxBytes;
}

Ptr<Socket>
ofhapplication::GetUserSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_u_socket;
}


Ptr<Socket>
ofhapplication::GetControlSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_c_socket;
}

int64_t
ofhapplication::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);

    m_u_time_id->SetStream(stream);
    m_c_time_id->SetStream(stream + 1);
    return 4;
}


void
ofhapplication::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_u_socket = nullptr;
    m_u_unsentPacket = nullptr;
    m_c_socket = nullptr;
    m_c_unsentPacket = nullptr;
    // chain up
    Application::DoDispose();
}




// Application Methods
void
ofhapplication::StartApplication() // Called at time specified by Start
{
    NS_LOG_FUNCTION(this);

    
    // Create the socket if not already
    if (!m_u_socket)
    {
        m_u_socket = Socket::CreateSocket(GetNode(), m_tid);
        NS_LOG_INFO("Socket init");
        int ret_u = -1;
        if (!m_u_local.IsInvalid())
        {
            NS_ABORT_MSG_IF((Inet6SocketAddress::IsMatchingType(m_u_peer) &&
                             InetSocketAddress::IsMatchingType(m_u_local)) ||
                                (InetSocketAddress::IsMatchingType(m_u_peer) &&
                                 Inet6SocketAddress::IsMatchingType(m_u_local)),
                            "Incompatible peer and local address IP version");
            ret_u = m_u_socket->Bind(m_u_local);
        }
        else
        {
            if (Inet6SocketAddress::IsMatchingType(m_u_peer))
            {
                ret_u = m_u_socket->Bind6();
            }
            else if (InetSocketAddress::IsMatchingType(m_u_peer) ||
                     PacketSocketAddress::IsMatchingType(m_u_peer))
            {
                ret_u = m_u_socket->Bind();
            }
        }


        

        NS_LOG_INFO("Configuration done");
        if (ret_u == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket: U-plane");
        }

        
        m_u_socket->SetConnectCallback(MakeCallback(&ofhapplication::UserConnectionSucceeded, this),
                                     MakeCallback(&ofhapplication::UserConnectionFailed, this));

        m_u_socket->Connect(m_u_peer);
        m_u_socket->SetAllowBroadcast(true);
        m_u_socket->ShutdownRecv();
        // std::cout << "User Callback - Configuration done" << std::endl;
        NS_LOG_INFO("User Callback - Configuration done");
        
    }
    m_u_cbrRateFailSafe = m_u_cbrRate;
    if (m_u_connected)
    {   
        
        if(type_of_pattern == "Exponential"){
            Ptr<ExponentialRandomVariable> u_time = CreateObject<ExponentialRandomVariable>();
            u_time->SetAttribute("Mean", DoubleValue(m_u_interval));
            m_u_sendEvent = Simulator::Schedule(Seconds(u_time->GetValue()), &ofhapplication::UserSendPacket, this);
        }else{
            Ptr<UniformRandomVariable> u_time = CreateObject<UniformRandomVariable>();
            u_time->SetAttribute("Min", DoubleValue(0));
            u_time->SetAttribute("Max", DoubleValue(m_u_interval));
            m_u_sendEvent = Simulator::Schedule(Seconds(u_time->GetValue()), &ofhapplication::UserSendPacket, this);
        }
       
        
    }
    
    if (m_enableCPlane)
    {
        if(!m_c_socket){
             m_c_socket = Socket::CreateSocket(GetNode(), m_tid);
        }
        int ret_c = -1;

        if (!m_c_local.IsInvalid())
        {
            NS_ABORT_MSG_IF((Inet6SocketAddress::IsMatchingType(m_c_peer) &&
                             InetSocketAddress::IsMatchingType(m_c_local)) ||
                                (InetSocketAddress::IsMatchingType(m_c_peer) &&
                                 Inet6SocketAddress::IsMatchingType(m_c_local)),
                            "Incompatible peer and local address IP version");
            ret_c = m_c_socket->Bind(m_c_local);
        }
        else
        {
            if (Inet6SocketAddress::IsMatchingType(m_c_peer))
            {
                ret_c = m_c_socket->Bind6();
            }
            else if (InetSocketAddress::IsMatchingType(m_c_peer) ||
                     PacketSocketAddress::IsMatchingType(m_c_peer))
            {
                ret_c = m_c_socket->Bind();
            }
        }
        if (ret_c == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket: C-plane");
        }


        m_c_socket->SetConnectCallback(MakeCallback(&ofhapplication::ControlConnectionSucceeded, this),
                                     MakeCallback(&ofhapplication::ControlConnectionFailed, this));
       
        m_c_socket->Connect(m_c_peer);
        m_c_socket->SetAllowBroadcast(true);
        m_c_socket->ShutdownRecv();
        NS_LOG_INFO("Control Callback - Configuration done");


                  
        if(m_c_connected)
        {
            m_c_cbrRateFailSafe = m_c_cbrRate;
            // m_c_time_id->SetAttribute("Mean", DoubleValue (m_c_interval));
            if(type_of_pattern == "Exponential"){
                Ptr<ExponentialRandomVariable> c_time = CreateObject<ExponentialRandomVariable>();
                c_time->SetAttribute("Mean", DoubleValue(m_c_interval));
                m_c_sendEvent = Simulator::Schedule(Seconds(c_time->GetValue()), &ofhapplication::ControlSendPacket, this);
            }else{
                m_c_sendEvent = Simulator::Schedule(Seconds(m_c_interval), &ofhapplication::ControlSendPacket, this);
            }

        }
    }
}

void
ofhapplication::StopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    if (m_u_socket)
    {
        m_u_socket->Close();
        if (m_enableCPlane){
            m_c_socket->Close();
        }
 
    }
    else
    {
        NS_LOG_WARN("ofhapplication found null socket to close in StopApplication");
    }
}



void
ofhapplication::UserSendPacket()
{
    NS_LOG_FUNCTION(this);

    unsigned size;
 
    NS_ASSERT(m_u_sendEvent.IsExpired());

    Ptr<Packet> packet;
    if (m_enableSeqTsSizeHeader)
    {
        Address from;
        Address to;
        m_u_socket->GetSockName(from);
        m_u_socket->GetPeerName(to);
        SeqTsSizeHeader header;
        header.SetSeq(m_u_seq++);
        header.SetSize(m_u_pktSize);
        NS_ABORT_IF(m_u_pktSize < header.GetSerializedSize());
        packet = Create<Packet>(m_u_pktSize - header.GetSerializedSize());
        // Trace before adding header, for consistency with PacketSink
        m_txTraceWithSeqTsSize(packet, from, to, header);
        packet->AddHeader(header);
    }
    else
    {
        if (type_of_packetsize == "Exponential"){
            size = m_u_packetSize_gen->GetValue();
            if (size == 0){
                size = 1;
            }
        }else{
            size = m_u_pktSize; 
        }
       
        packet = Create<Packet>(size);
    }
    
    int actual = m_u_socket->Send(packet);
    if ((unsigned)actual == m_u_pktSize)
    {  //std::cout << "sent" << std::endl;
        m_txTrace(packet);
        m_u_totBytes += m_u_pktSize;
        m_u_unsentPacket = nullptr;
        Address localAddress;
        m_u_socket->GetSockName(localAddress);
        if (InetSocketAddress::IsMatchingType(m_u_peer))
        {
            // std::cout << "sent" << std::endl;
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << InetSocketAddress::ConvertFrom(m_u_peer).GetIpv4() << " port "
                                   << InetSocketAddress::ConvertFrom(m_u_peer).GetPort()
                                   << " total Tx " << m_u_totBytes << " bytes");
                                //    std::cout << "sent-crashed" << std::endl;
            m_txTraceWithAddresses(packet, localAddress, InetSocketAddress::ConvertFrom(m_u_peer));
            //std::cout << "sent-done" << std::endl;
        }
        else if (Inet6SocketAddress::IsMatchingType(m_u_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << Inet6SocketAddress::ConvertFrom(m_u_peer).GetIpv6() << " port "
                                   << Inet6SocketAddress::ConvertFrom(m_u_peer).GetPort()
                                   << " total Tx " << m_u_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, Inet6SocketAddress::ConvertFrom(m_u_peer));
        }
    }
    else
    {
        NS_LOG_DEBUG("Unable to send packet; actual " << actual << " size " << m_u_pktSize
                                                      << "; caching for later attempt");
        m_u_unsentPacket = packet;
    }
 
       NS_LOG_FUNCTION(this);

    if (m_u_maxBytes == 0 || m_u_totBytes < m_u_maxBytes)
    {
         if(type_of_pattern == "Exponential"){
            Ptr<ExponentialRandomVariable> u_time = CreateObject<ExponentialRandomVariable>();
            u_time->SetAttribute("Mean", DoubleValue(m_u_interval));
            m_u_sendEvent = Simulator::Schedule(Seconds(u_time->GetValue()), &ofhapplication::UserSendPacket, this);
        }else{
            m_u_sendEvent = Simulator::Schedule(Seconds(m_u_interval), &ofhapplication::UserSendPacket, this);
        }
    }
    else
    { 
        StopApplication();
    }
}


void
ofhapplication::ControlSendPacket()
{
    NS_LOG_FUNCTION(this);


 
    NS_ASSERT(m_c_sendEvent.IsExpired());

    Ptr<Packet> packet;
    if (m_enableSeqTsSizeHeader)
    {
        Address from;
        Address to;
        m_c_socket->GetSockName(from);
        m_c_socket->GetPeerName(to);
        SeqTsSizeHeader header;
        header.SetSeq(m_c_seq++);
        header.SetSize(m_c_pktSize);
        NS_ABORT_IF(m_c_pktSize < header.GetSerializedSize());
        packet = Create<Packet>(m_c_pktSize - header.GetSerializedSize());
        // Trace before adding header, for consistency with PacketSink
        m_txTraceWithSeqTsSize(packet, from, to, header);
        packet->AddHeader(header);
    }
    else
    {
        packet = Create<Packet>(m_c_pktSize);
    }

    int actual = m_c_socket->Send(packet);
    if ((unsigned)actual == m_c_pktSize)
    {  //std::cout << "sent" << std::endl;
        m_txTrace(packet);
        m_c_totBytes += m_c_pktSize;
        m_c_unsentPacket = nullptr;
        Address localAddress;
        m_c_socket->GetSockName(localAddress);
        if (InetSocketAddress::IsMatchingType(m_c_peer))
        {
            // std::cout << "sent" << std::endl;
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << InetSocketAddress::ConvertFrom(m_c_peer).GetIpv4() << " port "
                                   << InetSocketAddress::ConvertFrom(m_c_peer).GetPort()
                                   << " total Tx " << m_c_totBytes << " bytes");
                                //    std::cout << "sent-crashed" << std::endl;
            m_txTraceWithAddresses(packet, localAddress, InetSocketAddress::ConvertFrom(m_c_peer));
            //std::cout << "sent-done" << std::endl;
        }
        else if (Inet6SocketAddress::IsMatchingType(m_c_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << Inet6SocketAddress::ConvertFrom(m_c_peer).GetIpv6() << " port "
                                   << Inet6SocketAddress::ConvertFrom(m_c_peer).GetPort()
                                   << " total Tx " << m_u_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, Inet6SocketAddress::ConvertFrom(m_c_peer));
        }
    }
    else
    {
        NS_LOG_DEBUG("Unable to send packet; actual " << actual << " size " << m_c_pktSize
                                                      << "; caching for later attempt");
        m_c_unsentPacket = packet;
    }
 
       NS_LOG_FUNCTION(this);

    if (m_c_maxBytes == 0 || m_c_totBytes < m_c_maxBytes)
    {
        if(type_of_pattern == "Exponential"){
            Ptr<ExponentialRandomVariable> c_time = CreateObject<ExponentialRandomVariable>();
            c_time->SetAttribute("Mean", DoubleValue(m_c_interval));
            m_c_sendEvent = Simulator::Schedule(Seconds(c_time->GetValue()), &ofhapplication::ControlSendPacket, this);
        }else{
            m_c_sendEvent = Simulator::Schedule(Seconds(m_c_interval), &ofhapplication::ControlSendPacket, this);
        }
    }
    else
    { 
        StopApplication();
    }
}

void
ofhapplication::UserConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    m_u_connected = true;
}

void
ofhapplication::ControlConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    m_c_connected = true;
}

void
ofhapplication::UserConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("User: Can't connect");
}


void
ofhapplication::ControlConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Control: Can't connect");
}

} // Namespace ns3
