# 🔗 SyncBus

**SyncBus** é uma biblioteca C++17 simples e leve para comunicação entre **cliente** e **servidor** em sistemas embarcados.  
Ela implementa um protocolo binário baseado em **slots de dados** com callbacks para notificação de mudanças, usando **CRC16 (Modbus)** para integridade.

---

## ✨ Recursos

- Comunicação **cliente-servidor** com múltiplos slots de dados.
- Estrutura de frame fixa e simples:
```

\[0..3] ServerId (uint32\_t, little-endian)
\[4]    SlotId   (uint8\_t)
\[5]    Function (uint8\_t)
\[6..]  Data (N bytes)
\[end]  CRC16 (2 bytes, LO/HI)

````
- Funções suportadas:
- `GetReq / GetResp` → leitura remota
- `SetReq / SetResp` → escrita remota (com ACK opcional)
- Callbacks configuráveis:
- Envio (`SyncBusSendData_cb`)
- Notificação de mudança (`SyncBusDataChanged_cb`)
- Suporte a qualquer tipo de dado **fixo** (ex.: `uint8_t`, `struct`, `array`).
- Não usa alocação dinâmica (`new`/`malloc`).
- Cabe apenas em **um header** (`SyncBus.hpp`).

---

## 📦 Instalação

Basta copiar `SyncBus.hpp` para o seu projeto e incluir:

```cpp
#include "SyncBus.hpp"
````

---

## 🛠️ Uso Básico

### Definição de Cliente e Servidor

```cpp
#include "SyncBus.hpp"
using namespace syncbus;

void clientSend(const uint8_t* data, uint8_t size);
void serverSend(const uint8_t* data, uint8_t size);

// Cliente e Servidor com 2 slots cada
SyncBusClient<2> client(clientSend);
SyncBusServer<2> server(0x12345678, serverSend);
```

### Registro de Slots

```cpp
uint8_t clientValue = 0;
uint8_t serverValue = 42;

// Cliente conhece o slot remoto
client.addData(&clientValue, 0x12345678, 1, sizeof(clientValue));

// Servidor mantém o dado oficial
server.addSlot(&serverValue, 1, sizeof(serverValue));
```

### Envio de GET e SET

```cpp
// Cliente requisita leitura do slot 1
client.getData(0x12345678, 0);

// Cliente altera valor local e envia para o servidor
clientValue = 99;
client.setData(0x12345678, 0);
```

### Processamento de Dados Recebidos

* `client.inputData(frame, size)` → processa resposta do servidor.
* `server.inputData(frame, size)` → processa requisição do cliente.

---

## 🔬 Exemplo Completo

```cpp
#include <iostream>
#include "SyncBus.hpp"

using namespace syncbus;

void clientSend(const uint8_t* d, uint8_t n);
void serverSend(const uint8_t* d, uint8_t n);

SyncBusClient<1> client(clientSend, [](uint8_t slot){ 
    std::cout << "Client slot " << int(slot) << " updated!\n"; 
});
SyncBusServer<1> server(0x12345678, serverSend, [](uint8_t slot){ 
    std::cout << "Server slot " << int(slot) << " changed!\n"; 
});

uint8_t clientVal = 0;
uint8_t serverVal = 42;

void clientSend(const uint8_t* d, uint8_t n) { server.inputData(d, n); }
void serverSend(const uint8_t* d, uint8_t n) { client.inputData(d, n); }

int main() {
    client.addData(&clientVal, 0x12345678, 1, sizeof(clientVal));
    server.addSlot(&serverVal, 1, sizeof(serverVal));

    client.getData(0x12345678, 0);
    clientVal = 99;
    client.setData(0x12345678, 0);
}
```

---

## 📂 Estrutura do Projeto

```
SyncBus/
 ├── SyncBus.hpp     # Arquivo único da biblioteca
 ├── examples/
 │    ├── demo.cpp   # Exemplo de uso com cliente/servidor
 └── README.md
```

---

## ⚙️ Configuração

* `SYNC_BUS_BUFFER_SIZE` → define o tamanho máximo de frame (default: `64`).
* `SYNCBUS_ENABLE_SET_ACK` → habilita ACK no `SetReq` (default: `1`).

---

## 📜 Licença

MIT License – veja o arquivo [LICENSE](LICENSE).

---

## 👤 Autor

Desenvolvido por **Samuel Almeida Rocha**

---

