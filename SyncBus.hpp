/*
 * SyncBus.hpp
 *
 *  Created on: Oct 16, 2024
 *  Author: Samuel Almeida Rocha
 *
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#ifndef PROTO_BUS_BUFFER_SIZE
#define PROTO_BUS_BUFFER_SIZE 64U
#endif

#ifndef SyncBus_ENABLE_SET_ACK
#define SyncBus_ENABLE_SET_ACK 1
#endif

namespace SyncBus
{

// ---- Frame layout ----------------------------------------------------------
// [0..3] ServerId (LE, 4 bytes)
// [4]    SlotId   (1 byte)
// [5]    Function (1 byte)
// [6..]  Data     (payload)
// [end]  CRC16 (Modbus, 2 bytes, LO then HI)
static constexpr uint8_t FrameServerId = 0U;
static constexpr uint8_t FrameSlotId = 4U;
static constexpr uint8_t FrameFunction = 5U;
static constexpr uint8_t FrameData = 6U;
static constexpr uint8_t HeaderSize = 6U; // 4 + 1 + 1

// ---- Function codes --------------------------------------------------------
enum class SyncBusFunc : uint8_t
{
  GetReq = 0U,
  SetReq = 1U,
  GetResp = 2U,
  SetResp = 3U,
};

enum class result
{
  ok,
  errOverflow,
  errCrc,
  errFault,
};

// ---- Callback types --------------------------------------------------------
using SyncBusSendData_cb = void (*)(const uint8_t* data, uint8_t size);
using SyncBusDataChanged_cb = void (*)(uint8_t slotId);

// ---- Slot records ----------------------------------------------------------
struct serverData_t
{
  void *data;     // pointer to application buffer
  uint32_t serverId;
  uint8_t slotId;
  uint8_t size;    // number of bytes in 'data'
};

struct clientSlot_t
{
  void *data;    // pointer to application buffer
  uint8_t slotId;
  uint8_t size;
};

// ---- Endianness helpers (LE) -----------------------------------------------
static inline void write_le32(uint8_t *dst, uint32_t v) noexcept
{
  dst[0] = static_cast<uint8_t>(v & 0xFFU);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFU);
  dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFU);
  dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFU);
}

static inline uint32_t read_le32(const uint8_t *src) noexcept
{
  return (static_cast<uint32_t>(src[0])) | (static_cast<uint32_t>(src[1]) << 8)
      | (static_cast<uint32_t>(src[2]) << 16)
      | (static_cast<uint32_t>(src[3]) << 24);
}

// ---- CRC16 (Modbus poly 0xA001), LO then HI appended -----------------------
static inline uint8_t genCRC16(uint8_t *buff, uint8_t len) noexcept
{
  uint16_t crc = 0xFFFFU;

  for (uint8_t pos = 0U; pos < len; ++pos)
  {
    crc ^= static_cast<uint16_t>(buff[pos]);
    for (uint8_t i = 0U; i < 8U; ++i)
    {
      const bool lsb = (crc & 0x0001U) != 0U;
      crc >>= 1;
      if (lsb)
      {
        crc ^= 0xA001U;
      }
    }
  }

  const uint8_t lo = static_cast<uint8_t>(crc & 0xFFU);
  const uint8_t hi = static_cast<uint8_t>((crc >> 8) & 0xFFU);

  buff[len++] = lo;
  buff[len++] = hi;
  return len;
}

static inline bool checkCRC16(const uint8_t *buff, uint8_t len) noexcept
{
  if (len < 2U)
  {
    return false;
  }

  uint16_t crc = 0xFFFFU;
  const uint8_t body = static_cast<uint8_t>(len - 2U);

  for (uint8_t pos = 0U; pos < body; ++pos)
  {
    crc ^= static_cast<uint16_t>(buff[pos]);
    for (uint8_t i = 0U; i < 8U; ++i)
    {
      const bool lsb = (crc & 0x0001U) != 0U;
      crc >>= 1;
      if (lsb)
      {
        crc ^= 0xA001U;
      }
    }
  }

  const uint8_t lo = static_cast<uint8_t>(crc & 0xFFU);
  const uint8_t hi = static_cast<uint8_t>((crc >> 8) & 0xFFU);
  return (buff[len - 2U] == lo) && (buff[len - 1U] == hi);
}

// ============================================================================
//                                CLIENT
// ============================================================================
template<uint8_t numSlots>
class SyncBusClient
{
public:
  explicit SyncBusClient(SyncBusSendData_cb SendData_cb,
      SyncBusDataChanged_cb DataChanged_cb = nullptr) noexcept :
      m_numSlots(0U), m_sendData_cb(SendData_cb), m_dataChanged_cb(
          DataChanged_cb)
  {
    // no-op
  }

  // GET request for a managed slot
  result getData(uint32_t serverId, uint8_t slot) noexcept
  {
    if (slot >= m_numSlots)
    {
      return result::errOverflow;
    }

    if (m_serveSlots[slot].data == nullptr)
    {
      return result::errFault;
    }

    // total = header + crc
    if ((static_cast<uint16_t>(HeaderSize) + 2U) > PROTO_BUS_BUFFER_SIZE)
    {
      return result::errOverflow;
    }

    write_le32(&m_buffer[FrameServerId], serverId);
    m_buffer[FrameSlotId] = m_serveSlots[slot].slotId;
    m_buffer[FrameFunction] = static_cast<uint8_t>(SyncBusFunc::GetReq);

    uint8_t size = genCRC16(m_buffer, HeaderSize);
    if (m_sendData_cb != nullptr)
    {
      m_sendData_cb(m_buffer, size);
    }
    return result::ok;
  }

  // SET request for a managed slot (sends current local data of that slot)
  result setData(uint32_t serverId, uint8_t slot) noexcept
  {
    if (slot >= m_numSlots)
    {
      return result::errOverflow;
    }
    if (m_serveSlots[slot].data == nullptr)
    {
      return result::errFault;
    }

    const uint8_t payload = m_serveSlots[slot].size;
    const uint16_t totalNoCrc = static_cast<uint16_t>(HeaderSize)
        + static_cast<uint16_t>(payload);

    if (totalNoCrc + 2U > PROTO_BUS_BUFFER_SIZE)
    {
      return result::errOverflow;
    }

    write_le32(&m_buffer[FrameServerId], serverId);
    m_buffer[FrameSlotId] = m_serveSlots[slot].slotId;
    m_buffer[FrameFunction] = static_cast<uint8_t>(SyncBusFunc::SetReq);

    std::memcpy(&m_buffer[FrameData], m_serveSlots[slot].data, payload);

    uint8_t size = genCRC16(m_buffer, static_cast<uint8_t>(totalNoCrc));
    if (m_sendData_cb != nullptr)
    {
      m_sendData_cb(m_buffer, size);
    }
    return result::ok;
  }

  // Incoming data (GetResp / SetResp)
  result inputData(const uint8_t *data, uint8_t size) noexcept
  {
    if (size < static_cast<uint8_t>(HeaderSize + 2U))
    {
      return result::errFault;
    }
    if (!checkCRC16(data, size))
    {
      return result::errCrc;
    }

    const uint32_t serverId = read_le32(&data[FrameServerId]);
    const auto function = static_cast<SyncBusFunc>(data[FrameFunction]);
    const uint8_t slotId = data[FrameSlotId];

    const uint8_t payloadLen = static_cast<uint8_t>(size - HeaderSize - 2U);

    if (function == SyncBusFunc::GetResp)
    {
      for (uint8_t i = 0U; i < m_numSlots; ++i)
      {
        if ((m_serveSlots[i].serverId == serverId)
            && (m_serveSlots[i].slotId == slotId))
        {

          if (payloadLen != m_serveSlots[i].size)
          {
            return result::errFault;
          }
          std::memcpy(m_serveSlots[i].data, &data[FrameData], payloadLen);

          if (m_dataChanged_cb != nullptr)
          {
            m_dataChanged_cb(slotId);
          }
          break;
        }
      }
    } else if (function == SyncBusFunc::SetResp)
    {
      // Optional: handle ACK, e.g., notify or update a status map
      // Currently no state kept → treated as success notification.
    }

    return result::ok;
  }

  // Register a (serverId, slotId, size, data*)
  result addData(void *data, uint32_t serverId, uint8_t slotId,
      uint8_t size) noexcept
  {
    if (data == nullptr)
    {
      return result::errFault;
    }
    if (m_numSlots >= numSlots)
    {
      return result::errOverflow;
    }

    // enforce maximum payload per frame
    if ((static_cast<uint16_t>(HeaderSize) + size + 2U) > PROTO_BUS_BUFFER_SIZE)
    {
      return result::errOverflow;
    }

    m_serveSlots[m_numSlots].data = data;
    m_serveSlots[m_numSlots].serverId = serverId;
    m_serveSlots[m_numSlots].slotId = slotId;
    m_serveSlots[m_numSlots].size = size;
    ++m_numSlots;

    return result::ok;
  }

private:
  uint8_t m_numSlots;
  serverData_t m_serveSlots[numSlots];
  SyncBusSendData_cb m_sendData_cb;
  SyncBusDataChanged_cb m_dataChanged_cb;
  uint8_t m_buffer[PROTO_BUS_BUFFER_SIZE];
};

// ============================================================================
//                                SERVER
// ============================================================================
template<uint8_t numSlots>
class SyncBusServer
{
public:
  explicit SyncBusServer(uint32_t id) noexcept :
      m_serverId(id), m_numSlots(0U), m_sendData_cb(nullptr), m_dataChanged_cb(
          nullptr)
  {
  }

  SyncBusServer(uint32_t id, SyncBusSendData_cb SendData_cb,
      SyncBusDataChanged_cb DataChanged_cb = nullptr) noexcept :
      m_serverId(id), m_numSlots(0U), m_sendData_cb(SendData_cb), m_dataChanged_cb(
          DataChanged_cb)
  {
  }

  void setId(uint32_t serverId) noexcept
  {
    m_serverId = serverId;
  }

  // Incoming data (GetReq / SetReq)
  result inputData(const uint8_t *data, uint8_t size) noexcept
  {
    if (size < static_cast<uint8_t>(HeaderSize + 2U))
    {
      return result::errFault;
    }
    if (!checkCRC16(data, size))
    {
      return result::errCrc;
    }

    const uint32_t serverId = read_le32(&data[FrameServerId]);
    if (serverId != m_serverId)
    {
      // Not for this server; ignore silently or return a benign code.
      return result::ok;
    }

    const uint8_t slotId = data[FrameSlotId];
    const auto function = static_cast<SyncBusFunc>(data[FrameFunction]);
    const uint8_t payloadLen = static_cast<uint8_t>(size - HeaderSize - 2U);

    if (function == SyncBusFunc::GetReq)
    {
      for (uint8_t i = 0U; i < m_numSlots; ++i)
      {
        if (m_clientSlots[i].slotId == slotId)
        {
          const uint8_t payload = m_clientSlots[i].size;
          const uint16_t totalNoCrc = static_cast<uint16_t>(HeaderSize)
              + payload;

          if (totalNoCrc + 2U > PROTO_BUS_BUFFER_SIZE)
          {
            return result::errOverflow;
          }

          write_le32(&m_buffer[FrameServerId], m_serverId);
          m_buffer[FrameSlotId] = slotId;
          m_buffer[FrameFunction] = static_cast<uint8_t>(SyncBusFunc::GetResp);
          std::memcpy(&m_buffer[FrameData], m_clientSlots[i].data, payload);

          uint8_t responseSize = genCRC16(m_buffer,
                                          static_cast<uint8_t>(totalNoCrc));
          if (m_sendData_cb != nullptr)
          {
            m_sendData_cb(m_buffer, responseSize);
          }
          break;
        }
      }
    } else if (function == SyncBusFunc::SetReq)
    {
      for (uint8_t i = 0U; i < m_numSlots; ++i)
      {
        if (m_clientSlots[i].slotId == slotId)
        {
          // Validate payload size
          if (payloadLen != m_clientSlots[i].size)
          {
            return result::errFault;
          }

          std::memcpy(m_clientSlots[i].data, &data[FrameData], payloadLen);
          if (m_dataChanged_cb != nullptr)
          {
            m_dataChanged_cb(slotId);
          }

#if SyncBus_ENABLE_SET_ACK
          // Send SetResp ACK (no payload)
          if ((static_cast<uint16_t>(HeaderSize) + 2U) > PROTO_BUS_BUFFER_SIZE)
          {
            return result::errOverflow;
          }
          write_le32(&m_buffer[FrameServerId], m_serverId);
          m_buffer[FrameSlotId] = slotId;
          m_buffer[FrameFunction] = static_cast<uint8_t>(SyncBusFunc::SetResp);

          uint8_t ackSize = genCRC16(m_buffer, HeaderSize);
          if (m_sendData_cb != nullptr)
          {
            m_sendData_cb(m_buffer, ackSize);
          }
#endif
          break;
        }
      }
    }

    return result::ok;
  }

  // Register a (slotId, size, data*)
  result addSlot(void *data, uint8_t slotId, uint8_t size) noexcept
  {
    if (data == nullptr)
    {
      return result::errFault;
    }
    if (m_numSlots >= numSlots)
    {
      return result::errOverflow;
    }

    // enforce maximum payload per frame for GET response
    if ((static_cast<uint16_t>(HeaderSize) + size + 2U) > PROTO_BUS_BUFFER_SIZE)
    {
      return result::errOverflow;
    }

    m_clientSlots[m_numSlots].data = data;
    m_clientSlots[m_numSlots].slotId = slotId;
    m_clientSlots[m_numSlots].size = size;
    ++m_numSlots;

    return result::ok;
  }

private:
  uint32_t m_serverId;
  uint8_t m_numSlots;
  clientSlot_t m_clientSlots[numSlots];
  SyncBusSendData_cb m_sendData_cb;
  SyncBusDataChanged_cb m_dataChanged_cb;
  uint8_t m_buffer[PROTO_BUS_BUFFER_SIZE];
};

} // namespace SyncBus
