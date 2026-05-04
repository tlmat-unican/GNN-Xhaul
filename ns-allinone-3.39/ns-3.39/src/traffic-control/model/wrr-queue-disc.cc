/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
 *
 *
 * Default Queueing, Dropping and Scheduling Configuration - Configure Catalyst 3750 QoS
 */

#include "wrr-queue-disc.h"


#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/pointer.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/queue.h"
#include "ns3/object-map.h"
#include "ns3/wdrr-queue-disc.h"

#include <algorithm>
#include <iterator>
#include <cassert>


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WrrQueueDisc");
NS_OBJECT_ENSURE_REGISTERED(WrrFlow);




TypeId
WrrFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WrrFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<WrrFlow>();
    return tid;
}

WrrFlow::WrrFlow()
    : m_deficit(0),
      m_status(INACTIVE),
      m_index(0)
{
    NS_LOG_FUNCTION(this);
}

WrrFlow::~WrrFlow()
{
    NS_LOG_FUNCTION(this);
}

void
WrrFlow::SetDeficit(uint32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit = deficit;
}

int32_t
WrrFlow::GetDeficit() const
{
    NS_LOG_FUNCTION(this);
    return m_deficit;
}

void
WrrFlow::IncreaseDeficit(int32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit += deficit;
}

void
WrrFlow::SetStatus(FlowStatus status)
{
    NS_LOG_FUNCTION(this);
    m_status = status;
}

WrrFlow::FlowStatus
WrrFlow::GetStatus() const
{
    NS_LOG_FUNCTION(this);
    return m_status;
}

void
WrrFlow::SetIndex(uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_index = index;
}

uint32_t
WrrFlow::GetIndex() const
{
    return m_index;
}

NS_OBJECT_ENSURE_REGISTERED(WrrQueueDisc);

TypeId
WrrQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::WrrQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<WrrQueueDisc>()
            .AddAttribute("MaxSize",
                          "The maximum number of packets accepted by this queue disc",
                          QueueSizeValue(QueueSize("10240p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("Flows",
                          "The number of queues into which the incoming packets are classified",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&WrrQueueDisc::m_flows),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Quantum",
                          "The quantum to band mapping.",
                          QuantumValue(Quantum{{1,1}}),
                          MakeQuantumAccessor(&WrrQueueDisc::m_quantum),
                          MakeQuantumChecker())
             .AddAttribute("DropBatchSize",
                          "The maximum number of packets dropped from the fat flow",
                          UintegerValue(64),
                          MakeUintegerAccessor(&WrrQueueDisc::m_dropBatchSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MapQueue",
                          "It can be used in order to map dscp marking with the queues <DSCP value> to <band>",
                          MapQueueValue(MapQueue{{1, 2},{2, 4}}),
                          MakeMapQueueAccessor(&WrrQueueDisc::mapuca),
                          MakeMapQueueChecker())
            .AddAttribute("Bands",
                          "The number of bands (queues) in the WRR scheduler",
                          UintegerValue(2),
                          MakeUintegerAccessor(&WrrQueueDisc::nbands),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

WrrQueueDisc::WrrQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
      m_quantum(0)
{
    NS_LOG_FUNCTION(this);
}

WrrQueueDisc::~WrrQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
WrrQueueDisc::SetQuantum(uint32_t id, uint32_t quantum)
{
    NS_LOG_FUNCTION(this << quantum);
    m_quantum[id] = quantum;
}

uint32_t
WrrQueueDisc::GetQuantum(uint32_t id) const
{
    return m_quantum[id];
}



bool
WrrQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
     NS_LOG_FUNCTION(this << item);

    uint32_t band = 0;

    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();

    // Extract DSCP value from the IP header
    int dscp = ipHeader.GetDscp();


    auto it = mapuca.find(dscp);
    // If the port is found in the map, assign the corresponding DSCP value
    if (it != mapuca.end()) {
        // std::cout << dscp << std::endl;
        band = it->second;
        NS_LOG_INFO(band);
    }
    // if (dscp == 1){
        // std::cout << "Packet with DSCP " << dscp << " mapped to band " << band << std::endl;
    // }
    
    Ptr<WrrFlow> flow;
    if (m_flowsIndices.find(band) == m_flowsIndices.end())
    {       
        Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc>();
        qd->Initialize();
        flow = m_flowFactory.Create<WrrFlow>();
        NS_LOG_DEBUG("Creating a new flow queue with index");
        flow->SetIndex(band);
        flow->SetQueueDisc(qd);
        flow->IncreaseDeficit(m_quantum[flow->GetIndex()]);
        AddQueueDiscClass(flow);
        m_flowsIndices[band] =  GetNQueueDiscClasses() - 1;
    }
    else
    {
        flow = StaticCast<WrrFlow>(GetQueueDiscClass(m_flowsIndices[band]));
    }
    // std::cout << "DSCP: " << dscp << " mapped to band: " << band  << " quantum "<< flow->GetDeficit() << std::endl;
    if (flow->GetStatus() == WrrFlow::INACTIVE)
    {
        flow->SetStatus(WrrFlow::NEW_FLOW);
        // std::cout << "INACTIVE flow with index " << flow->GetIndex() << " has been set to NEW_FLOW and added to the newFlows list" << std::endl;
        // flow->SetDeficit(m_quantum[flow->GetIndex()]);
        m_newFlows.push_back(flow);
    }


    bool retval = flow->GetQueueDisc()->Enqueue(item);

    if (ipHeader.GetSource()  == "10.1.0.1"){
        std::cout << "Error" << std::endl;
    }
    // Way to obtain the current pkts in the queue
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " Enqueue: Number packets band " << band << ": " << flow->GetQueueDisc()->GetNPackets() << " from: " << ipHeader.GetSource()); 
    
    // std::cout << Simulator::Now().GetSeconds() << " Enqueue: Number packets band " << band << ": " << flow->GetQueueDisc()->GetNPackets() << " from: " << ipHeader.GetSource() << std::endl;
    if (GetCurrentSize() > GetMaxSize())
    {
        NS_LOG_DEBUG("Overload; enter FqCobaltDrop ()");
        WrrDrop();
        assert (false && "wrong - pkts dropped!!");
    }
    return retval;
}

Ptr<QueueDiscItem>
WrrQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    Ptr<WrrFlow> flow;
    Ptr<QueueDiscItem> item;
    do
    {
        bool found = false;

        while (!found && !m_newFlows.empty())
        {
            flow = m_newFlows.front();

            if (flow->GetDeficit() <= 0)
            {
                NS_LOG_DEBUG("Increase deficit for new flow index " << flow->GetIndex());
                flow->IncreaseDeficit(m_quantum[flow->GetIndex()]);
                flow->SetStatus(WrrFlow::OLD_FLOW);
                m_oldFlows.push_back(flow);
                m_newFlows.pop_front();
                
            }
            else
            {
                NS_LOG_DEBUG("Found a new flow " << flow->GetIndex() << " with positive deficit");
                found = true;
            }
        }

        while (!found && !m_oldFlows.empty())
        {
            flow = m_oldFlows.front();
            if (flow->GetDeficit() <= 0)
            {
                NS_LOG_DEBUG("Increase deficit for old flow index " << flow->GetIndex());
                flow->IncreaseDeficit(m_quantum[flow->GetIndex()]);
                m_oldFlows.push_back(flow);
                m_oldFlows.pop_front();
            }
            else
            {  
                NS_LOG_DEBUG("Found an old flow " << flow->GetIndex() << " with positive deficit");
                found = true;
            }
        }

        if (!found)
        {
            NS_LOG_DEBUG("No flow found to dequeue a packet");
            return nullptr;
        }

        item = flow->GetQueueDisc()->Dequeue();

        if (!item)
        {
            NS_LOG_DEBUG("Could not get a packet from the selected flow queue");
            if (!m_newFlows.empty())
            {
                flow->SetStatus(WrrFlow::OLD_FLOW);
                m_oldFlows.push_back(flow);
                m_newFlows.pop_front();
            }
            else
            {
                flow->SetStatus(WrrFlow::INACTIVE);
                m_oldFlows.pop_front();
            }
        }
        else
        {   
            // std::cout << "Flow " << flow->GetIndex() << " has been select" << std::endl;
            NS_LOG_INFO(Simulator::Now().GetSeconds() << "  Dequeue: Flow " << flow->GetIndex() << " has been select" );
            NS_LOG_DEBUG("Dequeued packet " << item->GetPacket()->GetSize());
        }
    } while (!item);
    // Bufferlog << flow->GetIndex() << " " << item->GetSize() <<std::endl;
    flow->IncreaseDeficit(-1);
    // std::cout << "Flow " << flow->GetIndex() << " deficit after dequeue: " << flow->GetDeficit() << std::endl;
    return item;

}

bool
WrrQueueDisc::CheckConfig()
{   

    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("FqCobaltQueueDisc cannot have classes");
        return false;
    }


    NS_LOG_FUNCTION(this);
    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("WrrQueueDisc cannot have internal queues");
        return false;
    }

     // we are at initialization time. If the user has not set a quantum value,
    // set the quantum to the MTU of the device (if any)
    // for (int i = 0; i < (int)m_quantum.size(); i++){
    //     if (!m_quantum[i])
    //     {
    //         Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface();
    //         Ptr<NetDevice> dev;
    //         // if the NetDeviceQueueInterface object is aggregated to a
    //         // NetDevice, get the MTU of such NetDevice
    //         if (ndqi && (dev = ndqi->GetObject<NetDevice>()))
    //         {
    //             m_quantum[i] = 1;
    //             NS_LOG_DEBUG("Setting the quantum to the MTU of the device: " << m_quantum[i]);
    //         }

    //         if (!m_quantum[i])
    //         {
    //             NS_LOG_ERROR("The quantum parameter cannot be null");
    //             return false;
    //         }
    //     }

    // }




    return true;
}


uint32_t
WrrQueueDisc::WrrDrop()
{
    NS_LOG_FUNCTION(this);

    uint32_t maxBacklog = 0;
    uint32_t index = 0;
    Ptr<QueueDisc> qd;

    /* Queue is full! Find the fat flow and drop packet(s) from it */
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        qd = GetQueueDiscClass(i)->GetQueueDisc();
        uint32_t bytes = qd->GetNBytes();
        if (bytes > maxBacklog)
        {
            maxBacklog = bytes;
            index = i;
        }
    }

    /* Our goal is to drop half of this fat flow backlog */
    uint32_t len = 0;
    uint32_t count = 0;
    uint32_t threshold = maxBacklog >> 1;
    qd = GetQueueDiscClass(index)->GetQueueDisc();
    Ptr<QueueDiscItem> item;

    do
    {
        NS_LOG_DEBUG("Drop packet (overflow); count: " << count << " len: " << len
                                                       << " threshold: " << threshold);
        item = qd->GetInternalQueue(0)->Dequeue();
        DropAfterDequeue(item, OVERLIMIT_DROP);
        len += item->GetSize();
    } while (++count < m_dropBatchSize && len < threshold);

    return index;
}





void
WrrQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);

    m_flowFactory.SetTypeId("ns3::WrrFlow");
    m_queueDiscFactory.SetTypeId("ns3::FifoQueueDisc");


    for (uint32_t band = 0; band < nbands; band++)
    {
        if (m_flowsIndices.find(band) == m_flowsIndices.end())
        {
            Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc>();
            qd->Initialize();
            
            Ptr<WrrFlow> flow = m_flowFactory.Create<WrrFlow>();
            flow->SetIndex(band);
            flow->SetQueueDisc(qd);
            
            uint32_t quantum = 1; 
            if (band < m_quantum.size()) {
                quantum = m_quantum[band];
            }
            flow->IncreaseDeficit(quantum);
            
            AddQueueDiscClass(flow);
            m_flowsIndices[band] = GetNQueueDiscClasses() - 1;
            
            
        }
    }

    
   
    // m_queueDiscFactory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
    // m_queueDiscFactory.Set("MaxSize", QueueSizeValue(QueueSize("1p")));
    Bufferlog.open("./sim_results/sched-wrr-decision.log", std::fstream::out);
}



} // namespace ns3
