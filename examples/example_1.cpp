
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <array>
#include "SyncBus.hpp"

using namespace SyncBus;

// -------------------- Tipos de dados usados nos slots -----------------------
struct DeviceStats {
    uint32_t uptime_s;
    float    temperature_c;
    uint16_t error_count;
};

constexpr uint8_t kNameMax = 16;
struct NameSlot {
    char data[kNameMax]; // string ASCII, zero-terminated se couber
};
static_assert(sizeof(NameSlot) == kNameMax, "NameSlot size changed");

constexpr uint8_t kBlobSize = 8;
struct BlobSlot {
    uint8_t bytes[kBlobSize];
};
static_assert(sizeof(BlobSlot) == kBlobSize, "BlobSlot size changed");

// -------------------- util: hexdump bonito ----------------------------------
static std::string toHex(const uint8_t* d, uint8_t n)
{
    std::ostringstream oss;
    for (uint8_t i = 0; i < n; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(d[i]);
        if (i != n - 1) oss << ' ';
    }
    return oss.str();
}

// -------------------- forwarding entre cliente e servidor -------------------
void clientSend(const uint8_t* data, uint8_t size);
void serverSend(const uint8_t* data, uint8_t size);

constexpr uint32_t kServerId = 0x12345678;

enum : uint8_t {
    SLOT_U8    = 1,
    SLOT_STATS = 2,
    SLOT_NAME  = 3,
    SLOT_BLOB  = 4,
};

SyncBusClient<4> g_client(
    clientSend,
    [](uint8_t slotId){ std::cout << "[CLIENT] Data changed at slot " << int(slotId) << "\n"; }
);

SyncBusServer<4> g_server(
    kServerId,
    serverSend,
    [](uint8_t slotId){ std::cout << "[SERVER] Data changed at slot " << int(slotId) << "\n"; }
);

// -------------------- Buffers reais -----------------------------------------
uint8_t     g_cli_u8     = 0;
DeviceStats g_cli_stats  = {0, 0.0f, 0};
NameSlot    g_cli_name   = { "?" };
BlobSlot    g_cli_blob   = { {0} };

uint8_t     g_srv_u8     = 42;
DeviceStats g_srv_stats  = { 3600, 25.5f, 1 };
NameSlot    g_srv_name   = { "SyncBus-node" };
BlobSlot    g_srv_blob   = { { 0xDE,0xAD,0xBE,0xEF, 0x01,0x02,0x03,0x04 } };

// -------------------- Callbacks de envio ------------------------------------
void clientSend(const uint8_t* data, uint8_t size)
{
    std::cout << "[CLIENT->SERVER] " << toHex(data, size) << "\n";
    g_server.inputData(data, size);
}

void serverSend(const uint8_t* data, uint8_t size)
{
    std::cout << "[SERVER->CLIENT] " << toHex(data, size) << "\n";
    g_client.inputData(data, size);
}

// -------------------- Helpers de log ----------------------------------------
static void printClientMirror()
{
    std::cout << "CLIENT MIRROR:\n"
              << "  u8=" << int(g_cli_u8) << "\n"
              << "  stats={ uptime=" << g_cli_stats.uptime_s
              << ", temp=" << g_cli_stats.temperature_c
              << ", err=" << g_cli_stats.error_count << " }\n"
              << "  name=\"" << g_cli_name.data << "\"\n"
              << "  blob=" << toHex(g_cli_blob.bytes, sizeof(g_cli_blob.bytes)) << "\n";
}

static void printServerState()
{
    std::cout << "SERVER STATE:\n"
              << "  u8=" << int(g_srv_u8) << "\n"
              << "  stats={ uptime=" << g_srv_stats.uptime_s
              << ", temp=" << g_srv_stats.temperature_c
              << ", err=" << g_srv_stats.error_count << " }\n"
              << "  name=\"" << g_srv_name.data << "\"\n"
              << "  blob=" << toHex(g_srv_blob.bytes, sizeof(g_srv_blob.bytes)) << "\n";
}

// -------------------- Roteiro de teste --------------------------------------
static void runScript()
{
    std::cout << "=== SyncBus demo (GET/SET de tipos diversos) ===\n";
    printServerState();
    printClientMirror();

    g_client.addData(&g_cli_u8,    kServerId, SLOT_U8,    sizeof(g_cli_u8));
    g_client.addData(&g_cli_stats, kServerId, SLOT_STATS, sizeof(g_cli_stats));
    g_client.addData(&g_cli_name,  kServerId, SLOT_NAME,  sizeof(g_cli_name));
    g_client.addData(&g_cli_blob,  kServerId, SLOT_BLOB,  sizeof(g_cli_blob));

    g_server.addSlot(&g_srv_u8,    SLOT_U8,    sizeof(g_srv_u8));
    g_server.addSlot(&g_srv_stats, SLOT_STATS, sizeof(g_srv_stats));
    g_server.addSlot(&g_srv_name,  SLOT_NAME,  sizeof(g_srv_name));
    g_server.addSlot(&g_srv_blob,  SLOT_BLOB,  sizeof(g_srv_blob));

    std::cout << "\n[1] Cliente faz GET de todos os slots:\n";
    g_client.getData(kServerId, 0);
    g_client.getData(kServerId, 1);
    g_client.getData(kServerId, 2);
    g_client.getData(kServerId, 3);
    printClientMirror();

    std::cout << "\n[2] Cliente altera o espelho e envia SET:\n";
    g_cli_u8 = 77;
    g_cli_stats.temperature_c = 28.0f;
    std::strncpy(g_cli_name.data, "SyncBus-updated", sizeof(g_cli_name.data));
    for (uint8_t i = 0; i < kBlobSize/2; ++i) {
        std::swap(g_cli_blob.bytes[i], g_cli_blob.bytes[kBlobSize-1-i]);
    }

    g_client.setData(kServerId, 0);
    g_client.setData(kServerId, 1);
    g_client.setData(kServerId, 2);
    g_client.setData(kServerId, 3);
    printServerState();

    std::cout << "\n[3] Servidor modifica os dados e cliente faz GET novamente:\n";
    g_srv_u8 = 200;
    g_srv_stats.uptime_s += 123;
    g_srv_stats.error_count += 2;
    std::strncpy(g_srv_name.data, "server-changed", sizeof(g_srv_name.data));
    for (uint8_t& b : g_srv_blob.bytes) { b ^= 0xFF; }

    g_client.getData(kServerId, 0);
    g_client.getData(kServerId, 1);
    g_client.getData(kServerId, 2);
    g_client.getData(kServerId, 3);
    printClientMirror();

    std::cout << "\n=== Fim do roteiro ===\n";
}

int main_test()
{
    runScript();
    return 0;
}
