#include "tcp-owd-tag.h"

#include "ns3/log.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(TcpOwdTag);

TcpOwdTag::TcpOwdTag()
    : m_txTimeNs(0), m_txSeq(0), m_payloadSize(0), m_isRetransmission(0) {}

TypeId TcpOwdTag::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::TcpOwdTag").SetParent<Tag>().AddConstructor<TcpOwdTag>();
  return tid;
}

TypeId TcpOwdTag::GetInstanceTypeId(void) const { return GetTypeId(); }

uint32_t TcpOwdTag::GetSerializedSize(void) const { return 8 + 4 + 4 + 1; }

void TcpOwdTag::Serialize(TagBuffer i) const {
  i.WriteU64(m_txTimeNs);
  i.WriteU32(m_txSeq);
  i.WriteU32(m_payloadSize);
  i.WriteU8(m_isRetransmission);
}

void TcpOwdTag::Deserialize(TagBuffer i) {
  m_txTimeNs = i.ReadU64();
  m_txSeq = i.ReadU32();
  m_payloadSize = i.ReadU32();
  m_isRetransmission = i.ReadU8();
}

void TcpOwdTag::Print(std::ostream &os) const {
  os << "txTimeNs=" << m_txTimeNs << ", txSeq=" << m_txSeq
     << ", payloadSize=" << m_payloadSize
     << ", retrans=" << static_cast<uint32_t>(m_isRetransmission);
}

void TcpOwdTag::SetTxTime(Time t) {
  m_txTimeNs = static_cast<uint64_t>(t.GetNanoSeconds());
}

Time TcpOwdTag::GetTxTime() const { return NanoSeconds(m_txTimeNs); }

void TcpOwdTag::SetTxSeq(SequenceNumber32 seq) { m_txSeq = seq.GetValue(); }

SequenceNumber32 TcpOwdTag::GetTxSeq() const {
  return SequenceNumber32(m_txSeq);
}

void TcpOwdTag::SetPayloadSize(uint32_t size) { m_payloadSize = size; }

uint32_t TcpOwdTag::GetPayloadSize() const { return m_payloadSize; }

void TcpOwdTag::SetRetransmission(bool isRetrans) {
  m_isRetransmission = isRetrans ? 1 : 0;
}

bool TcpOwdTag::IsRetransmission() const { return m_isRetransmission != 0; }

}  // namespace ns3