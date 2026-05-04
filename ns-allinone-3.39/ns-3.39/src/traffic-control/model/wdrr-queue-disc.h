/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
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
 * Authors: Pasquale Imputato <p.imputato@gmail.com>
 *          Stefano Avallone <stefano.avallone@unina.it>
 */

#ifndef WDRR_QUEUE_DISC
#define WDRR_QUEUE_DISC

#include "ns3/object-factory.h"
#include "ns3/queue-disc.h"
#include "ns3/vector.h"
#include "ns3/attribute.h"
#include "ns3/object-vector.h"

#include <list>
#include <map>
#include <vector>
#include <array>
#include <iostream>
#include <fstream>

namespace ns3
{
//defining things
typedef std::vector<int> Quantum;

typedef std::map<int,int> MapQueue;


/**
 * \ingroup traffic-control
 *
 * \brief A flow queue used by the Wdrr queue disc
 */

class WdrrFlow : public QueueDiscClass
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
    WdrrFlow();

    ~WdrrFlow() override;

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
    int32_t m_deficit;   //!< the deficit for this flow
    FlowStatus m_status; //!< the status of this flow
    uint32_t m_index;    //!< the index for this flow
};

/**
 * \ingroup traffic-control
 *
 * \brief A Wdrr packet queue disc
 */

class WdrrQueueDisc : public QueueDisc
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    /**
     * \brief FqCoDelQueueDisc constructor
     */
    WdrrQueueDisc();/*  */

    ~WdrrQueueDisc() override;

    /**
     * \brief Set the quantum value.
     *
     * \param quantum The number of bytes each queue gets to dequeue on each round of the scheduling
     * algorithm
     */
    void SetQuantum(uint32_t id, uint32_t quantum);
    // uint32_t GetSize (void) const;


    void SetInitialValues(std::vector<int> values);
  
    /**
     * \brief Get the quantum value.
     *
     * \returns The number of bytes each queue gets to dequeue on each round of the scheduling
     * algorithm
     */
    uint32_t GetQuantum(uint32_t id) const;

    // Reasons for dropping packets
    static constexpr const char* UNCLASSIFIED_DROP =
        "Unclassified drop"; //!< No packet filter able to classify packet
    static constexpr const char* OVERLIMIT_DROP = "Overlimit drop"; //!< Overlimit dropped packets

  private:
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    bool CheckConfig() override;
    void InitializeParams() override;

    /**
     * \brief Drop a packet from the head of the queue with the largest current byte count
     * \return the index of the queue with the largest current byte count
     */

    bool m_useEcn; //!< True if ECN is used (packets are marked instead of being dropped)
    /**
     * Compute the index of the queue for the flow having the given flowHash,
     * according to the set associative hash approach.
     *
     * \param flowHash the hash of the flow 5-tuple
     * \return the index of the queue for the given flow
     */

    std::ofstream Bufferlog;
 
    uint32_t WdrrDrop();
   
    uint32_t m_flows;                //!< Number of flow queues
    std::list<Ptr<WdrrFlow>> m_newFlows; //!< The list of new flows
    std::list<Ptr<WdrrFlow>> m_oldFlows; //!< The list of old flows

    uint32_t m_dropBatchSize;        //!< Max number of packets dropped from the fat flow
    std::map<uint32_t, uint32_t> m_flowsIndices; //!< Map with the index of class for each flow

    ObjectFactory m_flowFactory;      //!< Factory to create a new flow
    ObjectFactory m_queueDiscFactory; //!< Factory to create a new queue

    Quantum m_quantum; //!< Deficit assigned to flows at each round
    MapQueue mapuca;
};


/**
 * Serialize the priomap to the given ostream
 *
 * \param os
 * \param priomap
 *
 * \return std::ostream
 */
std::ostream& operator<<(std::ostream& os, const Quantum& Quantum);

/**
 * Serialize from the given istream to this priomap.
 *
 * \param is
 * \param priomap
 *
 * \return std::istream
 */
std::istream& operator>>(std::istream& is, Quantum& Quantum);

ATTRIBUTE_HELPER_HEADER(Quantum);



/**
 * Serialize the priomap to the given ostream
 *
 * \param os
 * \param priomap
 *
 * \return std::ostream
 */
std::ostream& operator<<(std::ostream& os, const MapQueue& map2);

/**
 * Serialize from the given istream to this priomap.
 *
 * \param is
 * \param priomap
 *
 * \return std::istream
 */
std::istream& operator>>(std::istream& is, MapQueue& map2);

ATTRIBUTE_HELPER_HEADER(MapQueue);


} // namespace ns3

#endif /* FQ_CODEL_QUEUE_DISC */
