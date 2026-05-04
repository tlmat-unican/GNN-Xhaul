/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#ifndef PRIO_QUEUE_DSCP_DISC_H
#define PRIO_QUEUE_DSCP_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/ipv4-header.h"
#include <map>
#include <vector>

namespace ns3
{

// Typedefs
typedef std::vector<int> Quantum;
typedef std::map<int,int> MapQueue;

/**
 * \ingroup traffic-control
 *
 * PrioQueueDscpDisc is a classful queueing discipline that prioritizes packets
 * based on DSCP markings. Packets are assigned to bands (classes) according to
 * the DSCP -> band mapping in mapuca. Negative band (-1) means the packet is
 * dropped.
 *
 * By default, three FIFO queue discs are created if no child queues are provided.
 */
class PrioQueueDscpDisc : public QueueDisc
{
public:
    static TypeId GetTypeId();

    PrioQueueDscpDisc();
    ~PrioQueueDscpDisc() override;

    /**
     * Set the band assigned to packets with the specified priority.
     *
     * \param prio Packet priority (0-15)
     * \param band Band index assigned to this priority
     */
    void SetBandForPriority(uint8_t prio, uint16_t band);
    Ptr<QueueDiscItem> MarkingPacket(Ptr<QueueDiscItem> item);
    /**
     * Get the band assigned to packets with the specified priority.
     *
     * \param prio Packet priority (0-15)
     * \returns Band index for this priority
     */
    uint16_t GetBandForPriority(uint8_t prio) const;

    static constexpr const char* LIMIT_EXCEEDED_DROP = "LIMIT_EXCEEDED_DROP";

private:
    // QueueDisc interface overrides
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    Ptr<const QueueDiscItem> DoPeek() override;
    bool CheckConfig() override;
    void InitializeParams() override;
   

    /**
     * Get the band index for a given DSCP value.
     * \param dscp DSCP marking from IPv4 header
     * \return band index (>=0) or -1 if packet should be dropped
     */
    int GetBandForDscp(int dscp) const;

    // Map DSCP -> band index. -1 indicates drop
    MapQueue mapuca;
    uint32_t nBands;
    MapQueue markingMap;
};

} // namespace ns3

#endif /* PRIO_QUEUE_DSCP_DISC_H */