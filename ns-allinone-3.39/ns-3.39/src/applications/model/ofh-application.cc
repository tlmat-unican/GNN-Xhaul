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

#include "ofh-application.h"

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

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OfhApplication");

NS_OBJECT_ENSURE_REGISTERED(OfhApplication);

TypeId
OfhApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::OfhApplication")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<OfhApplication>()
            .AddAttribute("U-DataRate",
                          "The data rate in on state.",
                          DataRateValue(DataRate("500kb/s")),
                          MakeDataRateAccessor(&OfhApplication::m_u_cbrRate),
                          MakeDataRateChecker())
            .AddAttribute("U-PacketSize",
                          "The size of packets sent in on state",
                          UintegerValue(512),
                          MakeUintegerAccessor(&OfhApplication::m_u_pktSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("U-Plane",
                          "The address of the destination",
                          AddressValue(),
                          MakeAddressAccessor(&OfhApplication::m_u_peer),
                          MakeAddressChecker())
            .AddAttribute("U-OnTime",
                          "A RandomVariableStream used to pick the duration of the 'On' state.",
                          StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                          MakePointerAccessor(&OfhApplication::m_u_onTime),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("U-OffTime",
                          "A RandomVariableStream used to pick the duration of the 'Off' state.",
                          StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
                          MakePointerAccessor(&OfhApplication::m_u_offTime),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("U-MaxBytes",
                          "The total number of bytes to send. Once these bytes are sent, "
                          "no packet is sent again, even in on state. The value zero means "
                          "that there is no limit.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&OfhApplication::m_u_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("U-Local",
                          "The Address on which to bind the socket. If not set, it is generated "
                          "automatically.",
                          AddressValue(),
                          MakeAddressAccessor(&OfhApplication::m_u_local),
                          MakeAddressChecker())
            .AddAttribute("C-Local",
                        "The Address on which to bind the socket. If not set, it is generated "
                        "automatically.",
                        AddressValue(),
                        MakeAddressAccessor(&OfhApplication::m_c_local),
                        MakeAddressChecker())
            .AddAttribute("C-Plane",
                          "The address of the destination",
                          AddressValue(),
                          MakeAddressAccessor(&OfhApplication::m_c_peer),
                          MakeAddressChecker())
            .AddAttribute("C-DataRate",
                          "The data rate in on state.",
                          DataRateValue(DataRate("500kb/s")),
                          MakeDataRateAccessor(&OfhApplication::m_c_cbrRate),
                          MakeDataRateChecker())
            .AddAttribute("C-PacketSize",
                          "The size of packets sent in on state",
                          UintegerValue(512),
                          MakeUintegerAccessor(&OfhApplication::m_c_pktSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("C-OnTime",
                          "A RandomVariableStream used to pick the duration of the 'On' state.",
                          StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                          MakePointerAccessor(&OfhApplication::m_c_onTime),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("C-OffTime",
                          "A RandomVariableStream used to pick the duration of the 'Off' state.",
                          StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
                          MakePointerAccessor(&OfhApplication::m_c_offTime),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("C-MaxBytes",
                          "The total number of bytes to send. Once these bytes are sent, "
                          "no packet is sent again, even in on state. The value zero means "
                          "that there is no limit.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&OfhApplication::m_c_maxBytes),
                          MakeUintegerChecker<uint64_t>())
            .AddAttribute("Protocol",
                          "The type of protocol to use. This should be "
                          "a subclass of ns3::SocketFactory",
                          TypeIdValue(UdpSocketFactory::GetTypeId()),
                          MakeTypeIdAccessor(&OfhApplication::m_tid),
                          // This should check for SocketFactory as a parent
                          MakeTypeIdChecker())
            .AddAttribute("EnableSeqTsSizeHeader",
                          "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                          BooleanValue(false),
                          MakeBooleanAccessor(&OfhApplication::m_enableSeqTsSizeHeader),
                          MakeBooleanChecker())
            .AddTraceSource("Tx",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&OfhApplication::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&OfhApplication::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback")
            .AddTraceSource("TxWithSeqTsSize",
                            "A new packet is created with SeqTsSizeHeader",
                            MakeTraceSourceAccessor(&OfhApplication::m_txTraceWithSeqTsSize),
                            "ns3::PacketSink::SeqTsSizeCallback");
    return tid;
}

OfhApplication::OfhApplication()
    : m_u_socket(nullptr),
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
    NS_LOG_FUNCTION(this);
}

OfhApplication::~OfhApplication()
{
    NS_LOG_FUNCTION(this);
}

void
OfhApplication::SetMaxBytes(uint64_t maxBytes)
{
    NS_LOG_FUNCTION(this << maxBytes);
    m_u_maxBytes = maxBytes;
    m_c_maxBytes = maxBytes/8;
}

Ptr<Socket>
OfhApplication::GetSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_u_socket;
}

int64_t
OfhApplication::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_u_onTime->SetStream(stream);
    m_u_offTime->SetStream(stream + 1);
    m_c_onTime->SetStream(stream);
    m_c_offTime->SetStream(stream + 1);
    return 4;
}

void
OfhApplication::UserDoDispose()
{
    NS_LOG_FUNCTION(this);

    UserCancelEvents();
    m_u_socket = nullptr;
    m_u_unsentPacket = nullptr;
    // chain up
    Application::DoDispose();
}

void
OfhApplication::ControlDoDispose()
{
    NS_LOG_FUNCTION(this);

    ControlCancelEvents();
    m_u_socket = nullptr;
    m_u_unsentPacket = nullptr;
    // chain up
    Application::DoDispose();
}




// Application Methods
void
OfhApplication::StartApplication() // Called at time specified by Start
{
    NS_LOG_FUNCTION(this);

    // Create the socket if not already
    if (!m_u_socket & !m_c_socket )
    {
        m_u_socket = Socket::CreateSocket(GetNode(), m_tid);
        m_c_socket = Socket::CreateSocket(GetNode(), m_tid);
        NS_LOG_INFO("Socket init");
        int ret_u = -1;
        int ret_c = -1;
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
        NS_LOG_INFO("Configuration done");
        if (ret_u == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket: U-plane");
        }

        if (ret_c == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket: C-plane");
        }
        m_u_socket->SetConnectCallback(MakeCallback(&OfhApplication::UserConnectionSucceeded, this),
                                     MakeCallback(&OfhApplication::UserConnectionFailed, this));

        m_u_socket->Connect(m_u_peer);
        m_u_socket->SetAllowBroadcast(true);
        m_u_socket->ShutdownRecv();
        //std::cout << "User Callback - Configuration done" << std::endl;
        NS_LOG_INFO("User Callback - Configuration done");
        m_c_socket->SetConnectCallback(MakeCallback(&OfhApplication::ControlConnectionSucceeded, this),
                                     MakeCallback(&OfhApplication::ControlConnectionFailed, this));
       
        m_c_socket->Connect(m_c_peer);
        m_c_socket->SetAllowBroadcast(true);
        m_c_socket->ShutdownRecv();
        NS_LOG_INFO("Control Callback - Configuration done");
        //std::cout << "Control Callback - Configuration done" << std::endl;
    }
    m_u_cbrRateFailSafe = m_u_cbrRate;
    m_c_cbrRateFailSafe = m_c_cbrRate;

    // Ensure no pending event
    UserCancelEvents();
    ControlCancelEvents();
    // If we are not yet connected, there is nothing to do here,
    // the ConnectionComplete upcall will start timers at that time.
    // If we are already connected, CancelEvents did remove the events,
    // so we have to start them again.
    if (m_u_connected)
    {
        //std::cout << "U-Plane ok" << std::endl;
        NS_LOG_INFO("U-plane ok");
        UserScheduleStartEvent();
    }

    if (m_c_connected)
    {
        //std::cout << "C-Plane ok" << std::endl;
        NS_LOG_INFO("C-plane ok");
        ControlScheduleStartEvent();
    }
}

void
OfhApplication::UserStopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    UserCancelEvents();
    if (m_u_socket)
    {
        m_u_socket->Close();
    }
    else
    {
        NS_LOG_WARN("OfhApplication found null socket to close in StopApplication");
    }
}

void
OfhApplication::UserCancelEvents()
{
    NS_LOG_FUNCTION(this);

    if (m_u_sendEvent.IsRunning() && m_u_cbrRateFailSafe == m_u_cbrRate)
    { // Cancel the pending send packet event
        // Calculate residual bits since last packet sent
        Time delta(Simulator::Now() - m_u_lastStartTime);
        int64x64_t bits = delta.To(Time::S) * m_u_cbrRate.GetBitRate();
        m_u_residualBits += bits.GetHigh();
    }
    m_u_cbrRateFailSafe = m_u_cbrRate;
    Simulator::Cancel(m_u_sendEvent);
    Simulator::Cancel(m_u_startStopEvent);
    // Canceling events may cause discontinuity in sequence number if the
    // SeqTsSizeHeader is header, and m_unsentPacket is true
    if (m_u_unsentPacket)
    {
        NS_LOG_DEBUG("Discarding cached packet upon CancelEvents ()");
    }
    m_u_unsentPacket = nullptr;
}

// Event handlers
void
OfhApplication::UserStartSending()
{
    NS_LOG_FUNCTION(this);
    m_u_lastStartTime = Simulator::Now();
    UserScheduleNextTx(); // Schedule the send packet event
    UserScheduleStopEvent();
    //std::cout << "start sending" << std::endl;
}

void
OfhApplication::UserStopSending()
{
    NS_LOG_FUNCTION(this);
    UserCancelEvents();

    UserScheduleStartEvent();
}

// Private helpers
void
OfhApplication::UserScheduleNextTx()
{
    NS_LOG_FUNCTION(this);

    if (m_u_maxBytes == 0 || m_u_totBytes < m_u_maxBytes)
    {
        NS_ABORT_MSG_IF(m_u_residualBits > m_u_pktSize * 8,
                        "Calculation to compute next send time will overflow");
        uint32_t bits = m_u_pktSize * 8 - m_u_residualBits;
        NS_LOG_LOGIC("bits = " << bits);
        Time nextTime(
            Seconds(bits / static_cast<double>(m_u_cbrRate.GetBitRate()))); // Time till next packet
        NS_LOG_LOGIC("nextTime = " << nextTime.As(Time::S));
        m_u_sendEvent = Simulator::Schedule(nextTime, &OfhApplication::UserSendPacket, this);
        //std::cout << "send pkt" << std::endl;
    }
    else
    { // All done, cancel any pending events
        UserStopApplication();
        tx_finished = true;
        ControlCancelEvents();
    }
}

void
OfhApplication::UserScheduleStartEvent()
{ // Schedules the event to start sending data (switch to the "On" state)
    NS_LOG_FUNCTION(this);

    Time offInterval = Seconds(m_u_offTime->GetValue());
    NS_LOG_LOGIC("start at " << offInterval.As(Time::S));
    m_u_startStopEvent = Simulator::Schedule(offInterval, &OfhApplication::UserStartSending, this);
    //std::cout << "start" << std::endl;
}

void
OfhApplication::UserScheduleStopEvent()
{ // Schedules the event to stop sending data (switch to "Off" state)
    NS_LOG_FUNCTION(this);

    Time onInterval = Seconds(m_u_onTime->GetValue());
    NS_LOG_LOGIC("stop at " << onInterval.As(Time::S));
    m_u_startStopEvent = Simulator::Schedule(onInterval, &OfhApplication::UserStopSending, this);
}

void
OfhApplication::UserSendPacket()
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT(m_u_sendEvent.IsExpired());
    //std::cout << "UserSendPkt" << std::endl;
    Ptr<Packet> packet;
    if (m_u_unsentPacket)
    {
        // std::cout << "m_u_unsentPacket" << std::endl;
        packet = m_u_unsentPacket;
    }
    else if (m_enableSeqTsSizeHeader)
    {
        //std::cout << "m_enableSeqTsSizeHeader" << std::endl;
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
        //std::cout << "newpkt" << std::endl;
        packet = Create<Packet>(m_u_pktSize);
    }

    int actual = m_u_socket->Send(packet);
    if ((unsigned)actual == m_u_pktSize)
    {
        //std::cout << "sent" << std::endl;
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
        //std::cout << "not-sent" << std::endl;
    }
    //std::cout << "here" <<std::endl;
    m_u_residualBits = 0;
    m_u_lastStartTime = Simulator::Now();

    UserScheduleNextTx();
    
}

void
OfhApplication::UserConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    UserScheduleStartEvent();
    m_u_connected = true;
}

void
OfhApplication::UserConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Can't connect");
}


void
OfhApplication::ControlStopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    ControlCancelEvents();
    if (m_c_socket)
    {
        m_c_socket->Close();
    }
    else
    {
        NS_LOG_WARN("OfhApplication found null socket to close in StopApplication");
    }
}

void
OfhApplication::ControlCancelEvents()
{
    NS_LOG_FUNCTION(this);

    if (m_c_sendEvent.IsRunning() && m_c_cbrRateFailSafe == m_c_cbrRate)
    { // Cancel the pending send packet event
        // Calculate residual bits since last packet sent
        Time delta(Simulator::Now() - m_c_lastStartTime);
        int64x64_t bits = delta.To(Time::S) * m_c_cbrRate.GetBitRate();
        m_c_residualBits += bits.GetHigh();
    }
    m_c_cbrRateFailSafe = m_c_cbrRate;
    Simulator::Cancel(m_c_sendEvent);
    Simulator::Cancel(m_c_startStopEvent);
    // Canceling events may cause discontinuity in sequence number if the
    // SeqTsSizeHeader is header, and m_unsentPacket is true
    if (m_c_unsentPacket)
    {
        NS_LOG_DEBUG("Discarding cached packet upon CancelEvents ()");
    }
    m_c_unsentPacket = nullptr;
}

// Event handlers
void
OfhApplication::ControlStartSending()
{
    NS_LOG_FUNCTION(this);
    m_c_lastStartTime = Simulator::Now();
    ControlScheduleNextTx(); // Schedule the send packet event
    ControlScheduleStopEvent();
}

void
OfhApplication::ControlStopSending()
{
    NS_LOG_FUNCTION(this);
    ControlCancelEvents();

    ControlScheduleStartEvent();
}

// Private helpers
void
OfhApplication::ControlScheduleNextTx()
{
    NS_LOG_FUNCTION(this);

    if (m_c_maxBytes == 0 || m_c_totBytes < m_c_maxBytes || tx_finished == false)
    {
        NS_ABORT_MSG_IF(m_c_residualBits > m_c_pktSize * 8,
                        "Calculation to compute next send time will overflow");
        uint32_t bits = m_c_pktSize * 8 - m_c_residualBits;
        NS_LOG_LOGIC("bits = " << bits);
        Time nextTime(
            Seconds(bits / static_cast<double>(m_c_cbrRate.GetBitRate()))); // Time till next packet
        NS_LOG_LOGIC("nextTime = " << nextTime.As(Time::S));
        m_c_sendEvent = Simulator::Schedule(nextTime, &OfhApplication::ControlSendPacket, this);
    }
    else
    { // All done, cancel any pending events
        ControlStopApplication();
    }
}

void
OfhApplication::ControlScheduleStartEvent()
{ // Schedules the event to start sending data (switch to the "On" state)
    NS_LOG_FUNCTION(this);

    Time offInterval = Seconds(m_c_offTime->GetValue());
    NS_LOG_LOGIC("start at " << offInterval.As(Time::S));
    m_c_startStopEvent = Simulator::Schedule(offInterval, &OfhApplication::ControlStartSending, this);
}

void
OfhApplication::ControlScheduleStopEvent()
{ // Schedules the event to stop sending data (switch to "Off" state)
    NS_LOG_FUNCTION(this);

    Time onInterval = Seconds(m_c_onTime->GetValue());
    NS_LOG_LOGIC("stop at " << onInterval.As(Time::S));
    m_c_startStopEvent = Simulator::Schedule(onInterval, &OfhApplication::ControlStopSending, this);
}

void
OfhApplication::ControlSendPacket()
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT(m_c_sendEvent.IsExpired());

    Ptr<Packet> packet;
    if (m_c_unsentPacket)
    {
        packet = m_c_unsentPacket;
    }
    else if (m_enableSeqTsSizeHeader)
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
    {
        m_txTrace(packet);
        m_c_totBytes += m_c_pktSize;
        m_c_unsentPacket = nullptr;
        Address localAddress;
        m_c_socket->GetSockName(localAddress);
        if (InetSocketAddress::IsMatchingType(m_c_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << InetSocketAddress::ConvertFrom(m_c_peer).GetIpv4() << " port "
                                   << InetSocketAddress::ConvertFrom(m_c_peer).GetPort()
                                   << " total Tx " << m_c_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, InetSocketAddress::ConvertFrom(m_c_peer));
        }
        else if (Inet6SocketAddress::IsMatchingType(m_c_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                   << packet->GetSize() << " bytes to "
                                   << Inet6SocketAddress::ConvertFrom(m_c_peer).GetIpv6() << " port "
                                   << Inet6SocketAddress::ConvertFrom(m_c_peer).GetPort()
                                   << " total Tx " << m_c_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, Inet6SocketAddress::ConvertFrom(m_c_peer));
        }
    }
    else
    {
        NS_LOG_DEBUG("Unable to send packet; actual " << actual << " size " << m_c_pktSize
                                                      << "; caching for later attempt");
        m_c_unsentPacket = packet;
    }
    m_c_residualBits = 0;
    m_c_lastStartTime = Simulator::Now();
    ControlScheduleNextTx();
}

void
OfhApplication::ControlConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    ControlScheduleStartEvent();
    m_c_connected = true;
}

void
OfhApplication::ControlConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Can't connect");
}


} // Namespace ns3
