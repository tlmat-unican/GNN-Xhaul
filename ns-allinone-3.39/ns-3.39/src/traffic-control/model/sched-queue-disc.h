/*
 * Copyright (c) 2007, 2014 University of Washington
 *               2015 Universita' degli Studi di Napoli Federico II
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:  Stefano Avallone <stavallo@unina.it>
 *           Tom Henderson <tomhend@u.washington.edu>
 */

#ifndef SCHED_H
#define SCHED_H

#include "ns3/queue-disc.h"
#include "ns3/object-factory.h"

namespace ns3
{

/**
 * \ingroup traffic-control
 *
 * Linux pfifo_fast is the default priority queue enabled on Linux
 * systems. Packets are enqueued in three FIFO droptail queues according
 * to three priority bands based on the packet priority.
 *
 * The system behaves similar to three ns3::DropTail queues operating
 * together, in which packets from higher priority bands are always
 * dequeued before a packet from a lower priority band is dequeued.
 *
 * The queue disc capacity, i.e., the maximum number of packets that can
 * be enqueued in the queue disc, is set through the limit attribute, which
 * plays the same role as txqueuelen in Linux. If no internal queue is
 * provided, three DropTail queues having each a capacity equal to limit are
 * created by default. User is allowed to provide queues, but they must be
 * three, operate in packet mode and each have a capacity not less
 * than limit. No packet filter can be provided.
 */



class SchedFlow : public QueueDiscClass
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    /**
     * \brief FqCoDelFlow constructor
     */
    SchedFlow();

    ~SchedFlow() override;

    /**
     * \enum FlowStatus
     * \brief Used to determine the status of this flow queue
     */
    enum FlowStatus
    {
        INACTIVE,
        NEW_FLOW,
        OLD_FLOW
    };

    /**
     * \brief Set the deficit for this flow
     * \param deficit the deficit for this flow
     */
    void SetDeficit(uint32_t deficit);
    /**
     * \brief Get the deficit for this flow
     * \return the deficit for this flow
     */
    int32_t GetDeficit() const;
    /**
     * \brief Increase the deficit for this flow
     * \param deficit the amount by which the deficit is to be increased
     */
    void IncreaseDeficit(int32_t deficit);
    /**
     * \brief Set the status for this flow
     * \param status the status for this flow
     */
    void SetStatus(FlowStatus status);
    /**
     * \brief Get the status of this flow
     * \return the status of this flow
     */
    FlowStatus GetStatus() const;
    /**
     * \brief Set the index for this flow
     * \param index the index for this flow
     */
    void SetIndex(uint32_t index);
    /**
     * \brief Get the index of this flow
     * \return the index of this flow
     */
    uint32_t GetIndex() const;

  private:
       /**
     * \brief Drop a packet from the head of the queue with the largest current byte count
     * \return the index of the queue with the largest current byte count
     */
  
    int32_t m_deficit;   //!< the deficit for this flow
    FlowStatus m_status; //!< the status of this flow
    uint32_t m_index;    //!< the index for this flow
    uint32_t m_setWays;  //!< size of a set of queues (used by set associative hash)
};







































class SchedQueueDisc : public QueueDisc
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    /**
     * \brief SchedQueueDisc constructor
     *
     * Creates a queue with a depth of 1000 packets per band by default
     */
    SchedQueueDisc();

    ~SchedQueueDisc() override;

     /**
     * \brief Set the quantum value.
     *
     * \param quantum The number of bytes each queue gets to dequeue on each round of the scheduling
     * algorithm
     */
    void SetQuantum(uint32_t quantum);

    /**
     * \brief Get the quantum value.
     *
     * \returns The number of bytes each queue gets to dequeue on each round of the scheduling
     * algorithm
     */
    uint32_t GetQuantum() const;


     // Reasons for dropping packets
    static constexpr const char* UNCLASSIFIED_DROP =
        "Unclassified drop"; //!< No packet filter able to classify packet
    static constexpr const char* OVERLIMIT_DROP = "Overlimit drop"; //!< Overlimit dropped packets

    // Reasons for dropping packets
    // static constexpr const char* LIMIT_EXCEEDED_DROP =
    //     "Queue disc limit exceeded"; //!< Packet dropped due to queue disc limit exceeded

  private:
    /**
     * Priority to band map. Values are taken from the prio2band array used by
     * the Linux pfifo_fast queue disc.
     */
    static const uint32_t prio2band[2];
    
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    Ptr<const QueueDiscItem> DoPeek() override;
    bool CheckConfig() override;
    void InitializeParams() override;

    uint32_t SchedDrop();
    /**
     * Compute the index of the queue for the flow having the given flowHash,
     * according to the set associative hash approach.
     *
     * \param flowHash the hash of the flow 5-tuple
     * \return the index of the queue for the given flow
     */
    uint32_t SetAssociativeHash(uint32_t flowHash);
    uint32_t m_quantum;              //!< Deficit assigned to flows at each round
    uint32_t m_flows;                //!< Number of flow queues
    uint32_t m_setWays;              //!< size of a set of queues (used by set associative hash)
    uint32_t m_perturbation;         //!< hash perturbation value
    uint32_t m_dropBatchSize; //!< Max number of packets dropped from the fat flow
    bool m_enableSetAssociativeHash; //!< whether to enable set associative hash
    
    std::list<Ptr<SchedFlow>> m_newFlows; //!< The list of new flows
    std::list<Ptr<SchedFlow>> m_oldFlows; //!< The list of old flows

    std::map<uint32_t, uint32_t> m_flowsIndices; //!< Map with the index of class for each flow
    std::map<uint32_t, uint32_t> m_tags;         //!< Tags used by set associative hash

    ObjectFactory m_flowFactory;      //!< Factory to create a new flow
    ObjectFactory m_queueDiscFactory; //!< Factory to create a new queue



};

} // namespace ns3

#endif /* SCHED_H */
