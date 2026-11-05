#pragma once

#include <string.h>

#include "Board/BoardTypes.h"

inline const IoPointSpec* boardFindIoPoint(const BoardSpec& board, BoardSignal signal)
{
    for (uint8_t i = 0; i < board.ioPointCount; ++i) {
        if (board.ioPoints[i].signal == signal) return &board.ioPoints[i];
    }
    return nullptr;
}

inline const UartSpec* boardFindUart(const BoardSpec& board, const char* name)
{
    if (!name) return nullptr;
    for (uint8_t i = 0; i < board.uartCount; ++i) {
        const UartSpec& spec = board.uarts[i];
        if (spec.name && strcmp(spec.name, name) == 0) return &spec;
    }
    return nullptr;
}

inline const OneWireBusSpec* boardFindOneWire(const BoardSpec& board, BoardSignal signal)
{
    for (uint8_t i = 0; i < board.oneWireCount; ++i) {
        if (board.oneWireBuses[i].signal == signal) return &board.oneWireBuses[i];
    }
    return nullptr;
}

inline const I2cBusSpec* boardFindI2cBus(const BoardSpec& board, const char* name)
{
    if (!name) return nullptr;
    for (uint8_t i = 0; i < board.i2cCount; ++i) {
        const I2cBusSpec& spec = board.i2cBuses[i];
        if (spec.name && strcmp(spec.name, name) == 0) return &spec;
    }
    return nullptr;
}

inline const SupervisorBoardSpec* boardSupervisorConfig(const BoardSpec& board)
{
    return board.supervisor;
}

inline const char* boardMdnsHost(const BoardSpec& board)
{
    return board.mdnsHost;
}

inline IoCapacitySpec boardIoCapacity(const BoardSpec& board)
{
    return board.ioCapacity;
}

inline MqttCapacitySpec boardMqttCapacity(const BoardSpec& board)
{
    return board.mqttCapacity;
}

inline MqttBufferSpec boardMqttBuffers(const BoardSpec& board)
{
    return board.mqttBuffers;
}

inline HaCapacitySpec boardHaCapacity(const BoardSpec& board)
{
    return board.haCapacity;
}

inline const EthernetW5500Spec* boardEthernetW5500(const BoardSpec& board)
{
    return board.ethernetW5500;
}

inline const HmiBuzzerSpec* boardHmiBuzzer(const BoardSpec& board)
{
    return board.hmiBuzzer;
}
