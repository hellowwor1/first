#ifndef TCP_OWD_TAG_H
#define TCP_OWD_TAG_H

#include "ns3/nstime.h"
#include "ns3/sequence-number.h"
#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3 {

class TcpOwdTag : public Tag {
 public:
  TcpOwdTag();

  static TypeId GetTypeId(void);
  virtual TypeId GetInstanceTypeId(void) const override;
  virtual uint32_t GetSerializedSize(void) const override;
  virtual void Serialize(TagBuffer i) const override;
  virtual void Deserialize(TagBuffer i) override;
  virtual void Print(std::ostream &os) const override;

  void SetTxTime(Time t);
  Time GetTxTime() const;

  void SetTxSeq(SequenceNumber32 seq);
  SequenceNumber32 GetTxSeq() const;

  void SetPayloadSize(uint32_t size);
  uint32_t GetPayloadSize() const;

  void SetRetransmission(bool isRetrans);
  bool IsRetransmission() const;

 private:
  uint64_t m_txTimeNs;
  uint32_t m_txSeq;
  uint32_t m_payloadSize;
  uint8_t m_isRetransmission;
};

}  // namespace ns3

#endif  // TCP_OWD_TAG_H