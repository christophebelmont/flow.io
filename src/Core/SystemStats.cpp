/**
 * @file SystemStats.cpp
 * @brief Implementation file.
 */
#include "SystemStats.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {
uint8_t fragmentationPercent_(uint32_t freeBytes, uint32_t largestBlock)
{
    if (freeBytes == 0U) return 100U;
    float ratio = (float)largestBlock / (float)freeBytes;
    float frag = 1.0f - ratio;
    if (frag < 0.0f) frag = 0.0f;
    if (frag > 1.0f) frag = 1.0f;
    return (uint8_t)(frag * 100.0f);
}
}

void SystemStats::collect(SystemStatsSnapshot& out) {
    const uint64_t uptimeMs64 = (uint64_t)(esp_timer_get_time() / 1000ULL);
    out.uptimeMs64 = uptimeMs64;
    // Keep the legacy 32-bit view for code paths that still expect uint32_t.
    out.uptimeMs = (uint32_t)uptimeMs64;

    const uint32_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t minFree8 = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    out.heap.freeBytes = free8;
    out.heap.minFreeBytes = minFree8;
    out.heap.largestFreeBlock = largest;
    out.heap.fragPercent = fragmentationPercent_(free8, largest);

    const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t internalFree = heap_caps_get_free_size(internalCaps);
    const uint32_t internalMinFree = heap_caps_get_minimum_free_size(internalCaps);
    const uint32_t internalLargest = heap_caps_get_largest_free_block(internalCaps);
    out.heap.internalFreeBytes = internalFree;
    out.heap.internalMinFreeBytes = internalMinFree;
    out.heap.internalLargestFreeBlock = internalLargest;
    out.heap.internalFragPercent = fragmentationPercent_(internalFree, internalLargest);
}

const char* SystemStats::resetReasonStr() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
    }
}
