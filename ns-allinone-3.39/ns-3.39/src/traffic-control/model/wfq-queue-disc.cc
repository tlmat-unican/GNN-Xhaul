/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
 *
 *
 * Default Queueing, Dropping and Scheduling Configuration - Configure Catalyst 3750 QoS
 */

#include "wfq-queue-disc.h"


#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/pointer.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/queue.h"
#include "ns3/object-map.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wdrr-queue-disc.h"

#include <algorithm>
#include <iterator>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WfqQueueDisc");
NS_OBJECT_ENSURE_REGISTERED(WfqFlow);




TypeId
WfqFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WfqFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<WfqFlow>();
    return tid;
}

WfqFlow::WfqFlow()
    : m_deficit(0),
      m_status(INACTIVE),
      m_index(0)
{
    NS_LOG_FUNCTION(this);
}

WfqFlow::~WfqFlow()
{
    NS_LOG_FUNCTION(this);
}

void
WfqFlow::SetDeficit(uint32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit = deficit;
}

int32_t
WfqFlow::GetDeficit() const
{
    NS_LOG_FUNCTION(this);
    return m_deficit;
}

void
WfqFlow::IncreaseDeficit(int32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit += deficit;
}

void
WfqFlow::SetStatus(FlowStatus status)
{
    NS_LOG_FUNCTION(this);
    m_status = status;
}

WfqFlow::FlowStatus
WfqFlow::GetStatus() const
{
    NS_LOG_FUNCTION(this);
    return m_status;
}

void
WfqFlow::SetIndex(uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_index = index;
}

uint32_t
WfqFlow::GetIndex() const
{
    return m_index;
}

NS_OBJECT_ENSURE_REGISTERED(WfqQueueDisc);

TypeId
WfqQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::WfqQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<WfqQueueDisc>()
            .AddAttribute("MaxSize",
                          "The maximum number of packets accepted by this queue disc",
                          QueueSizeValue(QueueSize("1024p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("Flows",
                          "The number of queues into which the incoming packets are classified",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&WfqQueueDisc::m_flows),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Quantum",
                          "The quantum to band mapping.",
                          QuantumValue(Quantum{{10,90,5,5}}),
                          MakeQuantumAccessor(&WfqQueueDisc::m_quantum),
                          MakeQuantumChecker())
             .AddAttribute("DropBatchSize",
                          "The maximum number of packets dropped from the fat flow",
                          UintegerValue(64),
                          MakeUintegerAccessor(&WfqQueueDisc::m_dropBatchSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ChannelDataRate",
                        "The data rate of the channel",
                        DoubleValue(45),
                        MakeDoubleAccessor(&WfqQueueDisc::m_dataRate),
                        MakeDoubleChecker<double>())
            .AddAttribute("MapQueue",
                          "It can be used in order to map dscp marking with the queues",
                          MapQueueValue(MapQueue{{1, 2},{2, 4}}),
                          MakeMapQueueAccessor(&WfqQueueDisc::mapuca),
                          MakeMapQueueChecker());
    return tid;
}

WfqQueueDisc::WfqQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
      m_quantum(0)
{
    NS_LOG_FUNCTION(this);
}

WfqQueueDisc::~WfqQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
WfqQueueDisc::SetQuantum(uint32_t id, uint32_t quantum)
{
    NS_LOG_FUNCTION(this << quantum);
    m_quantum[id] = quantum;
}

uint32_t
WfqQueueDisc::GetQuantum(uint32_t id) const
{
    return m_quantum[id];
}



bool
WfqQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    uint32_t band = 0;
    item->SetTimeStamp(ns3::Simulator::Now());


    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();

    // Extract DSCP value from the IP header
    int dscp = ipHeader.GetDscp();


    // std::cout << dscp << std::endl;
    auto it = mapuca.find(dscp);
    
    // If the port is found in the map, assign the corresponding DSCP value
    if (it != mapuca.end()) {
        band = it->second;
        NS_LOG_INFO(band);
    }
    
    Ptr<WfqFlow> flow;
    if (m_flowsIndices.find(band) == m_flowsIndices.end())
    {       
        Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc>();
        qd->Initialize();
        flow = m_flowFactory.Create<WfqFlow>();
        NS_LOG_DEBUG("Creating a new flow queue with index");
        flow->SetIndex(band);
        flow->SetQueueDisc(qd);
        AddQueueDiscClass(flow);
        m_flowsIndices[band] =  GetNQueueDiscClasses() - 1;
    }
    else
    {
        flow = StaticCast<WfqFlow>(GetQueueDiscClass(m_flowsIndices[band]));
    }

    if (flow->GetStatus() == WfqFlow::INACTIVE)
    {
        flow->SetStatus(WfqFlow::NEW_FLOW);
        // capacity = (m_dataRate*(10^9)/8);
        flow->SetDeficit(m_quantum[flow->GetIndex()]);
        m_Flows.push_back(flow);
    }


    bool retval = flow->GetQueueDisc()->Enqueue(item);

    if (ipHeader.GetSource()  == "10.1.0.1"){
        std::cout << "Error" << std::endl;
    }
    // Way to obtain the current pkts in the queue
    // NS_LOG_INFO("--- Number packets band " << band << ": " << flow->GetQueueDisc()->GetNPackets() << " from: " << ipHeader.GetSource()); 

    
    if (GetCurrentSize() > GetMaxSize())
    {
        NS_LOG_DEBUG("Overload; enter FqCobaltDrop ()");
        WfqDrop();
    }
    return retval;
}



// Ptr<QueueDiscItem>
// WfqQueueDisc::DoDequeue()
// {
//     NS_LOG_FUNCTION(this);

//     Ptr<WfqFlow> flow;
//     Ptr<QueueDiscItem> item;
//     Ptr<const QueueDiscItem> pkt;
//     double bw;
   
//     Time minFinishTime = ns3::Time::Max();
//     do
//     {
//         bool found = false;

//         for (auto &currentFlow : m_Flows) {
//             if (currentFlow->GetQueueDisc()->GetNPackets() != 0) {
//                 pkt = currentFlow->GetQueueDisc()->Peek();
//                 bw = (double(currentFlow->GetDeficit()) / double(100)) * ((m_dataRate) * double(1e9 / 8));
//                 Time finishTime = std::max(pkt->GetTimeStamp(),queue_tail_time[currentFlow->GetIndex()]) + Seconds((double(pkt->GetSize()) / bw));
//                 // std::cout << "Checking flow: " << currentFlow->GetIndex() << " InitQueueArr " <<  pkt->GetTimeStamp() << "||"<< queue_tail_time[currentFlow->GetIndex()] << " " << " TxTime " << Seconds(((pkt->GetSize()) / bw)) << " finishTime: "<<finishTime <<std::endl;
//                 if (finishTime < minFinishTime) {
//                     minFinishTime = finishTime;
//                     flow = currentFlow; 
//                     found = true;
//                 }
//                 queue_tail_time[currentFlow->GetIndex()] = finishTime;
                
//             }
//         }


//         NS_LOG_DEBUG("Checking queues to send");
        
       

        
//         if (!found)
//         {
//             // std::cout << "No flow found to dequeue a packet" << std::endl;
//             NS_LOG_DEBUG("No flow found to dequeue a packet");
//             return nullptr;
//         }
        
//         item = flow->GetQueueDisc()->Dequeue();
       
//         if (!item)
//         {

//              NS_LOG_DEBUG("Could not get a packet from the selected flow queue");
//             if (!m_Flows.empty())
//             {
//                 flow->SetStatus(WfqFlow::OLD_FLOW);
//                 std::cout << "Old " << flow->GetIndex() << std::endl;
//             }
//             else
//             {
//                 std::cout << "Inactive " << flow->GetIndex() << std::endl;
//                 flow->SetStatus(WfqFlow::INACTIVE);
//             }
          
//         }
//         else
//         {   NS_LOG_INFO("Flow " << flow->GetIndex() << " has been select");
//             NS_LOG_DEBUG("Dequeued packet " << item->GetPacket()->GetSize());
//             // std::cout << "Flow " << flow->GetIndex() << std::endl;
//             // std::cout << "***Flow sent: " << flow->GetIndex() << " - Pkt Size: " << item->GetPacket()->GetSize() << std::endl;
//         }
//     } while (!item);
//     Bufferlog << flow->GetIndex() << " " << item->GetSize() <<std::endl;
   

//     return item;

// }


Ptr<QueueDiscItem>
WfqQueueDisc::DoDequeue()
{
    Ptr<WfqFlow> selectedFlow = nullptr;
    Time minFinishTime = Time::Max();

    
    for (auto &currentFlow : m_Flows) {
        if (currentFlow->GetQueueDisc()->GetNPackets() > 0) {
            Ptr<const QueueDiscItem> pkt = currentFlow->GetQueueDisc()->Peek();
            
            // 1. Calcular el ancho de banda asignado proporcional (Normalización)
            // Peso relativo = quantum_i / suma_quantums_activos
            double weight = static_cast<double>(currentFlow->GetDeficit());
            double bps_allocated = (weight / 100.0) * (m_dataRate * 1e9); 
            double bytes_per_sec = bps_allocated / 8.0;

            // 2. Cálculo del Finish Time virtual (F = S + L/w)
            // L = pkt->GetSize(), w = bytes_per_sec
            Time serviceTime = Seconds(static_cast<double>(pkt->GetSize()) / bytes_per_sec);
            
            // S (Start Time) es el máximo entre la llegada y el fin del anterior
            Time startTime = std::max(pkt->GetTimeStamp(), queue_tail_time[currentFlow->GetIndex()]);
            Time finishTime = startTime + serviceTime;

            if (finishTime < minFinishTime) {
                minFinishTime = finishTime;
                selectedFlow = currentFlow;
            }
        }
    }

    if (selectedFlow) {
        // Actualizar el historial de la cola para el próximo paquete
        queue_tail_time[selectedFlow->GetIndex()] = minFinishTime;
        return selectedFlow->GetQueueDisc()->Dequeue();
    }

    return nullptr;
}





bool
WfqQueueDisc::CheckConfig()
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
        NS_LOG_ERROR("WfqQueueDisc cannot have internal queues");
        return false;
    }

     // we are at initialization time. If the user has not set a quantum value,
    // set the quantum to the MTU of the device (if any)
    for (int i = 0; i < (int)m_quantum.size(); i++){
        if (!m_quantum[i])
        {
            Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface();
            Ptr<NetDevice> dev;
            // if the NetDeviceQueueInterface object is aggregated to a
            // NetDevice, get the MTU of such NetDevice
            if (ndqi && (dev = ndqi->GetObject<NetDevice>()))
            {
                m_quantum[i] = dev->GetMtu();
                NS_LOG_DEBUG("Setting the quantum to the MTU of the device: " << m_quantum[i]);
            }

            if (!m_quantum[i])
            {
                NS_LOG_ERROR("The quantum parameter cannot be null");
                return false;
            }
        }    
        queue_tail_time.push_back(Time(0));

    }




    return true;
}


uint32_t
WfqQueueDisc::WfqDrop()
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
WfqQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);

    m_flowFactory.SetTypeId("ns3::WfqFlow");
    m_queueDiscFactory.SetTypeId("ns3::FifoQueueDisc");
    // m_queueDiscFactory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
    Bufferlog.open("./sim_results/sched-wfq-decision.log", std::fstream::out);
}



} // namespace ns3
