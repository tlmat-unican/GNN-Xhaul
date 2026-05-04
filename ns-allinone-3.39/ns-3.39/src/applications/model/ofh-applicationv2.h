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
// Adapted from ApplicationPOISSON_APP in GTNetS.

#ifndef POISSON_APP_APPLICATION_H
#define POISSON_APP_APPLICATION_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/seq-ts-size-header.h"
#include "ns3/traced-callback.h"
#include "ns3/double.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

class Address;
class RandomVariableStream;
class Socket;

/**
 * \ingroup applications
 * \defgroup POISSON_APP ofhapplication
 *
 * This traffic generator follows an On/Off pattern: after
 * Application::StartApplication
 * is called, "On" and "Off" states alternate. The duration of each of
 * these states is determined with the onTime and the offTime random
 * variables. During the "Off" state, no traffic is generated.
 * During the "On" state, cbr traffic is generated. This cbr traffic is
 * characterized by the specified "data rate" and "packet size".
 */
/**
 * \ingroup POISSON_APP
 *
 * \brief Generate traffic to a single destination according to an
 *        POISSON_APP pattern.
 *
 * This traffic generator follows an On/Off pattern: after
 * Application::StartApplication
 * is called, "On" and "Off" states alternate. The duration of each of
 * these states is determined with the onTime and the offTime random
 * variables. During the "Off" state, no traffic is generated.
 * During the "On" state, cbr traffic is generated. This cbr traffic is
 * characterized by the specified "data rate" and "packet size".
 *
 * Note:  When an application is started, the first packet transmission
 * occurs _after_ a delay equal to (packet size/bit rate).  Note also,
 * when an application transitions into an off state in between packet
 * transmissions, the remaining time until when the next transmission
 * would have occurred is cached and is used when the application starts
 * up again.  Example:  packet size = 1000 bits, bit rate = 500 bits/sec.
 * If the application is started at time 3 seconds, the first packet
 * transmission will be scheduled for time 5 seconds (3 + 1000/500)
 * and subsequent transmissions at 2 second intervals.  If the above
 * application were instead stopped at time 4 seconds, and restarted at
 * time 5.5 seconds, then the first packet would be sent at time 6.5 seconds,
 * because when it was stopped at 4 seconds, there was only 1 second remaining
 * until the originally scheduled transmission, and this time remaining
 * information is cached and used to schedule the next transmission
 * upon restarting.
 *
 * If the underlying socket type supports broadcast, this application
 * will automatically enable the SetAllowBroadcast(true) socket option.
 *
 * If the attribute "EnableSeqTsSizeHeader" is enabled, the application will
 * use some bytes of the payload to store an header with a sequence number,
 * a timestamp, and the size of the packet sent. Support for extracting
 * statistics from this header have been added to \c ns3::PacketSink
 * (enable its "EnableSeqTsSizeHeader" attribute), or users may extract
 * the header via trace sources.  Note that the continuity of the sequence
 * number may be disrupted across On/Off cycles.
 */
class ofhapplication : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    ofhapplication();

    ~ofhapplication() override;


    int64_t AssignStreams(int64_t stream);
    /**
     * \brief Set the total number of bytes to send.
     *
     * Once these bytes are sent, no packet is sent again, even in on state.
     * The value zero means that there is no limit.
     *
     * \param maxBytes the total number of bytes to send
     */
    void SetMaxBytes(uint64_t maxBytes);

    /**
     * \brief Return a pointer to associated socket.
     * \return pointer to associated socket
     */
    Ptr<Socket> GetUserSocket() const;
    Ptr<Socket> GetControlSocket() const;


  protected:
    void DoDispose() override;

  private:
    // inherited from Application base class.
    void StartApplication() override; // Called at time specified by Start
    void StopApplication() override;  // Called at time specified by Stop




    /**
     * \brief Send a packet
     */
    void UserSendPacket();
    void ControlSendPacket();

    Ptr<Socket> m_u_socket, m_c_socket;                //!< Associated socket
    Address m_u_peer, m_c_peer;                      //!< Peer address
    Address m_u_local, m_c_local;                     //!< Local address to bind to
    bool m_u_connected, m_c_connected;                    //!< True if connected
    bool m_enableCPlane;
    DataRate m_u_cbrRate, m_c_cbrRate;                  //!< Rate that data is generated
    DataRate m_u_cbrRateFailSafe, m_c_cbrRateFailSafe;          //!< Rate that data is generated (check copy)
    uint32_t m_u_pktSize, m_c_pktSize;                  //!< Size of packets
    uint32_t m_u_residualBits, m_c_residualBits;             //!< Number of generated, but not sent, bits
    Time m_u_lastStartTime, m_c_lastStartTime;                //!< Time last packet sent
    uint64_t m_u_maxBytes, m_c_maxBytes;                 //!< Limit total number of bytes sent
    uint64_t m_u_totBytes, m_c_totBytes;                 //!< Total bytes sent so far
    EventId m_startStopEvent;            //!< Event id for next start or stop event
    EventId m_u_sendEvent, m_c_sendEvent;                 //!< Event id of pending "send packet" event
    double m_u_interval, m_c_interval;
    std::string type_of_pattern, type_of_packetsize;
    Ptr<RandomVariableStream> m_u_time_id, m_c_time_id;
    Ptr<RandomVariableStream> m_u_packetSize_gen, m_c_packetSize_gen;
    uint32_t m_u_seq{0}, m_c_seq{0};                   //!< Sequence
    Ptr<Packet> m_u_unsentPacket, m_c_unsentPacket ;          //!< Unsent packet cached for future attempt
    
    // Global
    TypeId m_tid;                        //!< Type of the socket used
    bool m_enableSeqTsSizeHeader{false}; //!< Enable or disable the use of SeqTsSizeHeader

    /// Traced Callback: transmitted packets.
    TracedCallback<Ptr<const Packet>> m_txTrace;

    /// Callbacks for tracing the packet Tx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_txTraceWithAddresses;

    /// Callback for tracing the packet Tx events, includes source, destination, the packet sent,
    /// and header
    TracedCallback<Ptr<const Packet>, const Address&, const Address&, const SeqTsSizeHeader&>
        m_txTraceWithSeqTsSize;

  private:

    /**
     * \brief Handle a Connection Succeed event
     * \param socket the connected socket
     */
    void UserConnectionSucceeded(Ptr<Socket> socket);
    void ControlConnectionSucceeded(Ptr<Socket> socket);
    /**
     * \brief Handle a Connection Failed event
     * \param socket the not connected socket
     */
    void UserConnectionFailed(Ptr<Socket> socket);
    void ControlConnectionFailed(Ptr<Socket> socket);
   

};

} // namespace ns3

#endif /* POISSON_APP_APPLICATION_H */
