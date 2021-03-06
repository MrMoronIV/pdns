/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef DNSPACKET_HH

#define DNSPACKET_HH

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include "iputils.hh"
#include "ednssubnet.hh"

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <vector>
#include "qtype.hh"
#include "dns.hh"
#include "misc.hh"
#include "utility.hh"
#include "logger.hh"
#include "pdnsexception.hh"
#include "dnsrecords.hh"

class UeberBackend;
class DNSSECKeeper;


//! This class represents DNS packets, either received or to be sent.
class DNSPacket
{
public:
  DNSPacket(bool isQuery);
  DNSPacket(const DNSPacket &orig);

  int noparse(const char *mesg, size_t len); //!< just suck the data inward
  int parse(const char *mesg, size_t len); //!< parse a raw UDP or TCP packet and suck the data inward
  const string& getString(); //!< for serialization - just passes the whole packet

  // address & socket manipulation
  void setRemote(const ComboAddress*);
  ComboAddress getRemote() const;
  Netmask getRealRemote() const;
  ComboAddress getLocal() const
  {
    ComboAddress ca;
    socklen_t len=sizeof(ca);
    getsockname(d_socket, (sockaddr*)&ca, &len);
    return ca;
  }
  uint16_t getRemotePort() const;

  boost::optional<ComboAddress> d_anyLocal;

  Utility::sock_t getSocket() const
  {
    return d_socket;
  }
  void setSocket(Utility::sock_t sock);


  // these manipulate 'd'
  void setA(bool); //!< make this packet authoritative - manipulates 'd'
  void setID(uint16_t); //!< set the DNS id of this packet - manipulates 'd'
  void setRA(bool); //!< set the Recursion Available flag - manipulates 'd'
  void setRD(bool); //!< set the Recursion Desired flag - manipulates 'd'
  void setAnswer(bool); //!< Make this packet an answer - clears the 'stringbuffer' first, if passed 'true', does nothing otherwise, manipulates 'd'

  void setOpcode(uint16_t);  //!< set the Opcode of this packet - manipulates 'd'
  void setRcode(int v); //!< set the Rcode of this packet - manipulates 'd'

  void clearRecords(); //!< when building a packet, wipe all previously added records (clears 'rrs')

  /** Add a DNSZoneRecord to this packet. A DNSPacket (as does a DNS Packet) has 4 kinds of resource records. Questions, 
      Answers, Authority and Additional. See RFC 1034 and 1035 for details. You can specify where a record needs to go in the
      DNSZoneRecord d_place field */
  void addRecord(const DNSZoneRecord &);  // adds to 'rrs'

  void setQuestion(int op, const DNSName &qdomain, int qtype);  // wipes 'd', sets a random id, creates start of packet (domain, type, class etc)

  DTime d_dt; //!< the time this packet was created. replyPacket() copies this in for you, so d_dt becomes the time spent processing the question+answer
  void wrapup();  // writes out queued rrs, and generates the binary packet. also shuffles. also rectifies dnsheader 'd', and copies it to the stringbuffer
  void spoofQuestion(const DNSPacket *qd); //!< paste in the exact right case of the question. Useful for PacketCache
  unsigned int getMinTTL(); //!< returns lowest TTL of any record in the packet
  bool isEmpty(); //!< returns true if there are no rrs in the packet

  vector<DNSZoneRecord*> getAPRecords(); //!< get a vector with DNSZoneRecords that need additional processing
  vector<DNSZoneRecord*> getAnswerRecords(); //!< get a vector with DNSZoneRecords that are answers
  void setCompress(bool compress);

  DNSPacket *replyPacket() const; //!< convenience function that creates a virgin answer packet to this question

  void commitD(); //!< copies 'd' into the stringbuffer
  unsigned int getMaxReplyLen(); //!< retrieve the maximum length of the packet we should send in response
  void setMaxReplyLen(int bytes); //!< set the max reply len (used when retrieving from the packet cache, and this changed)

  bool couldBeCached(); //!< returns 0 if this query should bypass the packet cache
  bool hasEDNSSubnet() const;
  bool hasEDNS();
  uint8_t getEDNSVersion() const { return d_ednsversion; };
  void setEDNSRcode(uint16_t extRCode)
  {
    // WARNING: this is really 12 bits
    d_ednsrcode=extRCode;
  };
  uint8_t getEDNSRCode() const { return d_ednsrcode; };
  uint32_t getHash() const { return d_hash; };
  void setHash(uint32_t hash) { d_hash = hash; };

  //////// DATA !

  DNSName qdomain;  //!< qname of the question 4 - unsure how this is used
  DNSName qdomainwild;  //!< wildcard matched by qname, used by LuaPolicyEngine
  DNSName qdomainzone;  //!< zone name for the answer (as reflected in SOA for negative responses), used by LuaPolicyEngine
  string d_peer_principal;
  const DNSName& getTSIGKeyname() const;

  uint16_t qclass;  //!< class of the question - should always be INternet 2
  struct dnsheader d; //!< dnsheader at the start of the databuffer 12

  QType qtype;  //!< type of the question 2

  TSIGRecordContent d_trc; //72

  ComboAddress d_remote; //28
  TSIGHashEnum d_tsig_algo; //4

  bool d_tcp;
  bool d_dnssecOk;
  bool d_havetsig;

  bool getTSIGDetails(TSIGRecordContent* tr, DNSName* keyname, uint16_t* tsigPos=nullptr) const;
  void setTSIGDetails(const TSIGRecordContent& tr, const DNSName& keyname, const string& secret, const string& previous, bool timersonly=false);
  bool getTKEYRecord(TKEYRecordContent* tr, DNSName* keyname) const;

  vector<DNSZoneRecord>& getRRS() { return d_rrs; }
  bool checkForCorrectTSIG(UeberBackend* B, DNSName* keyname, string* secret, TSIGRecordContent* trc) const;

  static bool s_doEDNSSubnetProcessing;
  static uint16_t s_udpTruncationThreshold; 
  int d_ednsRawPacketSizeLimit; // only used for Lua record
private:
  void pasteQ(const char *question, int length); //!< set the question of this packet, useful for crafting replies

  bool d_wrapped; // 1
  int d_socket; // 4

  string d_tsigsecret;
  DNSName d_tsigkeyname;
  string d_tsigprevious;

  vector<DNSZoneRecord> d_rrs; // 8
  string d_rawpacket; // this is where everything lives 8
  EDNSSubnetOpts d_eso;

  int d_maxreplylen;

  uint8_t d_ednsversion;
  // WARNING! This is really 12 bits
  uint16_t d_ednsrcode;

  uint32_t d_hash{0};
  
  bool d_compress; // 1
  bool d_tsigtimersonly;
  bool d_wantsnsid;
  bool d_haveednssubnet;
  bool d_haveednssection;
  bool d_isQuery;
};

#endif
