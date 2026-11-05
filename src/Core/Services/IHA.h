#pragma once
/**
 * @file IHA.h
 * @brief Home Assistant discovery service interface.
 */

/** @brief Static Home Assistant sensor discovery registration. */
struct HASensorEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* entityCategory;
    const char* icon;
    const char* unit;
    bool hasEntityName;
    const char* availabilityTemplate;
    bool isText;
};

/** @brief Static Home Assistant binary sensor discovery registration. */
struct HABinarySensorEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* deviceClass;
    const char* entityCategory;
    const char* icon;
};

/** @brief Static Home Assistant switch discovery registration. */
struct HASwitchEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* commandTopicSuffix;
    const char* payloadOn;
    const char* payloadOff;
    const char* icon;
    const char* entityCategory;
};

/** @brief Static Home Assistant number discovery registration. */
struct HANumberEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* commandTopicSuffix;
    const char* commandTemplate;
    float minValue;
    float maxValue;
    float step;
    const char* mode;
    const char* entityCategory;
    const char* icon;
    const char* unit;
};

/** @brief Static Home Assistant select discovery registration. */
struct HASelectEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* stateTopicSuffix;
    const char* valueTemplate;
    const char* commandTopicSuffix;
    const char* commandTemplate;
    const char* optionsJson;
    const char* icon;
    const char* entityCategory;
};

/** @brief Static Home Assistant button discovery registration. */
struct HAButtonEntry {
    const char* ownerId;
    const char* objectSuffix;
    const char* name;
    const char* commandTopicSuffix;
    const char* payloadPress;
    const char* entityCategory;
    const char* icon;
};

/** @brief Service used by modules to register static HA discovery entries and request refreshes. */
struct HAService {
    bool (*addSensor)(void* ctx, const HASensorEntry* entry);
    bool (*addBinarySensor)(void* ctx, const HABinarySensorEntry* entry);
    bool (*addSwitch)(void* ctx, const HASwitchEntry* entry);
    bool (*addNumber)(void* ctx, const HANumberEntry* entry);
    bool (*addSelect)(void* ctx, const HASelectEntry* entry);
    bool (*addButton)(void* ctx, const HAButtonEntry* entry);
    bool (*requestRefresh)(void* ctx);
    void* ctx;
};
