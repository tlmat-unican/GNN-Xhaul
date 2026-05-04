/*
 * Dumbbell Topology with QoS and TrafficControl for RouteNet-Fermi
 * 
 * Topology:
 *         (1Gbps, 1ms)      (100Mbps, 5ms)      (1Gbps, 1ms)
 * Node0 [Sender] --------- Node1 [Router] ---------- Node2 [Router] --------- Node3 [Receiver]
 * 
 * Applications:
 * - URLLC: Ultra Reliable Low Latency (64 bytes, 5ms inter-arrival)
 * - eMBB: Enhanced Mobile Broadband (1500 bytes, exponential)
 * - FH Sensing: Fronthaul Sensing (512 bytes, 10ms inter-arrival)
 * - Video Streaming: Variable bitrate (1024 bytes, ON/OFF)
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/seq-ts-header.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DumbbellQoS");


double GetPercentile(std::vector<double> v, double percentile) {
    if (v.empty()) {
        return 0.0;
    }

    // 1. Ordenar los datos de menor a mayor
    std::sort(v.begin(), v.end());

    // 2. Calcular el índice (Interpolación lineal simple)
    double index = percentile * (v.size() - 1);
    int lower_index = std::floor(index);
    int upper_index = std::ceil(index);

    if (lower_index == upper_index) {
        return v[lower_index];
    }

    // 3. Interpolación entre los dos valores más cercanos (opcional pero más preciso)
    double weight = index - lower_index;
    return v[lower_index] * (1.0 - weight) + v[upper_index] * weight;
}



// Estrutura para almacenar métricas por flujo
struct FlowMetrics
{
    uint64_t txBytes = 0;
    uint64_t rxBytes = 0;
    uint64_t txPackets = 0;
    uint64_t rxPackets = 0;
    uint64_t droppedPackets = 0;
    double avgDelay = 0.0;
    double jitter = 0.0;
    double avgBwKbps = 0.0;
    std::vector<double> delayPerPacket;
    double firstRxTime = -1.0;
    double lastRxTime = -1.0;
};

// Estructura para rastrear paquetes por flujo
struct PacketTrace
{
    uint32_t seq;
    double txTime;      // Timestamp de TX en segundos
    double rxTime;      // Timestamp de RX en segundos (-1 si no se recibió)
    uint32_t packetSize;    // Tamaño del paquete en bytes
};

// Variable global para almacenar métricas
std::map<uint32_t, FlowMetrics> flowMetricsMap;

// Mapa para rastrear paquetes TX/RX por flujo
std::map<uint32_t, std::map<uint32_t, PacketTrace>> flowPacketTraces;  // flowId -> seqNum -> PacketTrace


void
PacketTxCallback(uint32_t flowId, Ptr<const Packet> packet)
{
    // Crear una copia para extraer headers
    Ptr<Packet> copy = packet->Copy();
    SeqTsHeader seqTsHeader;
    
    // Intentar extraer el SeqTsHeader
    if (copy->RemoveHeader(seqTsHeader))
    {
        uint32_t seq = seqTsHeader.GetSeq();
        double txTime = Simulator::Now().GetFemtoSeconds();
        
        // Guardar el tx timestamp
        if (flowPacketTraces[flowId].find(seq) == flowPacketTraces[flowId].end())
        {
            flowPacketTraces[flowId][seq] = {seq, txTime, -1.0, packet->GetSize()};  // rxTime = -1 inicialmente
            flowMetricsMap[flowId].txPackets++;
            flowMetricsMap[flowId].txBytes += packet->GetSize();
        }
        
    }
    

}


void
PacketRxCallback(uint32_t flowId, Ptr<const Packet> packet, const Address& from)
{
    // Crear una copia para extraer headers
    Ptr<Packet> copy = packet->Copy();
    SeqTsHeader seqTsHeader;
    
    // Intentar extraer el SeqTsHeader
    if (copy->RemoveHeader(seqTsHeader))
    {
        uint32_t seq = seqTsHeader.GetSeq();
        Time txTime = seqTsHeader.GetTs();
        double rxTime = Simulator::Now().GetFemtoSeconds();
       
        
        // Guardar el rx timestamp
        if (flowPacketTraces[flowId].find(seq) == flowPacketTraces[flowId].end())
        {
            flowPacketTraces[flowId][seq].rxTime = rxTime;
            double delay = flowPacketTraces[flowId][seq].txTime >= 0 ? rxTime - flowPacketTraces[flowId][seq].txTime : -1.0;
            FlowMetrics& metrics = flowMetricsMap[flowId];
            metrics.delayPerPacket.push_back(delay);
            if (metrics.firstRxTime < 0.0)
            {
                metrics.firstRxTime = rxTime;
            }
            metrics.lastRxTime = rxTime;
        }
       

     

   
    }
    std::cout << "Packet received for flow " << flowId << " from " << from << " at time " << Simulator::Now().GetFemtoSeconds() << "s" << std::endl;
    flowMetricsMap[flowId].rxPackets++;
    flowMetricsMap[flowId].rxBytes += packet->GetSize();
}

int
main(int argc, char* argv[])
{
    double simulationTime = 60.0; // segundos
    std::string outputDir = "./ns3-dumbbell-output";
    std::string scheduling = "FIFO"; // FIFO, WFQ, SP, DRR
    bool verbose = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("outputDir", "Output directory for results", outputDir);
    cmd.AddValue("scheduling", "Scheduling policy: FIFO, WFQ, SP, DRR", scheduling);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("DumbbellQoS", LOG_LEVEL_ALL);
    }

    // Crear directorio de salida
    int ret = system(("mkdir -p " + outputDir).c_str());
    (void)ret; // Suprimir warning de unused variable

    NS_LOG_UNCOND("=== Dumbbell QoS Scenario ===");
    NS_LOG_UNCOND("Simulation Time: " << simulationTime << " seconds");
    NS_LOG_UNCOND("Scheduling Policy: " << scheduling);

    // ============================================
    // 1. CREAR NODOS
    // ============================================
    NodeContainer nodes;
    nodes.Create(4);
    NS_LOG_UNCOND("Created 4 nodes (0: Sender, 1: Router-L, 2: Router-R, 3: Receiver)");

    // ============================================
    // 2. CONFIGURAR ENLACES POINT-TO-POINT
    // ============================================
    PointToPointHelper p2p1, p2p2, p2p3;

    // Enlace 0->1: 1 Gbps
    p2p1.SetDeviceAttribute("DataRate", StringValue("2Gbps"));
    p2p1.SetChannelAttribute("Delay", StringValue("0ms"));
    p2p1.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("0p"));

    // Enlace 1->2: 1 Gbps
    p2p2.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p2.SetChannelAttribute("Delay", StringValue("0ms"));
    p2p2.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("0p"));

    // Enlace 2->3: 1 Gbps
    p2p3.SetDeviceAttribute("DataRate", StringValue("2Gbps"));
    p2p3.SetChannelAttribute("Delay", StringValue("0ms"));
    p2p3.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("0p"));

    // Instalar dispositivos
    NetDeviceContainer devices01 = p2p1.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer devices12 = p2p2.Install(nodes.Get(1), nodes.Get(2));
    NetDeviceContainer devices23 = p2p3.Install(nodes.Get(2), nodes.Get(3));

    NS_LOG_UNCOND("Installed point-to-point links");

    // ============================================
    // 3. INSTALAR STACK INTERNET
    // ============================================
    InternetStackHelper stack;
    stack.Install(nodes);

    // ============================================
    // 4. CONFIGURAR TRAFFIC CONTROL (QoS)
    // ============================================
    TrafficControlHelper tch;
    QueueDiscContainer qdisc;

    // Configurar política de scheduling en el enlace bottleneck (1->2)
    if (scheduling == "WFQ")
    {
        tch.SetRootQueueDisc("ns3::FqQueueDisc");
    }
    // else if (scheduling == "DRR")
    // {
    //     tch.SetRootQueueDisc("ns3::DrrQueueDisc");
    // }
    else if (scheduling == "SP")
    {
        // Usar PrioQueueDisc para strict priority
        tch.SetRootQueueDisc("ns3::PrioQueueDisc", "Priomap",
                             StringValue("0 1 2 3 0 1 2 3 0 1 2 3 0 1 2 3"));
    }
    else // FIFO (default)
    {
        tch.SetRootQueueDisc("ns3::FifoQueueDisc");
    }

    // Instalar QDisc en todos los dispositivos de salida
    qdisc.Add(tch.Install(devices01.Get(1))); // Node 1, salida hacia node 0
    qdisc.Add(tch.Install(devices12.Get(0))); // Node 1, salida hacia node 2 
    qdisc.Add(tch.Install(devices12.Get(1))); // Node 2, salida hacia node 1 
    qdisc.Add(tch.Install(devices23.Get(0))); // Node 2, salida hacia node 3

    NS_LOG_UNCOND("Installed " << scheduling << " queue discs");

    // ============================================
    // 5. ASIGNAR DIRECCIONES IP
    // ============================================
    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer if01 = ipv4.Assign(devices01);

    ipv4.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer if12 = ipv4.Assign(devices12);

    ipv4.SetBase("10.0.3.0", "255.255.255.0");
    Ipv4InterfaceContainer if23 = ipv4.Assign(devices23);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    NS_LOG_UNCOND("Assigned IPv4 addresses and routing tables");

    // ============================================
    // 6. CREAR APLICACIONES
    // ============================================

    // Puerto base
    uint16_t port = 5000;
    uint32_t flowId = 1;

    struct AppSpec {
        const char* name;
        uint8_t timeDist;   // 0=EXP, 1=DET, 4=ONOFF
        double lambda;      // Paquetes por segundo (media)
        uint8_t sizeDist;   // 0=DET, 1=UNI
        uint32_t pktSize;   // Tamaño base
        uint8_t tos;
    };

    AppSpec apps[] = {
        {"URLLC", 1, 200.0, 0, 64, 0},   // Determinista: 200 pps (5ms), 64B
        {"eMBB",  0, 100.0, 0, 1500, 1}, // Exponencial: 100 pps (10ms), 1500B
        {"Video", 4, 30.0,  1, 1024, 3}  // OnOff: 30 pps, Tamaño Variable
    };

    ApplicationContainer allApps;

    for (size_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++)
    {
        NS_LOG_UNCOND("Configurando flujo: " << apps[i].name);

        // Calculamos el DataRate en bps: lambda * tamaño * 8
        double bps = apps[i].lambda * apps[i].pktSize * 8;
        OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(if23.GetAddress(1), port));
        
        onoff.SetAttribute("PacketSize", UintegerValue(apps[i].pktSize));
        onoff.SetAttribute("DataRate", DataRateValue(DataRate(bps)));
        onoff.SetAttribute("Tos", UintegerValue(apps[i].tos));

        // --- CONFIGURACIÓN SEGÚN TIMEDIST ---
        if (apps[i].timeDist == 1) // DETERMINISTIC
        {
            onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        }
        else if (apps[i].timeDist == 0) // EXPONENTIAL
        {
            // En ns-3, OnOff con OnTime exponencial simula llegadas Poisson
            onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        }
        else if (apps[i].timeDist == 4) // ON/OFF ráfagas
        {
            onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
        }

        ApplicationContainer app = onoff.Install(nodes.Get(0));
        app.Start(Seconds(0 + (i * 0.001))); // Pequeño offset para evitar colisiones exactas
        app.Stop(Seconds(simulationTime));
        allApps.Add(app);
        
        // Conectar callback TX a OnOff application
        Ptr<OnOffApplication> onoffApp = DynamicCast<OnOffApplication>(app.Get(0));
        if (onoffApp)
        {
            onoffApp->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PacketTxCallback, i + 1));
        }

        // --- RECEIVER ---
        PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
        sinkApp.Start(Seconds(0));
        sinkApp.Stop(Seconds(simulationTime + 1));
        allApps.Add(sinkApp);
        
        // Conectar callback RX a PacketSink
        Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink>(sinkApp.Get(0));
        if (sinkPtr)
        {
            sinkPtr->TraceConnectWithoutContext("Rx", MakeBoundCallback(&PacketRxCallback, i + 1));
        }

        port++;
    }

    NS_LOG_UNCOND("Created 4 applications");

    // ============================================
    // 7. CORRER SIMULACIÓN
    // ============================================
    NS_LOG_UNCOND("Starting simulation...");
    Simulator::Stop(Seconds(simulationTime + 1.0));
    Simulator::Run();
    Simulator::Destroy();

    // ============================================
    // 8. PROCESAR RESULTADOS
    // ============================================
    NS_LOG_UNCOND("Processing results...");

    // Crear archivo de resultados
    std::string resultsFile = outputDir + "/simulationResults.txt";
    std::ofstream resultsStream(resultsFile.c_str());

    // Crear archivo de tráfico
    std::string trafficFile = outputDir + "/traffic.txt";
    std::ofstream trafficStream(trafficFile.c_str());

    // Crear archivo de estabilidad
    std::string stabilityFile = outputDir + "/stability.txt";
    std::ofstream stabilityStream(stabilityFile.c_str());

    // Crear archivo de entrada
    std::string inputFile = outputDir + "/input_files.txt";
    std::ofstream inputStream(inputFile.c_str());

    NS_LOG_UNCOND("=== Flow Statistics ===");

    double globalPackets = 0;
    double globalLosses = 0;
    double globalDelay = 0;
    std::vector<std::string> resultsPerFlow;
    std::vector<std::string> trafficPerFlow;

    uint32_t numFlows = static_cast<uint32_t>(sizeof(apps) / sizeof(apps[0]));
    for (uint32_t flowIndex = 0; flowIndex < numFlows; ++flowIndex)
    {
        uint32_t flowNum = flowIndex + 1;
        FlowMetrics& metrics = flowMetricsMap[flowNum];
        const std::vector<double>& delays = metrics.delayPerPacket;

        uint64_t txPackets = metrics.txPackets;
        uint64_t rxPackets = metrics.rxPackets;
        uint64_t rxBytes = metrics.rxBytes;
        uint64_t droppedPackets = (txPackets >= rxPackets) ? (txPackets - rxPackets) : 0;

        double avgDelay = 0.0;
        if (!delays.empty())
        {
            double sum = 0.0;
            for (double d : delays)
            {
                sum += d;
            }
            avgDelay = sum / static_cast<double>(delays.size());
        }

        double avgJitter = 0.0;
        if (delays.size() > 1)
        {
            double jitterSum = 0.0;
            for (size_t i = 1; i < delays.size(); ++i)
            {
                jitterSum += std::fabs(delays[i] - delays[i - 1]);
            }
            avgJitter = jitterSum / static_cast<double>(delays.size() - 1);
        }

        double avgBwKbps = 0.0;
        if (metrics.firstRxTime >= 0.0 && metrics.lastRxTime > metrics.firstRxTime)
        {
            avgBwKbps =
                (rxBytes * 8.0) /
                (metrics.lastRxTime - metrics.firstRxTime) /
                1000.0;
        }

        globalPackets += txPackets;
        globalLosses += droppedPackets;
        globalDelay += avgDelay * rxPackets;

        NS_LOG_UNCOND("Flow " << flowIndex << ": TxPkt=" << txPackets << " RxPkt=" << rxPackets
                              << " Drop=" << droppedPackets
                              << " AvgDelay=" << std::fixed << std::setprecision(3) << avgDelay
                              << "ms Jitter=" << avgJitter << "ms BW=" << std::fixed
                              << std::setprecision(3) << avgBwKbps << " kbps");

        double p10 = GetPercentile(delays, 0.10);
        double p20 = GetPercentile(delays, 0.20);
        double p50 = GetPercentile(delays, 0.50);
        double p80 = GetPercentile(delays, 0.80);
        double p90 = GetPercentile(delays, 0.90);

        // Formato: AvgBw_kbps,PktsGen,PktsDrop,AvgDelay,AvgLnDelay,p10,p20,p50,p80,p90,Jitter
        std::stringstream resultLine;
        resultLine << std::fixed << std::setprecision(3);
        resultLine << avgBwKbps << "," << txPackets << "," << droppedPackets << "," << avgDelay
                   << "," << (avgDelay > 0 ? std::log(avgDelay) : 0) << "," << p10 << "," << p20
                   << "," << p50 << "," << p80 << "," << p90 << "," << avgJitter;
        resultsPerFlow.push_back(resultLine.str());

        // Tráfico: TimeDist,TimeDistParams,SizeDist,SizeDistParams,ToS
        // Simplificado para dumbbell
        std::stringstream trafficLine;
        trafficLine << "1,1.0,0.2,0," << (flowIndex % 4); // DETERMINISTIC, DETERMINISTIC size
        trafficPerFlow.push_back(trafficLine.str());
    }

    // Escribir resultados globales y por par src-dst
    resultsStream << globalPackets << "," << globalLosses << ","
                  << (globalPackets > 0 ? globalDelay / globalPackets : 0) << "|";
    for (size_t i = 0; i < resultsPerFlow.size(); i++)
    {
        resultsStream << resultsPerFlow[i];
        if (i < resultsPerFlow.size() - 1)
            resultsStream << ";";
    }
    resultsStream << "\n";
    resultsStream.close();

    // ============================================
    // Generar archivos por flujo con delays
    // ============================================
    NS_LOG_UNCOND("Generating per-flow delay files...");
    
    uint32_t fileNum = 0;
    for (uint32_t flowIndex = 0; flowIndex < numFlows; ++flowIndex)
    {
        uint32_t flowNum = flowIndex + 1;
        std::string flowFileName = outputDir + "/flow_" + std::to_string(fileNum) + ".csv";
        std::ofstream flowStream(flowFileName.c_str());
        
        // Encabezado CSV
        flowStream << "pkt_id,tx_time_sec,rx_time_sec,delay_ms,pkt_size\n";
        
        // Obtener traces capturados para este flow
        if (flowPacketTraces.find(flowNum) != flowPacketTraces.end())
        {
            const auto& traces = flowPacketTraces[flowNum];
            uint32_t pktCount = 0;
            
            // Iterar sobre los paquetes rastreados
            for (const auto& trace : traces)
            {
                const PacketTrace& pkt = trace.second;
                double txTime = pkt.txTime;
                double rxTime = pkt.rxTime;
                double delayMs = -1.0;
                
                // Calcular delay solo si se recibió el paquete
                if (rxTime >= 0.0)
                {
                    delayMs = (rxTime - txTime) * 1000.0;  // Convertir a ms
                }
                
                flowStream << std::fixed << std::setprecision(6);
                flowStream << pktCount << "," 
                          << txTime << "," 
                          << rxTime << "," 
                          << delayMs << ","
                          << "-\n";  // pkt_size se puede obtener de otra forma si es necesario
                
                pktCount++;
            }
            
            NS_LOG_UNCOND("Written " << pktCount << " packets to " << flowFileName);
        }
        else
        {
            NS_LOG_UNCOND("No packet traces found for flow " << flowNum << ", writing empty file");
        }
        
        flowStream.close();
        
        fileNum++;
    }

    // Tráfico intensidad máxima y parámetros
    double maxIntensity = 1.0;
    trafficStream << maxIntensity << "|";
    for (size_t i = 0; i < trafficPerFlow.size(); i++)
    {
        trafficStream << trafficPerFlow[i];
        if (i < trafficPerFlow.size() - 1)
            trafficStream << ";";
    }
    trafficStream << "\n";
    trafficStream.close();

    // Estabilidad
    stabilityStream << simulationTime << ";OK;\n";
    stabilityStream.close();

    // Entrada
    inputStream << "scenario_001;network.gml;routing.txt\n";
    inputStream.close();

    // Crear directorio de topología
    ret = system(("mkdir -p " + outputDir + "/graphs").c_str());
    (void)ret;
    ret = system(("mkdir -p " + outputDir + "/routings").c_str());
    (void)ret;

    // Generar GML con topología dumbbell
    std::string gmlFile = outputDir + "/graphs/network.gml";
    std::ofstream gmlStream(gmlFile.c_str());
    gmlStream << "graph [\n";
    gmlStream << "  directed 1\n";
    for (int i = 0; i < 4; i++)
    {
        gmlStream << "  node [\n";
        gmlStream << "    id " << i << "\n";
        gmlStream << "    label \"Node" << i << "\"\n";
        gmlStream << "    schedulingPolicy \"FIFO\"\n";
        gmlStream << "    levelsQoS 1\n";
        gmlStream << "    queueSize 10000000\n";
        gmlStream << "  ]\n";
    }

    // Edges: 0->1, 1->2, 2->3, 1->0, 2->1, 3->2
    std::vector<std::tuple<int, int, uint64_t>> edges = {{0, 1, 1000000000},
                                                          {1, 0, 1000000000},
                                                          {1, 2, 100000000},
                                                          {2, 1, 100000000},
                                                          {2, 3, 1000000000},
                                                          {3, 2, 1000000000}};

    uint32_t edgeId = 0;
    for (auto& e : edges)
    {
        gmlStream << "  edge [\n";
        gmlStream << "    source " << std::get<0>(e) << "\n";
        gmlStream << "    target " << std::get<1>(e) << "\n";
        gmlStream << "    bandwidth " << std::get<2>(e) << "\n";
        gmlStream << "    port " << edgeId << "\n";
        gmlStream << "  ]\n";
        edgeId++;
    }
    gmlStream << "]\n";
    gmlStream.close();

    // Generar matriz de routing
    std::string routingFile = outputDir + "/routings/routing.txt";
    std::ofstream routingStream(routingFile.c_str());
    // Routing matrix para dumbbell: 4x4
    // Destination ports para cada nodo
    
    // Each row eis the SORUCE node. Each column is the DESTINATION node.
    // The value is the output port to use to reach that destination. -1 means no route (same node).

    // Nodo 0: Solo tiene el puerto 0 (conecta al Nodo 1)
    routingStream << "-1, 0, 0, 0\n";  // Para ir al 1, 2 o 3, sale por puerto 0
    // Nodo 1: Puerto 0 (hacia atrás al Nodo 0), Puerto 1 (hacia adelante al Nodo 2)
    routingStream << "0, -1, 1, 1\n";  // Al 0 por puerto 0; al 2 y 3 por puerto 1
    // Nodo 2: Puerto 0 (hacia atrás al Nodo 1), Puerto 1 (hacia adelante al Nodo 3)
    routingStream << "0, 0, -1, 1\n";  // Al 0 y 1 por puerto 0; al 3 por puerto 1
    // Nodo 3: Solo tiene el puerto 0 (conecta al Nodo 2)
    routingStream << "0, 0, 0, -1\n";  // Para ir al 0, 1 o 2, sale por puerto 0
    routingStream.close();

    NS_LOG_UNCOND("Results written to " << outputDir);
    NS_LOG_UNCOND("=== Simulation Complete ===");

    return 0;
}
