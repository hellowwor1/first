#ifndef FLOW_TAG_H
#define FLOW_TAG_H

#include "ns3/nstime.h"
#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3 {

class FlowTag : public Tag {
 public:
  enum FlowType : uint8_t { AUDIO = 1, VIDEO = 2 };

  FlowTag() : m_type(0), m_txTime(Seconds(1)), m_rxTime(Seconds(0)) {}
  FlowTag(uint8_t type)
      : m_type(type), m_txTime(Seconds(1)), m_rxTime(Seconds(0)) {}

  static TypeId GetTypeId() {
    static TypeId tid =
        TypeId("ns3::FlowTag").SetParent<Tag>().AddConstructor<FlowTag>();
    return tid;
  }

  TypeId GetInstanceTypeId() const override { return GetTypeId(); }

  /* ===== 关键修正点 ===== */
  uint32_t GetSerializedSize() const override {
    return 1 + 8 + 8;  // flow type (1B) + txTime (8B) + rxTime (8B)
  }

  void Serialize(TagBuffer i) const override {
    i.WriteU8(m_type);
    i.WriteU64(m_txTime.GetNanoSeconds());
    i.WriteU64(m_rxTime.GetNanoSeconds());
  }

  void Deserialize(TagBuffer i) override {
    m_type = i.ReadU8();
    m_txTime = NanoSeconds(i.ReadU64());
    m_rxTime = NanoSeconds(i.ReadU64());
  }

  void Print(std::ostream &os) const override {
    os << "FlowType=" << uint32_t(m_type)
       << " TxTime=" << m_txTime.GetMilliSeconds() << "ms"
       << " RxTime=" << m_rxTime.GetMilliSeconds() << "ms";
  }

  void SetSeq(uint64_t s) { seq = s; }
  uint64_t GetSeq() { return seq; }

  void SetTxTime(Time time) { m_txTime = time; }
  void SetRxTime(Time time) { m_rxTime = time; }
  uint8_t GetFlowType() { return m_type; }
  Time GetTxTime() { return m_txTime; }
  Time GetRxTime() { return m_rxTime; }

 private:
  uint8_t m_type;
  Time m_txTime;
  Time m_rxTime;
  uint64_t seq;
};

}  // namespace ns3

#endif  // FLOW_TAG_H
