#ifndef ORDER_BOOK_ENTRY
#define ORDER_BOOK_ENTRY
#include <stdint.h>
#include <cassert>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <map>
#define OUT(x) #x " : " << x << " "
#define DEBUG(x) x
#define THRESHOLD 1000

#include <x86intrin.h>
using namespace std;

//enum CommandType { ADDBUY = 0, ADDSELL, EXECBUY, EXECSELL, DELBUY, DELSELL };

struct TriggerData {
  uint64_t orderID_;
  uint32_t orderBookID_;
  uint32_t buyPrice_;
  uint32_t buyQuantity_;
  uint32_t sellPrice_;
  uint32_t sellQuantity_;

};

typedef void(*snapShotCallBack)(const unique_ptr<TriggerData, void(*)(TriggerData*)>);

void recycle(TriggerData* it) {
  cout << "Trigger " << it->orderID_ << " " << bswap_32(it->orderBookID_) << " " << it->buyPrice_ << " "
       << it->buyQuantity_ << " " << it->sellPrice_ << " " << it->sellQuantity_ << endl;
}

struct Order {
  uint32_t quantity_;
  uint32_t price_;
  Order(uint32_t q, uint32_t p) : quantity_(q), price_(p) {}
  Order() = default;
};


class OrderBook {
  unordered_map<uint64_t, Order> buyHash_;
  unordered_map<uint64_t, Order> sellHash_;
  map<uint32_t, uint32_t> quantityAtPrice_;
  map<uint32_t, uint32_t>::iterator bestBuy_;
  map<uint32_t, uint32_t>::iterator bestSell_;
  bool buyThresholdTriggered_{false};
  bool sellThresholdTriggered_{false};
protected:
  uint32_t id_;
  snapShotCallBack cb_;
  TriggerData triggerData_{};
  uint32_t numTrigger_{0};
  
public:
  
  OrderBook(uint32_t id, snapShotCallBack cb) : id_(id), cb_(cb)  {
    bestBuy_ = quantityAtPrice_.insert(make_pair(0, 0)).first;
    bestSell_ = quantityAtPrice_.insert(make_pair(0xFFFFFFFF, 0)).first;
  }
  
  virtual
  ~OrderBook() {
    cout << OUT(bswap_32(id_)) << OUT(numTrigger_) << __PRETTY_FUNCTION__ << endl;
    assert( buyHash_.empty() );
    assert( sellHash_.empty() );
    assert( quantityAtPrice_.size() == 2 );
    assert( bestBuy_->first == 0 );
    assert( bestBuy_->second == 0 );
    assert( bestSell_->first == 0xFFFFFFFF );
    assert( bestSell_->second == 0 );
  }
  
  virtual
  void
  trigger(uint64_t orderID, uint32_t buyPrice, uint32_t buyQuantity, uint32_t sellPrice, uint32_t sellQuantity) {
    ++numTrigger_;
    triggerData_.orderBookID_ = id_;
    triggerData_.orderID_ = orderID;
    triggerData_.buyPrice_ = buyPrice;
    triggerData_.buyQuantity_ = buyQuantity;
    triggerData_.sellPrice_ = sellPrice;
    triggerData_.sellQuantity_ = sellQuantity;
    cb_( unique_ptr<TriggerData, decltype(&recycle)>(&triggerData_, &recycle) );
  }
  
  void
  processAddBuy(uint64_t orderID, uint32_t quantity, uint32_t price, uint32_t orderBookPos) {
    DEBUG( cout << "ADDBUY " << OUT(orderID) << OUT(quantity) << OUT(price) << OUT(orderBookPos) << endl );

    if ( price > bestBuy_->first ) {
      assert( orderBookPos == 1);
      
      trigger(orderID, price, quantity, bestSell_->first, bestSell_->second);
      bestBuy_ = quantityAtPrice_.insert(bestSell_, make_pair(price, quantity));
      buyThresholdTriggered_ = false;
    } else    
      quantityAtPrice_[price] += quantity;
    
    bool inserted = buyHash_.insert(make_pair(orderID, Order(quantity,price))).second;
    assert(inserted);
  }
  
  void
  processAddSell(uint64_t orderID, uint32_t quantity, uint32_t price, uint32_t orderBookPos) {
    DEBUG( cout << "ADDSELL " << OUT(orderID) << OUT(quantity) << OUT(price) << OUT(orderBookPos) << endl );
    
    if ( price < bestSell_->first ) {
      assert( orderBookPos == 1);

      trigger(orderID, bestBuy_->first, bestBuy_->second, price, quantity);
      bestSell_ = quantityAtPrice_.insert(bestSell_, make_pair(price, quantity));
      sellThresholdTriggered_ = false;
    } else    
      quantityAtPrice_[price] += quantity;
    
    bool inserted = sellHash_.insert(make_pair(orderID, Order(quantity,price))).second;
    assert(inserted);
  }

  void processExecBuy( uint64_t orderID, uint32_t quantity) {
    DEBUG( cout << "EXECBUY " << OUT(orderID) << OUT(quantity) << endl );
    
#ifndef NDEBUG
    uint32_t bestBuyPrice = bestBuy_->first;
#endif
    assert( quantity <= bestBuy_->second );
    bestBuy_->second -= quantity;
    
    if ( bestBuy_->second == 0 ) {
      auto bit = bestBuy_--;
      trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
      quantityAtPrice_.erase(bit);
      buyThresholdTriggered_ = false;
    } else {
      if ( (bestBuy_->second <= THRESHOLD) && (buyThresholdTriggered_ == false) ) {
	//	cout << "THRESHOLD " << OUT(bestBuy_->second) << endl;
	trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
	buyThresholdTriggered_ = true;
      }
    }
    
    auto it = buyHash_.find(orderID);
    assert( it != buyHash_.end() );
    assert( bestBuyPrice == it->second.price_ ); // exec en yuksek buy fiyatindan
    assert(quantity <= it->second.quantity_);
    
    it->second.quantity_ -= quantity;
    if ( it->second.quantity_ == 0)
      buyHash_.erase(it);
  }
  
  void processExecSell( uint64_t orderID, uint32_t quantity) {
    DEBUG( cout << "EXECSELL " << OUT(orderID) << OUT(quantity) << endl );

#ifndef NDEBUG
    uint32_t bestSellPrice = bestSell_->first;
#endif
    assert( quantity <= bestSell_->second );
    bestSell_->second -= quantity;
    
    if ( bestSell_->second == 0 ) {
      auto bit = bestSell_++;
      trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
      quantityAtPrice_.erase(bit);
      sellThresholdTriggered_ = false;
    } else {
      if ( (bestSell_->second <= THRESHOLD) && (sellThresholdTriggered_ == false) ) {
	//	cout << "THRESHOLD " << OUT(bestSell_->second) << endl;
	trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
	sellThresholdTriggered_ = true;
      }
    }
    
    auto it = sellHash_.find(orderID);
    assert( it != sellHash_.end() );
    assert( bestSellPrice == it->second.price_ ); // exec en dusuk sell fiyatindan
    assert(quantity <= it->second.quantity_);
    
    it->second.quantity_ -= quantity;
    if ( it->second.quantity_ == 0)
      sellHash_.erase(it);
  }

  void processDelBuy(uint64_t orderID) {
    DEBUG( cout << "DELBUY " << OUT(orderID) << endl );
    auto it = buyHash_.find(orderID);
    assert( it != buyHash_.end() );
    
    auto pit = quantityAtPrice_.find(it->second.price_);
    assert( pit != quantityAtPrice_.end() );
    assert( it->second.quantity_ <= pit->second );
    pit->second -= it->second.quantity_;

    if ( pit == bestBuy_ ) {
      if ( bestBuy_->second == 0 ) {
	--bestBuy_;
	trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
	buyThresholdTriggered_ = false;
	quantityAtPrice_.erase(pit);
      } else {
	if ( (bestBuy_->second <= THRESHOLD) && (buyThresholdTriggered_ == false) ) {
	  //	  cout << "THRESHOLD " << OUT(bestBuy_->second) << endl;
	  trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
	  buyThresholdTriggered_ = true;
	}
      }
    } else
      if ( pit->second == 0 )
	quantityAtPrice_.erase(pit);

    buyHash_.erase(it);
  }
  
  void processDelSell(uint64_t orderID) {
    DEBUG( cout << "DELSELL " << OUT(orderID) << endl );
    auto it = sellHash_.find(orderID);
    assert( it != buyHash_.end() );
    
    auto pit = quantityAtPrice_.find(it->second.price_);
    assert( pit != quantityAtPrice_.end() );
    assert( it->second.quantity_ <= pit->second );
    pit->second -= it->second.quantity_;
    
    if ( pit == bestSell_ ) {
      if ( bestSell_->second == 0 ) {
	++bestSell_;
	trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
	sellThresholdTriggered_ = false;
	quantityAtPrice_.erase(pit);
      } else {
	if ( (bestSell_->second <= THRESHOLD) && (sellThresholdTriggered_ == false) ) {
	  //	  cout << "THRESHOLD " << bestSell_->second << endl;
	  trigger(orderID, bestBuy_->first, bestBuy_->second, bestSell_->first, bestSell_->second);
	  sellThresholdTriggered_ = true;
	}
      }
    } else
      if ( pit->second == 0 )
	quantityAtPrice_.erase(pit);

    sellHash_.erase(it);
  }
  virtual
  void setState(char* state) {
  }
};

class OrderBookTrigStats : public OrderBook {
  string state_;
  unordered_map<string, uint32_t> trigStats_;
  unordered_map<string, uint32_t>::iterator statIt_;
public:
  void setState(char* state) {
    state_ = "";
    for (unsigned i = 0; i != 20; ++i)
      state_ += state[i];
    cout << bswap_32(id_) << " " << state_ << endl;
    statIt_ = trigStats_.find(state_);
    if ( statIt_ == trigStats_.end() )
      statIt_ = trigStats_.insert(make_pair(state_, 0)).first;
  }

  OrderBookTrigStats(uint32_t id, snapShotCallBack cb) : OrderBook(id, cb) {}
  
  ~OrderBookTrigStats() {
    for ( auto it = trigStats_.begin(); it != trigStats_.end(); ++it )
      cout << it->first << " " << it->second << endl;
  }
  
  void
  trigger(uint64_t orderID, uint32_t buyPrice, uint32_t buyQuantity, uint32_t sellPrice, uint32_t sellQuantity) {
    OrderBook::trigger(orderID, buyPrice, buyQuantity, sellPrice, sellQuantity);
    ++statIt_->second;
  }

};

#endif
