/*
 * Dumbbell Topology with QoS and TrafficControl
 * TX/RX tracing using Packet UID
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DumbbellQoS");

// ============================================
// Globals
// ============================================

std::string g_outputDir;

// For delay matching (optional)
struct PacketInfo
{
    double txTime;
    uint32_t size;
};

std::map<uint32_t, std::map<uint64_t, PacketInfo>> flowPackets;

// ============================================
// TX CALLBACK
// ============================================

void PacketTxCallback(uint32_t flowId, Ptr<const Packet> packet)
{
    uint64_t pktId = packet->GetUid();  // UNIQUE PACKET ID
    double txTime = Simulator::Now().GetSeconds();
    uint32_t pktSize = packet->GetSize();

    // Store for matching with RX (optional but recommended)
    flowPackets[flowId][pktId] = {txTime, pktSize};

    // Write TX file
    std::string txFileName =
        g_outputDir + "/txfileflow" + std::to_string(flowId) + ".txt";

    std::ofstream txFile(txFileName, std::ios::app);
    txFile << pktId << " "
           << std::fixed << std::setprecision(9)
           << txTime << " "
           << pktSize << "\n";
    txFile.close();
}

// ============================================
// RX CALLBACK
// ============================================

void PacketRxCallback(uint32_t flowId,
                      Ptr<const Packet> packet,
                      const Address &from)
{
    uint64_t pktId = packet->GetUid();  // SAME UID
    double rxTime = Simulator::Now().GetSeconds();
    uint32_t pktSize = packet->GetSize();

    // Write RX file
    std::string rxFileName =
        g_outputDir + "/rxfileflow" + std::to_string(flowId) + ".txt";

    std::ofstream rxFile(rxFileName, std::ios::app);
    rxFile << pktId << " "
           << std::fixed << std::setprecision(9)
           << rxTime << " "
           << pktSize << "\n";
    rxFile.close();
}

// ============================================
// MAIN
// ============================================

int main(int argc, char *argv[])
{
    double simulationTime = 60.0;
    g_outputDir = "./ns3-dumbbell-output";
    std::string scheduling = "WFQ";
    std::string queueweigths = "90 5 5";
    std::string mapqueue = "46 0 8 1 16 2";
    std::string markingportqueue = "5000 46 5001 8 5002 16 5003 16";
    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation time", simulationTime);
    cmd.AddValue("outputDir", "Output directory", g_outputDir);
    cmd.AddValue("scheduling", "FIFO | WFQ | SP", scheduling);
    cmd.Parse(argc, argv);

    system(("mkdir -p " + g_outputDir).c_str());

    // ============================================
    // Create nodes
    // ============================================

    NodeContainer nodes;
    nodes.Create(4);

    // ============================================
    // Links
    // ============================================

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("0ms"));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("0p"));

    NetDeviceContainer d01 = p2p.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer d12 = p2p.Install(nodes.Get(1), nodes.Get(2));
    NetDeviceContainer d23 = p2p.Install(nodes.Get(2), nodes.Get(3));

    // ============================================
    // Internet stack
    // ============================================

    InternetStackHelper stack;
    stack.Install(nodes);

    // ============================================
    // Traffic Control
    // ============================================

    TrafficControlHelper tch, tchmarker;
    tchmarker.SetRootQueueDisc("ns3::MarkerQueueDisc", "MarkingQueue", StringValue(markingportqueue));
    tchmarker.Install(d01.Get(0)); // bottleneck
    if (scheduling == "WFQ")
        tch.SetRootQueueDisc("ns3::WfqQueueDisc", "Quantum", StringValue(queueweigths), "MapQueue", StringValue(mapqueue));
    else if (scheduling == "SP")
        tch.SetRootQueueDisc("ns3::PrioQueueDisc");
    else
        tch.SetRootQueueDisc("ns3::FifoQueueDisc");

    tch.Install(d12.Get(0)); // bottleneck
    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer if01 = ipv4.Assign(d01);

    ipv4.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer if12 = ipv4.Assign(d12);

    ipv4.SetBase("10.0.3.0", "255.255.255.0");
    Ipv4InterfaceContainer if23 = ipv4.Assign(d23);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

   

    // ============================================
    // Applications
    // ============================================

   

    struct AppSpec
    {
        double lambda;
        uint32_t pktSize;
        uint8_t tos;
        uint32_t port;
    };

    AppSpec apps[] = {
        {200.0, 64, 0, 5000},     // URLLC
        {100.0, 1500, 1, 5001},   // eMBB
        {30.0, 1024, 3, 5002}     // Video
    };

    uint32_t numFlows = sizeof(apps) / sizeof(apps[0]);

    // Clear output files before starting
    for (uint32_t i = 1; i <= numFlows; ++i)
    {
        std::ofstream(g_outputDir + "/txfileflow" + std::to_string(i) + ".txt").close();
        std::ofstream(g_outputDir + "/rxfileflow" + std::to_string(i) + ".txt").close();
    }

    for (uint32_t i = 0; i < numFlows; i++)
    {
        double bps = apps[i].lambda * apps[i].pktSize * 8;

        OnOffHelper onoff("ns3::UdpSocketFactory",
                          InetSocketAddress(if23.GetAddress(1), apps[i].port));

        onoff.SetAttribute("PacketSize", UintegerValue(apps[i].pktSize));
        onoff.SetAttribute("DataRate", DataRateValue(DataRate(bps)));
        onoff.SetAttribute("Tos", UintegerValue(apps[i].tos));
        onoff.SetAttribute("OnTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer app = onoff.Install(nodes.Get(0));
        app.Start(Seconds(0.0));
        app.Stop(Seconds(simulationTime));

        Ptr<OnOffApplication> onoffApp =
            DynamicCast<OnOffApplication>(app.Get(0));

        if (onoffApp)
            onoffApp->TraceConnectWithoutContext(
                "Tx", MakeBoundCallback(&PacketTxCallback, i + 1));

        // Receiver
        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), apps[i].port));

        ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simulationTime + 1));

        Ptr<PacketSink> sinkPtr =
            DynamicCast<PacketSink>(sinkApp.Get(0));

        if (sinkPtr)
            sinkPtr->TraceConnectWithoutContext(
                "Rx", MakeBoundCallback(&PacketRxCallback, i + 1));

    
    }

    // ============================================
    // Run
    // ============================================

    Simulator::Stop(Seconds(simulationTime + 1));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "Simulation complete.\n";
    return 0;
}