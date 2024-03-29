/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 University of Washington
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
 */

//
// This program configures a grid (default 5x5) of nodes on an
// 802.11b physical layer, with
// 802.11b NICs in adhoc mode, and by default, sends one packet of 1000
// (application) bytes to node 1.
//
// The default layout is like this, on a 2-D grid.
//
// n20  n21  n22  n23  n24
// n15  n16  n17  n18  n19
// n10  n11  n12  n13  n14
// n5   n6   n7   n8   n9
// n0   n1   n2   n3   n4
//
// the layout is affected by the parameters given to GridPositionAllocator;
// by default, GridWidth is 5 and numNodes is 25..
//
// There are a number of command-line options available to control
// the default behavior.  The list of available command-line options
// can be listed with the following command:
// ./waf --run "taller1 --help"
//
// Note that all ns-3 attributes (not just the ones exposed in the below
// script) can be changed at command line; see the ns-3 documentation.
//
// For instance, for this configuration, the physical layer will
// stop successfully receiving packets when distance increases beyond
// the default of 500m.
// To see this effect, try running:
//
// ./waf --run "taller1 --distance=500"
// ./waf --run "taller1 --distance=1000"
// ./waf --run "taller1 --distance=1500"
//
// The source node and sink node can be changed like this:
//
// ./waf --run "taller1 --sourceNode=20 --sinkNode=10"
//
// This script can also be helpful to put the Wifi layer into verbose
// logging mode; this command will turn on all wifi logging:
//
// ./waf --run "taller1 --verbose=1"
//
// By default, trace file writing is off-- to enable it, try:
// ./waf --run "taller1 --tracing=1"
//
// When you are done tracing, you will notice many pcap trace files
// in your directory.  If you have tcpdump installed, you can try this:
//
// tcpdump -r taller1-0-0.pcap -nn -tt
//

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/mobility-model.h"
#include "ns3/olsr-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/applications-module.h" // On-Off model
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiSimpleAdhocGrid");

void ReceivePacket(Ptr<Socket> socket)
{
  while (socket->Recv())
  {
    NS_LOG_UNCOND("Received one packet!");
  }
}

static void GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize,
                            uint32_t pktCount, Time pktInterval)
{
  if (pktCount > 0)
  {
    socket->Send(Create<Packet>(pktSize));
    Simulator::Schedule(pktInterval, &GenerateTraffic,
                        socket, pktSize, pktCount - 1, pktInterval);
  }
  else
  {
    socket->Close();
  }
}

int main(int argc, char *argv[])
{
  LogComponentEnable ( "OnOffApplication" , LOG_LEVEL_INFO) ;
  // LogComponentEnable ( "UdpApplication" , LOG_LEVEL_INFO) ;
  std::string phyMode("DsssRate1Mbps");
  double distance = 125;      // m
  uint32_t packetSize = 1000; // bytes
  uint32_t numPackets = 1;
  uint32_t numNodes = 25; // by default, 5x5
  uint32_t sinkNode = 0;
  uint32_t sourceNode = 24;
  double interval = 1.0; // seconds
  bool verbose = false;
  bool tracing = true;
  double meanPacketsPerSecond = 10; // Poisson arrival rate
  

  CommandLine cmd(__FILE__);
  cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
  cmd.AddValue("distance", "distance (m)", distance);
  cmd.AddValue("packetSize", "size of application packet sent", packetSize);
  cmd.AddValue("numPackets", "number of packets generated", numPackets);
  cmd.AddValue("interval", "interval (seconds) between packets", interval);
  cmd.AddValue("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue("tracing", "turn on ascii and pcap tracing", tracing);
  cmd.AddValue("numNodes", "number of nodes", numNodes);
  cmd.AddValue("sinkNode", "Receiver node number", sinkNode);
  cmd.AddValue("sourceNode", "Sender node number", sourceNode);
  cmd.Parse(argc, argv);
  // Convert to time object
  Time interPacketInterval = Seconds(interval);



  // Fix non-unicast data rate to be the same as that of unicast
  Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                     StringValue(phyMode));

  NodeContainer c;
  c.Create(numNodes);

  // The below set of helpers will help us to put together the wifi NICs we want
  WifiHelper wifi;
  if (verbose)
  {
    wifi.EnableLogComponents(); // Turn on all Wifi logging
  }

  YansWifiPhyHelper wifiPhy;
  // set it to zero; otherwise, gain will be added
  wifiPhy.Set("RxGain", DoubleValue(-10));
  // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
  wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
  wifiPhy.SetChannel(wifiChannel.Create());

  // Add an upper mac and disable rate control
  WifiMacHelper wifiMac;
  wifi.SetStandard(WIFI_STANDARD_80211b);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(phyMode),
                               "ControlMode", StringValue(phyMode));
  // Set it to adhoc mode
  wifiMac.SetType("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);

  ObjectFactory pos;
  pos.SetTypeId("ns3::RandomRectanglePositionAllocator");
  pos.Set ("X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  pos.Set ("Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  Ptr<PositionAllocator> posAlloc = pos.Create()->GetObject<PositionAllocator>();
  MobilityHelper mobility;
  mobility.SetPositionAllocator(posAlloc);
  mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel", 
                            "Speed", StringValue ("ns3::UniformRandomVariable[Min=0|Max=1]"),
                            "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"),
                            "PositionAllocator", PointerValue(posAlloc));
  mobility.Install(c);

  // Enable OLSR
  OlsrHelper olsr;
  Ipv4StaticRoutingHelper staticRouting;

  Ipv4ListRoutingHelper list;
  list.Add(staticRouting, 0);
  list.Add(olsr, 10);

  InternetStackHelper internet;
  internet.SetRoutingHelper(list); // has effect on the next Install ()
  internet.Install(c);

  Ipv4AddressHelper ipv4;
  NS_LOG_INFO("Assign IP Addresses.");
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i = ipv4.Assign(devices);

  std::string socketType = "ns3::UdpSocketFactory";
  TypeId tid = TypeId::LookupByName(socketType);
  
  Ptr<RandomVariableStream> interPacketIntervalStream = CreateObject<ExponentialRandomVariable>();
  interPacketIntervalStream->SetAttribute("Mean", DoubleValue(1.0 / meanPacketsPerSecond));
  // Ptr<Socket> recvSink = Socket::CreateSocket(c.Get(sinkNode), tid);
  OnOffHelper onoff (socketType, Ipv4Address::GetAny ());
  onoff.SetAttribute("OnTime", PointerValue(CreateObject<ConstantRandomVariable>()));
  onoff.SetAttribute("OffTime", PointerValue(interPacketIntervalStream));
  onoff.SetAttribute ("PacketSize", UintegerValue (packetSize));
  onoff.SetAttribute ("DataRate", StringValue ("50Mbps")); //bit/s

  InetSocketAddress rmt (InetSocketAddress(Ipv4Address::GetAny(), 80));
  AddressValue remoteAddress (rmt);
  Address LocalAddress (rmt);
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", LocalAddress);
  ApplicationContainer recvapp = packetSinkHelper.Install(c.Get(1));
  recvapp.Start (Seconds (1.0));
  recvapp.Stop (Seconds (10));


  ApplicationContainer apps;
  onoff.SetAttribute ("Remote", remoteAddress);
  apps.Add(onoff.Install(c.Get(sourceNode)));
  apps.Start (Seconds (2.0));
  apps.Stop (Seconds (10));

  if (tracing == true)
  {
    AsciiTraceHelper ascii;
    wifiPhy.EnableAsciiAll(ascii.CreateFileStream("taller1.tr"));
    wifiPhy.EnablePcap("taller1", devices);
    // Trace routing tables
    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>("taller1.routes", std::ios::out);
    olsr.PrintRoutingTableAllEvery(Seconds(2), routingStream);
    Ptr<OutputStreamWrapper> neighborStream = Create<OutputStreamWrapper>("taller1.neighbors", std::ios::out);
    olsr.PrintNeighborCacheAllEvery(Seconds(2), neighborStream);

    MobilityHelper::EnableAsciiAll (ascii.CreateFileStream ("taller1.mob"));

    // To do-- enable an IP-level trace that shows forwarding events only
  }
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();

  // Output what we are doing
  NS_LOG_UNCOND("Testing from node " << sourceNode << " to " << sinkNode << " with grid distance " << distance);

  // Netamin
  AnimationInterface anim("taller1.xml");

  for (size_t i = 0; i < c.GetN(); i++)
  {
    int col = i % 5, row = i / 5;
    anim.SetConstantPosition(c.Get(i), distance * col, distance * row);
  }


  Simulator::Stop(Seconds(33.0));
  Simulator::Run();
  flowMonitor->SerializeToXmlFile("third.xml", true, true);
  Simulator::Destroy();

  return 0;
}
