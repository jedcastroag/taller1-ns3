#include "ns3/command-line.h"
#include "ns3/string.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/mobility-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/qos-txop.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/olsr-helper.h"
#include "ns3/csma-helper.h"
#include "ns3/animation-interface.h"

using namespace ns3;

//
// Define logging keyword for this file
//
NS_LOG_COMPONENT_DEFINE ("MixedWireless");

//
// This function will be used below as a trace sink, if the command-line
// argument or default value "useCourseChangeCallback" is set to true
//
static void
CourseChangeCallback (std::string path, Ptr<const MobilityModel> model)
{
  Vector position = model->GetPosition ();
  std::cout << "CourseChange " << path << " x=" << position.x << ", y=" << position.y << ", z=" << position.z << std::endl;
}

int
main (int argc, char *argv[])
{
  //
  // First, we declare and initialize a few local variables that control some
  // simulation parameters.
  //
  uint32_t backboneNodes = 10;
  uint32_t infraNodes = 2;
  uint32_t lanNodes = 2;
  uint32_t stopTime = 20;
  bool useCourseChangeCallback = false;
  double meanPacketsPerSecond = 10;
  uint32_t packetSize = 1000; // bytes

  //
  // Simulation defaults are typically set next, before command line
  // arguments are parsed.
  //
  Config::SetDefault ("ns3::OnOffApplication::PacketSize", StringValue ("1472"));
  Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue ("100kb/s"));

  //
  // For convenience, we add the local variables to the command line argument
  // system so that they can be overridden with flags such as
  // "--backboneNodes=20"
  //
  CommandLine cmd (__FILE__);
  cmd.AddValue ("backboneNodes", "number of backbone nodes", backboneNodes);
  cmd.AddValue ("infraNodes", "number of leaf nodes", infraNodes);
  cmd.AddValue ("lanNodes", "number of LAN nodes", lanNodes);
  cmd.AddValue ("stopTime", "simulation stop time (seconds)", stopTime);
  cmd.AddValue ("useCourseChangeCallback", "whether to enable course change tracing", useCourseChangeCallback);

  //
  // The system global variables and the local values added to the argument
  // system can be overridden by command line arguments by using this call.
  //
  cmd.Parse (argc, argv);

  if (stopTime < 10)
    {
      std::cout << "Use a simulation stop time >= 10 seconds" << std::endl;
      exit (1);
    }
  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Construct the backbone                                                //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  //
  // Create a container to manage the nodes of the adhoc (backbone) network.
  // Later we'll create the rest of the nodes we'll need.
  //
  NodeContainer backbone;
  backbone.Create (backboneNodes);
  //
  // Create the backbone wifi net devices and install them into the nodes in
  // our container
  //
  WifiHelper wifi;
  WifiMacHelper mac;
  mac.SetType ("ns3::AdhocWifiMac");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate54Mbps"));
  YansWifiPhyHelper wifiPhy;
  wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());
  NetDeviceContainer backboneDevices = wifi.Install (wifiPhy, mac, backbone);

  // We enable OLSR (which will be consulted at a higher priority than
  // the global routing) on the backbone ad hoc nodes
  NS_LOG_INFO ("Enabling OLSR routing on all backbone nodes");
  OlsrHelper olsr;
  //
  // Add the IPv4 protocol stack to the nodes in our container
  //
  InternetStackHelper internet;
  internet.SetRoutingHelper (olsr); // has effect on the next Install ()
  internet.Install (backbone);

  //
  // Assign IPv4 addresses to the device drivers (actually to the associated
  // IPv4 interfaces) we just created.
  //
  Ipv4AddressHelper ipAddrs;
  ipAddrs.SetBase ("192.168.0.0", "255.255.255.0");
  ipAddrs.Assign (backboneDevices);

  //
  // The ad-hoc network nodes need a mobility model so we aggregate one to
  // each of the nodes we just finished building.
  //
  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (20.0),
                                 "MinY", DoubleValue (20.0),
                                 "DeltaX", DoubleValue (20.0),
                                 "DeltaY", DoubleValue (20.0),
                                 "GridWidth", UintegerValue (5),
                                 "LayoutType", StringValue ("RowFirst"));
  mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
                             "Bounds", RectangleValue (Rectangle (-500, 500, -500, 500)),
                             "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=2]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.2]"));
  mobility.Install (backbone);

  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Construct the LANs                                                    //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  // Reset the address base-- all of the CSMA networks will be in
  // the "172.16 address space
  ipAddrs.SetBase ("172.16.0.0", "255.255.255.0");


  for (uint32_t i = 0; i < backboneNodes; ++i)
    {
      NS_LOG_INFO ("Configuring local area network for backbone node " << i);
      //
      // Create a container to manage the nodes of the LAN.  We need
      // two containers here; one with all of the new nodes, and one
      // with all of the nodes including new and existing nodes
      //
      NodeContainer stas;
      stas.Create (infraNodes - 1);
      // Now, create the container with all nodes on this link
      NodeContainer infra (backbone.Get (i), stas);
      //
      // Create an infrastructure network
      //
      WifiHelper wifiInfra;
      WifiMacHelper macInfra;
      macInfra.SetType ("ns3::AdhocWifiMac");
      wifiInfra.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                    "DataMode", StringValue ("OfdmRate54Mbps"));
      YansWifiPhyHelper wifiPhyInfra;
      wifiPhyInfra.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
      YansWifiChannelHelper wifiChannelInfra = YansWifiChannelHelper::Default ();
      wifiPhyInfra.SetChannel (wifiChannelInfra.Create ());
      NetDeviceContainer lanDevices = wifiInfra.Install (wifiPhyInfra, macInfra, stas);
  
      //
      // Add the IPv4 protocol stack to the new LAN nodes
      //
      internet.Install (stas);
      //
      // Assign IPv4 addresses to the device drivers (actually to the
      // associated IPv4 interfaces) we just created.
      //
      ipAddrs.Assign (lanDevices);
      //
      // Assign a new network prefix for the next LAN, according to the
      // network mask initialized above
      //
      ipAddrs.NewNetwork ();
      //
      // The new LAN nodes need a mobility model so we aggregate one
      // to each of the nodes we just finished building.
      //
      
      mobility.PushReferenceMobilityModel (backbone.Get (i));
      mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (20.0),
                                 "MinY", DoubleValue (20.0),
                                 "DeltaX", DoubleValue (20.0),
                                 "DeltaY", DoubleValue (20.0),
                                 "GridWidth", UintegerValue (5),
                                 "LayoutType", StringValue ("RowFirst"));
      mobility.SetMobilityModel ("ns3::RandomDirection2dMobilityModel",
                             "Bounds", RectangleValue (Rectangle (-500, 500, -500, 500)),
                             "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=2]"),
                             "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=0.2]"));
      mobility.Install (stas);
    }

  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Application configuration                                             //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  // Create the OnOff application to send UDP datagrams of size
  // 210 bytes at a rate of 10 Kb/s, between two nodes
  // We'll send data from the first wired LAN node on the first wired LAN
  // to the last wireless STA on the last infrastructure net, thereby
  // causing packets to traverse CSMA to adhoc to infrastructure links

  NS_LOG_INFO ("Create Applications.");
  uint16_t port = 9;   // Discard port (RFC 863)

  // Let's make sure that the user does not define too few nodes
  // to make this example work.  We need lanNodes > 1  and infraNodes > 1
  NS_ASSERT (lanNodes > 1 && infraNodes > 1);
  // We want the source to be the first node created outside of the backbone
  // Conveniently, the variable "backboneNodes" holds this node index value
  Ptr<Node> appSource = NodeList::GetNode (backboneNodes);
  // We want the sink to be the last node created in the topology.
  uint32_t lastNodeIndex = backboneNodes + /*backboneNodes * (lanNodes - 1) +*/ backboneNodes * (infraNodes - 1) - 1;
  Ptr<Node> appSink = NodeList::GetNode (lastNodeIndex);
  // Let's fetch the IP address of the last node, which is on Ipv4Interface 1
  Ipv4Address remoteAddr = appSink->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();

  Ptr<RandomVariableStream> interPacketIntervalStream = CreateObject<ExponentialRandomVariable>();
  interPacketIntervalStream->SetAttribute("Mean", DoubleValue(1.0 / meanPacketsPerSecond));

  OnOffHelper onoff ("ns3::UdpSocketFactory",
                     Address (InetSocketAddress (remoteAddr, port)));
  onoff.SetAttribute("OnTime", PointerValue(CreateObject<ConstantRandomVariable>()));
  onoff.SetAttribute("OffTime", PointerValue(interPacketIntervalStream));
  onoff.SetAttribute ("PacketSize", UintegerValue (packetSize));
  onoff.SetAttribute ("DataRate", StringValue ("50Mbps")); //bit/s


  ApplicationContainer apps = onoff.Install (appSource);
  apps.Start (Seconds (3));
  apps.Stop (Seconds (stopTime - 1));

  // Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::UdpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), port));
  apps = sink.Install (appSink);
  apps.Start (Seconds (3));
/*
  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Tracing configuration                                                 //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  NS_LOG_INFO ("Configure Tracing.");
  CsmaHelper csma;

  //
  // Let's set up some ns-2-like ascii traces, using another helper class
  //
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("mixed-wireless.tr");
  wifiPhy.EnableAsciiAll (stream);
  csma.EnableAsciiAll (stream);
  internet.EnableAsciiIpv4All (stream);

  // Csma captures in non-promiscuous mode
  csma.EnablePcapAll ("mixed-wireless", false);
  // pcap captures on the backbone wifi devices
  wifiPhy.EnablePcap ("mixed-wireless", backboneDevices, false);
  // pcap trace on the application data sink
  wifiPhy.EnablePcap ("mixed-wireless", appSink->GetId (), 0);
*/
  if (useCourseChangeCallback == true)
    {
      Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange", MakeCallback (&CourseChangeCallback));
    }

  AnimationInterface anim ("uno.xml");

  ///////////////////////////////////////////////////////////////////////////
  //                                                                       //
  // Run simulation                                                        //
  //                                                                       //
  ///////////////////////////////////////////////////////////////////////////

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Stop (Seconds (stopTime));
  Simulator::Run ();
  Simulator::Destroy ();
}
