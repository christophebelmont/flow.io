#pragma once
/**
 * @file MQTTModuleDataModel.h
 * @brief MQTT runtime data model contribution.
 */

#include <stdint.h>

/** @brief MQTT runtime status. */
struct MQTTRuntimeData {
    bool mqttReady = false;
    bool runtimeFullSnapshotPublished = false;
    uint32_t rxDrop = 0;
    uint32_t parseFail = 0;
    uint32_t handlerFail = 0;
    uint32_t oversizeDrop = 0;
};

// MODULE_DATA_MODEL: MQTTRuntimeData mqtt
