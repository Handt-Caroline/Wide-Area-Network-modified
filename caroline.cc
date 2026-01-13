/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Missing Persons Alert Network Simulation
 * ns-3.46+ compatible
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MissingPersonsAlertNetwork");

/* ================= Custom Application ================= */
class MissingPersonReport : public Application
{
public:
  MissingPersonReport();
  virtual ~MissingPersonReport();
  void Setup(Ptr<Socket> socket, Address peer,
             uint32_t packetSize, uint32_t nPackets,
             DataRate dataRate);

private:
  virtual void StartApplication();
  virtual void StopApplication();
  void SendPacket();
  void ScheduleTx(Time dt);

  Ptr<Socket> m_socket;
  Address m_peer;
  uint32_t m_packetSize;
  uint32_t m_nPackets;
  DataRate m_dataRate;
  EventId m_sendEvent;
  bool m_running;
  uint32_t m_packetsSent;
};

MissingPersonReport::MissingPersonReport()
  : m_socket(nullptr),
    m_packetSize(0),
    m_nPackets(0),
    m_dataRate(0),
    m_running(false),
    m_packetsSent(0)
{
}

MissingPersonReport::~MissingPersonReport()
{
  m_socket = nullptr;
}

void
MissingPersonReport::Setup(Ptr<Socket> socket, Address peer,
                           uint32_t packetSize, uint32_t nPackets,
                           DataRate dataRate)
{
  m_socket = socket;
  m_peer = peer;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MissingPersonReport::StartApplication()
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind();
  m_socket->Connect(m_peer);
  SendPacket();
}

void
MissingPersonReport::StopApplication()
{
  m_running = false;
  if (m_sendEvent.IsPending())
  {
    Simulator::Cancel(m_sendEvent);
  }
  if (m_socket)
  {
    m_socket->Close();
  }
}

void
MissingPersonReport::SendPacket()
{
  Ptr<Packet> packet = Create<Packet>(m_packetSize);
  m_socket->Send(packet);

  NS_LOG_INFO("Packet sent at "
              << Simulator::Now().GetSeconds()
              << "s | Size=" << m_packetSize << " bytes");

  if (++m_packetsSent < m_nPackets)
  {
    Time nextTx = Seconds(m_packetSize * 8.0 /
                          m_dataRate.GetBitRate());
    ScheduleTx(nextTx);
  }
}

void
MissingPersonReport::ScheduleTx(Time dt)
{
  if (m_running)
  {
    m_sendEvent = Simulator::Schedule(dt,
      &MissingPersonReport::SendPacket, this);
  }
}

/* ================= Receive Callbacks ================= */
static void
CloudRxCallback(Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom(from)))
  {
    NS_LOG_INFO("Cloud received report at "
      << Simulator::Now().GetSeconds()
      << "s | Size=" << packet->GetSize());
  }
}

static void
StationRxCallback(Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom(from)))
  {
    NS_LOG_INFO("Station received ALERT at "
      << Simulator::Now().GetSeconds()
      << "s | Size=" << packet->GetSize());
  }
}

/* ================= Main ================= */
int main(int argc, char *argv[])
{
  uint32_t nStations = 5;
  double simTime = 20.0;

  CommandLine cmd;
  cmd.AddValue("nStations", "Number of police stations", nStations);
  cmd.AddValue("simTime", "Simulation time", simTime);
  cmd.Parse(argc, argv);

  LogComponentEnable("MissingPersonsAlertNetwork", LOG_LEVEL_INFO);

  NodeContainer cloud;
  cloud.Create(1);

  NodeContainer stations;
  stations.Create(nStations);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));

  InternetStackHelper internet;
  internet.Install(cloud);
  internet.Install(stations);

  Ipv4AddressHelper address;
  std::vector<Ipv4InterfaceContainer> ifaces;

  for (uint32_t i = 0; i < nStations; ++i)
  {
    NetDeviceContainer dev =
      p2p.Install(stations.Get(i), cloud.Get(0));

    std::ostringstream subnet;
    subnet << "10.1." << i + 1 << ".0";
    address.SetBase(subnet.str().c_str(), "255.255.255.0");
    ifaces.push_back(address.Assign(dev));
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  uint16_t reportPort = 9000;
  Ptr<Socket> cloudRecv =
    Socket::CreateSocket(cloud.Get(0), UdpSocketFactory::GetTypeId());
  cloudRecv->Bind(InetSocketAddress(Ipv4Address::GetAny(), reportPort));
  cloudRecv->SetRecvCallback(MakeCallback(&CloudRxCallback));

  Ptr<Socket> reportSock =
    Socket::CreateSocket(stations.Get(0), UdpSocketFactory::GetTypeId());

  Ptr<MissingPersonReport> reportApp = CreateObject<MissingPersonReport>();
  reportApp->Setup(reportSock,
    InetSocketAddress(ifaces[0].GetAddress(1), reportPort),
    5120, 1, DataRate("10Mbps"));
  stations.Get(0)->AddApplication(reportApp);
  reportApp->SetStartTime(Seconds(2.0));
  reportApp->SetStopTime(Seconds(simTime));

  uint16_t alertPort = 9001;
  for (uint32_t i = 0; i < nStations; ++i)
  {
    Ptr<Socket> recv =
      Socket::CreateSocket(stations.Get(i), UdpSocketFactory::GetTypeId());
    recv->Bind(InetSocketAddress(Ipv4Address::GetAny(), alertPort));
    recv->SetRecvCallback(MakeCallback(&StationRxCallback));

    Ptr<Socket> send =
      Socket::CreateSocket(cloud.Get(0), UdpSocketFactory::GetTypeId());

    Ptr<MissingPersonReport> alertApp = CreateObject<MissingPersonReport>();
    alertApp->Setup(send,
      InetSocketAddress(ifaces[i].GetAddress(0), alertPort),
      2048, 1, DataRate("50Mbps"));
    cloud.Get(0)->AddApplication(alertApp);
    alertApp->SetStartTime(Seconds(2.1));
    alertApp->SetStopTime(Seconds(simTime));
  }

  FlowMonitorHelper flowmon;
  flowmon.InstallAll();

  p2p.EnablePcapAll("missing-persons-network");

  AnimationInterface anim("missing-persons-network.xml");
  anim.SetConstantPosition(cloud.Get(0), 50, 50);
  for (uint32_t i = 0; i < nStations; ++i)
  {
    anim.SetConstantPosition(stations.Get(i), 10, 10 + i * 15);
  }

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
