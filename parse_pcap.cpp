#include <sys/stat.h>
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <cassert>
#include <byteswap.h>
//#include "order_book_stl.h"
#include "order_book_stl_thresh.h"
#include <vector>
#define OUT(x) #x " : " << x << " "

using namespace std;
vector<TriggerData> trigData;
void trigger(const unique_ptr<TriggerData, void(*)(TriggerData*)> snapshot) {
  trigData.push_back(*snapshot);
}

class ParsePCAP {
  const char* buffer_;
  char* const bufferEnd_;
  uint32_t prevSeconds_;
  uint32_t prevMicro_;
  uint32_t header_[16];
#ifndef NDEBUG
  const char* nextRecordPtr_;
#endif
  //  unordered_map<uint32_t, OrderBookTrigStats*> ob;
  unordered_map<uint32_t, OrderBook*> ob;
  bool buildOBList_;
  
  void parseHeader() {
    /*
    uint32_t* ptr = (uint32_t*)buffer_;
    cout << hex << "Magic Number " << *ptr++ << endl;
    cout << "Versions " << *ptr++ << endl;
    cout << "Reserved " << *ptr++ << endl;
    cout << "Reserved " << *ptr++ << endl;
    cout << "SnapLen " << *ptr++ << endl;
    cout << "LinkType " << *ptr++ << endl;
    */
    buffer_ += 24;
  }
  
  void parseRecord() {
    //        cout << dec << endl;
    uint32_t* ptr = (uint32_t*)buffer_;
    uint32_t seconds = *ptr++;
    uint32_t micro = *ptr++;
    assert ( prevSeconds_ <= seconds );
    assert( (prevSeconds_ < seconds) || ( prevMicro_ < micro ) );
    prevSeconds_ = seconds;
    prevMicro_ = micro;
    /*
    cout << "Timestamp secs " << seconds << endl;
    cout << "Timestamp micro " << micro << endl;
    */
    uint32_t packetLen = *(uint32_t*)(buffer_+8);

    /*    cout << "Cap Packet Len " << *ptr++ << endl;
	  cout << "Orig Packet Len " << *ptr++ << endl;*/
#ifndef NDEBUG
    nextRecordPtr_ = buffer_ + packetLen + 16;
#endif
    //    parseEthernet((uint8_t*)(buffer_ + 16));
    parseMoldUDP64((uint8_t*)(buffer_ + 16 + 14 + 20 + 8 ));
    buffer_ += packetLen + 16;
  }

  void parseEthernet(const uint8_t* ptr) {
    cout << hex;
    for (unsigned i = 0; i != 4; ++i) {
      header_[i] = bswap_32(*(uint32_t*)ptr);
      ptr += 4;
    }
    
    uint64_t destAddr = ((uint64_t)header_[0] << 16) + (header_[1] >> 16);
    uint64_t srcAddr = ((uint64_t)(header_[1] & 0xffff) << 32) + header_[2];
    uint32_t etherType = header_[3] >> 16;
    cout << "Ethernet\n" << OUT(destAddr) << OUT(srcAddr) << OUT(etherType) << endl;
    if ( etherType == 0x800 )
      parseIP(ptr-2);
  }

  void parseIP(const uint8_t* ptr) {
    cout << dec;
    for (unsigned i = 0; i != 3; ++i) {
      header_[i] = bswap_32(((uint32_t*)ptr)[i]);
    }
    assert( (header_[0] >> 28) == 4 ); //IPv4
    cout << "Version: " << (header_[0] >> 28) << endl;
    uint16_t ihl = (header_[0] >> 24) & 0xf;
    //    cout << "IHL: " << ((header_[0] >> 24) & 0xf) << endl;
    cout << "IHL: " << ihl << endl;
    cout << "Total Length : " << (header_[0] & 0xffff) << endl;
    in_addr addr = {((uint32_t*)ptr)[3]};
    cout << "Source IP : " << inet_ntoa( addr ) << endl ;
    addr = {((uint32_t*)ptr)[4]};
    cout << "Dest IP : " << inet_ntoa( addr ) << endl ;
    uint16_t protocol = (header_[2] >> 16) & 0xff;
    cout << OUT(protocol) << endl;
    if (protocol == 17)
      parseUDP( ptr + ihl * 4 );
  }
  void parseUDP(const uint8_t* ptr) {
    for (unsigned i = 0; i != 2; ++i) {
      header_[i] = bswap_32(((uint32_t*)ptr)[i]);
    }
    cout << "Source Port : " << (header_[0] >> 16) << "Destination Port : " << (header_[0] & 0xffff) << endl;
    parseMoldUDP64(ptr+8);
  }
  /*
  uint64_t be64toh_Pol(const uint8_t* ptr) {
    uint64_t retVal(0);
    for (unsigned i = 0; i !=8; ++i) {
      retVal <<= 8;
      retVal+= ptr[i];
    }
    return retVal;
  }
  */

  void parseMoldUDP64(const uint8_t* ptr) {
    uint64_t sequence = bswap_64(*(uint64_t*)(ptr+10)); //buraya sequence number check koyulacak.
    uint16_t count = bswap_16(*(uint16_t*)(ptr+18));
    /*
    cout << "MoldUDP64 " << OUT(sequence) << OUT(count) << endl;
    if ( count == 0) cout << "HEARTBEAT" << endl;
    if (count == 0xffff) cout << "END OF SESSION" << endl;
    */
    parseITCH(ptr + 20, count);
  }

  void parseITCH(const uint8_t* ptr, uint16_t count) {

    for (unsigned i = 0; i !=  count; ++i) {
#ifndef NDEBUG
      uint16_t messageLen = bswap_16(*(uint16_t*)(ptr));
#endif
      uint8_t command = ptr[2];
      ptr += 3;
      //      cout << OUT(i) << OUT(messageLen) << OUT(command) << endl;
      switch (command) {
      case 'P' :
	assert( messageLen == 50 );
	parseTradeMesg(ptr);
	ptr += 49;
	break;
      case 'A' :
	assert( messageLen == 37 );
	parseAddMesg(ptr);
	ptr += 36;
	break;
      case 'E' :
	assert( messageLen == 52 );
	parseExecuteMesg(ptr);
	ptr += 51;
	break;
      case 'D' :
	assert( messageLen == 18 );
	parseDeleteMesg(ptr);
	ptr += 17;
	break;
      case 'Z' :
	assert( messageLen == 53 );
	parseEPUMesg(ptr);
	ptr += 52;
	break;
      case 'T' :
	assert( messageLen == 5 );
	parseTimeMesg(ptr);
	ptr += 4;
	break;
      case 'O' :
	assert( messageLen == 29 );
	parseOBStateMesg(ptr);
	ptr += 28;
	break;
      default:
	cout << "Unimplemented" << endl;
	exit(0);
	break;
      }
    }
    assert(nextRecordPtr_ == (char*)ptr);
  }

  void parseTradeMesg(const uint8_t* ptr) {
    /*
    uint32_t timeStamp = bswap_32(*(uint32_t*)(ptr));
    uint64_t matchID = bswap_64(*(uint64_t*)(ptr+4));
    uint32_t comboGroupID = bswap_32(*(uint32_t*)(ptr+12));
    uint8_t side = ptr[16];
    uint64_t quantity = bswap_64(*(uint64_t*)(ptr+17));
    uint32_t orderBookID = bswap_32(*(uint32_t*)(ptr+25));
    uint32_t tradePrice = bswap_32(*(uint32_t*)(ptr+29));
    uint8_t printable = ptr[47];
    uint8_t cross = ptr[48];
    //    if ( orderBookID == 71536 ) {
      cout << "TRADE MESG" << endl;
      cout << OUT(timeStamp) << endl;
      cout << OUT(matchID) << endl;
      cout << OUT(comboGroupID) << endl;
      cout << OUT(side) << endl;
      cout << OUT(quantity) << endl;
      cout << OUT(orderBookID) << endl;
      cout << OUT(tradePrice) << endl;
      cout << OUT(printable) << endl;
      cout << OUT(cross) << endl;
      //    }
      */
  }

  void parseAddMesg(const uint8_t* ptr) {
    //    uint32_t timeStamp = bswap_32(*(uint32_t*)(ptr));
    uint64_t orderID = bswap_64(*(uint64_t*)(ptr+4));
    //    uint64_t orderID = *(uint64_t*)(ptr+4);
    uint32_t orderBookID = *(uint32_t*)(ptr+12);
    uint8_t side = ptr[16];
    uint32_t orderBookPos = bswap_32(*(uint32_t*)(ptr+17));
    uint64_t quantity = bswap_64(*(uint64_t*)(ptr+21));
    uint32_t price = bswap_32(*(uint32_t*)(ptr+29));
    //    uint16_t attrib = bswap_16(*(uint16_t*)(ptr+33));
    //    uint8_t lotType = ptr[35];
    unordered_map<uint32_t, OrderBook*>::iterator it;
    if ( buildOBList_ ) {
      it = ob.find(orderBookID);
      if ( it == ob.end() )
	//	it = ob.insert(make_pair(orderBookID, new OrderBook(orderBookID, trigger))).first;
	it = ob.insert(make_pair(orderBookID, new OrderBookTrigStats(orderBookID, trigger))).first;
    } else {
      it = ob.find(orderBookID);
      if ( it == ob.end() )
	return;
    }

    if ( side == 'B' ) {
      it->second->processAddBuy(orderID, quantity, price, orderBookPos);
    } else {
      it->second->processAddSell(orderID, quantity, price, orderBookPos);
    }
    
    /*	
    cout << OUT(timeStamp) << endl;
    cout << OUT(orderID) << endl;
    cout << OUT(orderBookID) << endl;
    cout << OUT(side) << endl;
    cout << OUT(orderBookPos) << endl;
    cout << OUT(quantity) << endl;
    cout << OUT(price) << endl;
    cout << OUT(attrib) << endl;
    cout << OUT(+lotType) << endl;
    */
  }

  void parseExecuteMesg(const uint8_t* ptr) {
    //    uint32_t timeStamp = bswap_32(*(uint32_t*)(ptr));
    uint64_t orderID = bswap_64(*(uint64_t*)(ptr+4));
    //    uint64_t orderID = *(uint64_t*)(ptr+4);
    uint32_t orderBookID = *(uint32_t*)(ptr+12);
    uint8_t side = ptr[16];
    uint64_t quantity = bswap_64(*(uint64_t*)(ptr+17));
    //    uint64_t matchID = bswap_64(*(uint64_t*)(ptr+25));
    //    uint32_t comboGroupID = bswap_32(*(uint32_t*)(ptr+12));

    auto it = ob.find(orderBookID);
    if ( it == ob.end() )
      return;
    if ( side == 'B' ) {
      it->second->processExecBuy(orderID, quantity);
    } else {
      it->second->processExecSell(orderID, quantity);
    }


    /*
    cout << OUT(timeStamp) << endl;
    cout << OUT(orderID) << endl;
    cout << OUT(orderBookID) << endl;
    cout << OUT(side) << endl;
    cout << OUT(quantity) << endl;
    cout << OUT(matchID) << endl;
    cout << OUT(comboGroupID) << endl;
    */
  }

  void parseDeleteMesg(const uint8_t* ptr) {
    //    uint32_t timeStamp = bswap_32(*(uint32_t*)(ptr));
    uint64_t orderID = bswap_64(*(uint64_t*)(ptr+4));
    //    uint64_t orderID = *(uint64_t*)(ptr+4);
    uint32_t orderBookID = *(uint32_t*)(ptr+12);
    uint8_t side = ptr[16];
    
    auto it = ob.find(orderBookID);
    if (it == ob.end() )
      return;
    if ( side == 'B' ) {
      it->second->processDelBuy(orderID);
    } else {
      it->second->processDelSell(orderID);
    }
    /*
    cout << OUT(timeStamp) << endl;
    cout << OUT(orderID) << endl;
    cout << OUT(orderBookID) << endl;
    cout << OUT(side) << endl;
    */
  }

  void parseEPUMesg(const uint8_t* ptr) {
  }

  void parseTimeMesg(const uint8_t* ptr) {
    /*
    uint32_t unixTime = bswap_32(*(uint32_t*)(ptr));
    cout << OUT(unixTime) << endl;
    */
  }

  void parseOBStateMesg(const uint8_t* ptr) {

    //    uint32_t timeStamp = bswap_32(*(uint32_t*)(ptr));
    //    uint32_t orderBookID = bswap_32(*(uint32_t*)(ptr+4));
    //    cout << OUT(timeStamp) << endl;
    //    cout << orderBookID << endl;
    /*
    for ( unsigned i = 0; i != 20; ++i)
      cout << ptr[i+8];
    cout << endl;
    */
    uint32_t orderBookID = *(uint32_t*)(ptr+4);
    auto it = ob.find(orderBookID);
    if (it == ob.end() )
      return;
    it->second->setState((char*)ptr+8);	
  }
  
  void parseOrderBookDirMesg(const uint8_t* ptr) {
  }
  
public:

  ParsePCAP(char* buffer, uint64_t size, char* orderBookList) : buffer_(buffer), bufferEnd_(buffer+size),
								prevSeconds_(0), prevMicro_(0) {
    if ( orderBookList ) {
      buildOBList_ = false;
      ifstream inFile(orderBookList);
      uint32_t orderBookID;
      uint32_t obID;
      cout << "Following Order Book ID's will be processed" << endl;

      //son deger cr/lf oldugunda iki defa okunuyor, duzelt
      while ( !inFile.eof() ) {
	inFile >> obID;
	cout << obID << endl;
	orderBookID = bswap_32(obID);
	//	ob.insert(make_pair(orderBookID, new OrderBook(orderBookID, trigger)));
	ob.insert(make_pair(orderBookID, new OrderBookTrigStats(orderBookID, trigger)));
      }
    } else {
      cout << "All Order Books will be processed" << endl;
      buildOBList_ = true;
    }
  }

  ~ParsePCAP() {
    //    cout << OUT(ob.size()) << endl;
    for (auto it = ob.begin(); it != ob.end(); ++it)
      delete it->second;
  }

  void parse() {
    parseHeader();
    while (buffer_ < bufferEnd_) {
      parseRecord();

    }
    assert( buffer_ == bufferEnd_ );
  }

  void setBuffer(char* buffer) {
    buffer_ = buffer;
  }
};

int main(int argc, char*argv[]) {
  struct stat results;
  if ( stat(argv[1], &results) != 0 ) {
    cout << "Problem opening " << argv[1] << endl;
    exit(0);
  }
  trigData.reserve(1'000'000);
  char* buffer = new char[results.st_size];
  ifstream pcapFile(argv[1], ios::in | ios::binary);
  auto startTime = clock();
  pcapFile.read(buffer, results.st_size);
  auto endTime = clock();
  assert( pcapFile.gcount() == results.st_size );
  cout << OUT(pcapFile.gcount()) << " " << OUT(endTime - startTime) << endl;
  
  char* fName = (argc == 2 ? nullptr : argv[2]);
  ParsePCAP pcap(buffer, results.st_size, fName);
  
  auto startCycle = __rdtsc();
  //  for (unsigned i = 0; i != 100; ++i) {
    pcap.parse();
    //    pcap.setBuffer(buffer);
    //  }
  auto endCycle = __rdtsc();
  cout << "Processing Cycles " << endCycle - startCycle << endl;
  /*
  for (auto it = trigData.begin(); it != trigData.end(); ++it)
    //    cout << "Trigger " << bswap_64(it->orderID_) << " " << it->quantity_ << " "
    cout << "Trigger " << it->orderID_ << " " << it->quantity_ << " "
    << it->price_ << endl;
  */
  delete[] buffer;
}
