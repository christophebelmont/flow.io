/**
 * @file ConfigMenuModel.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/ConfigMenuModel.h"
#include "Domain/Pool/PoolDomain.h"

#include "Core/BufferUsageTracker.h"
#include "Core/SystemLimits.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

static constexpr size_t kMenuJsonDocCapacity = Limits::Mqtt::Buffers::StateCfg + 512U;

static void copyStr_(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

static bool parseBoolText_(const char* text, bool& out)
{
    if (!text) return false;
    if (strcasecmp(text, "1") == 0 || strcasecmp(text, "true") == 0 ||
        strcasecmp(text, "on") == 0 || strcasecmp(text, "yes") == 0) {
        out = true;
        return true;
    }
    if (strcasecmp(text, "0") == 0 || strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "off") == 0 || strcasecmp(text, "no") == 0) {
        out = false;
        return true;
    }
    return false;
}

static void trimFloat_(char* s)
{
    if (!s) return;
    char* dot = strchr(s, '.');
    if (!dot) return;

    char* end = s + strlen(s) - 1;
    while (end > dot && *end == '0') {
        *end = '\0';
        --end;
    }
    if (end == dot) *end = '\0';
}

static bool isHexDisplayField_(const char* module, const char* key)
{
    if (!module || !key) return false;
    if (strcmp(key, "target_addr") == 0) {
        return strcmp(module, "elink/client") == 0;
    }
    if (strcmp(key, "address") != 0) return false;
    if (strcmp(module, "elink/server") == 0) return true;
    return strncmp(module, "io/drivers/", strlen("io/drivers/")) == 0;
}

static bool parseIntText_(const char* text, bool hexDisplay, int32_t& out)
{
    if (!text) return false;
    char* end = nullptr;
    const int base = hexDisplay ? 16 : 10;
    const long v = strtol(text, &end, base);
    if (!end || *end != '\0') return false;
    out = (int32_t)v;
    return true;
}

static bool parseFloatText_(const char* text, float& out)
{
    if (!text) return false;

    char normalized[64]{};
    const size_t maxCopy = sizeof(normalized) - 1U;
    size_t len = strnlen(text, maxCopy);
    memcpy(normalized, text, len);
    normalized[len] = '\0';

    size_t start = 0U;
    while (normalized[start] != '\0' && isspace((unsigned char)normalized[start])) ++start;

    while (len > start && isspace((unsigned char)normalized[len - 1U])) {
        normalized[len - 1U] = '\0';
        --len;
    }
    if (normalized[start] == '\0') return false;

    if (start > 0U) memmove(normalized, normalized + start, strlen(normalized + start) + 1U);
    for (size_t i = 0U; normalized[i] != '\0'; ++i) {
        if (normalized[i] == ',') normalized[i] = '.';
    }

    char* end = nullptr;
    const float v = strtof(normalized, &end);
    if (!end || *end != '\0') return false;
    out = v;
    return true;
}

static void formatHexInt_(char* out, size_t outLen, int32_t value)
{
    if (!out || outLen == 0) return;

    const bool negative = value < 0;
    const uint32_t raw = negative ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    const int width = (raw <= 0xFFU) ? 2 : 0;
    if (negative) {
        if (width > 0) snprintf(out, outLen, "-0x%0*lX", width, (unsigned long)raw);
        else snprintf(out, outLen, "-0x%lX", (unsigned long)raw);
        return;
    }
    if (width > 0) snprintf(out, outLen, "0x%0*lX", width, (unsigned long)raw);
    else snprintf(out, outLen, "0x%lX", (unsigned long)raw);
}

static bool jsonEscape_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return false;
    if (!in) in = "";

    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0'; ++i) {
        const unsigned char uc = (unsigned char)in[i];
        const char c = (char)uc;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= outLen) return false;
            out[pos++] = '\\';
            out[pos++] = c;
        } else if (uc < 32U) {
            if (pos + 6 >= outLen) return false;
            const int n = snprintf(out + pos, outLen - pos, "\\u%04X", (unsigned)uc);
            if (n != 6) return false;
            pos += 6U;
        } else {
            if (pos + 1 >= outLen) return false;
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
    return true;
}

static uint8_t countCsvOptions_(const char* csv)
{
    if (!csv || csv[0] == '\0') return 0;

    uint8_t count = 0;
    const char* p = csv;
    while (*p != '\0') {
        bool hasToken = false;
        while (*p != '\0' && *p != '|') {
            hasToken = true;
            ++p;
        }
        if (hasToken && count < 0xFFU) ++count;
        if (*p == '|') ++p;
    }
    return count;
}

static bool csvOptionAt_(const char* csv, uint8_t wanted, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';
    if (!csv || csv[0] == '\0') return false;

    uint8_t index = 0;
    const char* p = csv;
    while (*p != '\0') {
        char token[48]{};
        uint8_t n = 0;
        while (*p != '\0' && *p != '|') {
            if (n < (uint8_t)(sizeof(token) - 1U)) token[n++] = *p;
            ++p;
        }
        token[n] = '\0';
        if (n > 0) {
            if (index == wanted) {
                copyStr_(out, outLen, token);
                return true;
            }
            ++index;
        }
        if (*p == '|') ++p;
    }
    return false;
}

static bool csvOptionIndex_(const char* csv, const char* value, uint8_t& out)
{
    if (!csv || !value) return false;

    uint8_t index = 0;
    const char* p = csv;
    while (*p != '\0') {
        char token[48]{};
        uint8_t n = 0;
        while (*p != '\0' && *p != '|') {
            if (n < (uint8_t)(sizeof(token) - 1U)) token[n++] = *p;
            ++p;
        }
        token[n] = '\0';
        if (n > 0) {
            if (strcmp(token, value) == 0) {
                out = index;
                return true;
            }
            ++index;
        }
        if (*p == '|') ++p;
    }
    return false;
}

static bool startsWithBranch_(const char* module, const char* branch)
{
    if (!module) return false;
    if (!branch || branch[0] == '\0') return true;
    const size_t len = strlen(branch);
    return strncmp(module, branch, len) == 0 && module[len] == '/';
}

static bool immediateChildForBranch_(const char* module,
                                     const char* branch,
                                     char* child,
                                     size_t childLen,
                                     char* fullPath,
                                     size_t fullPathLen)
{
    if (!module || !child || childLen == 0 || !fullPath || fullPathLen == 0) return false;
    child[0] = '\0';
    fullPath[0] = '\0';

    const char* rel = module;
    if (branch && branch[0] != '\0') {
        if (!startsWithBranch_(module, branch)) return false;
        rel = module + strlen(branch) + 1U;
    }
    if (!rel || rel[0] == '\0') return false;

    uint8_t n = 0;
    while (rel[n] != '\0' && rel[n] != '/' && n + 1U < childLen) {
        child[n] = rel[n];
        ++n;
    }
    child[n] = '\0';
    if (child[0] == '\0') return false;

    if (branch && branch[0] != '\0') {
        const int written = snprintf(fullPath, fullPathLen, "%s/%s", branch, child);
        return written > 0 && (size_t)written < fullPathLen;
    }

    copyStr_(fullPath, fullPathLen, child);
    return fullPath[0] != '\0';
}

static uint8_t editTypeFromValueType_(ConfigMenuValueType type)
{
    switch (type) {
        case ConfigMenuValueType::Bool:
            return 3U;
        case ConfigMenuValueType::Int:
            return 1U;
        case ConfigMenuValueType::Float:
            return 2U;
        case ConfigMenuValueType::Text:
        case ConfigMenuValueType::Unknown:
        default:
            return 0U;
    }
}

} // namespace

bool ConfigMenuModel::begin(const ConfigStoreService* cfgSvc)
{
    cfgSvc_ = cfgSvc;
    hints_ = nullptr;
    hintCount_ = 0;
    mode_ = ConfigMenuMode::Browse;
    pageIndex_ = 0;
    editReturnPageIndex_ = 0;
    currentModule_[0] = '\0';
    previousModule_[0] = '\0';
    BufferUsageTracker::note(TrackedBufferId::ConfigMenuHeap, 0U, 0U, "stateless", nullptr);
    return cfgSvc_ != nullptr;
}

void ConfigMenuModel::setHints(const ConfigMenuHint* hints, uint8_t count)
{
    hints_ = hints;
    hintCount_ = count;
}

bool ConfigMenuModel::home()
{
    if (!isHome()) copyStr_(previousModule_, sizeof(previousModule_), currentModule_);
    mode_ = ConfigMenuMode::Browse;
    currentModule_[0] = '\0';
    pageIndex_ = 0;
    editReturnPageIndex_ = 0;
    return cfgSvc_ && cfgSvc_->listModules;
}

bool ConfigMenuModel::back()
{
    if (mode_ == ConfigMenuMode::Edit) {
        mode_ = ConfigMenuMode::Browse;
        copyStr_(currentModule_, sizeof(currentModule_), previousModule_);
        previousModule_[0] = '\0';
        pageIndex_ = editReturnPageIndex_;
        const uint8_t cnt = pageCount();
        if (cnt == 0U) pageIndex_ = 0U;
        else if (pageIndex_ >= cnt) pageIndex_ = (uint8_t)(cnt - 1U);
        editReturnPageIndex_ = 0U;
        return true;
    }

    if (isHome()) return false;
    char* slash = strrchr(currentModule_, '/');
    if (!slash) {
        currentModule_[0] = '\0';
    } else {
        *slash = '\0';
    }
    pageIndex_ = 0;
    return true;
}

bool ConfigMenuModel::openModule(const char* module)
{
    if (!module || module[0] == '\0') return false;
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;
    if (!moduleExists_(module)) return false;

    if (!isHome()) copyStr_(previousModule_, sizeof(previousModule_), currentModule_);
    else previousModule_[0] = '\0';
    editReturnPageIndex_ = pageIndex_;

    copyStr_(currentModule_, sizeof(currentModule_), module);
    mode_ = ConfigMenuMode::Edit;
    pageIndex_ = 0;
    return true;
}

bool ConfigMenuModel::refreshCurrent()
{
    return cfgSvc_ != nullptr;
}

bool ConfigMenuModel::enterRow(uint8_t rowOnPage)
{
    if (mode_ != ConfigMenuMode::Browse) return false;
    if (rowOnPage >= RowsPerPage) return false;

    const uint8_t idx = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage + rowOnPage);

    char fullPath[28]{};
    char label[28]{};
    bool hasChildren = false;
    bool hasModule = false;
    bool hasAttributes = false;
    if (!branchRowAt_(currentModule_,
                      idx,
                      fullPath,
                      sizeof(fullPath),
                      label,
                      sizeof(label),
                      hasChildren,
                      hasModule,
                      hasAttributes)) {
        return false;
    }
    if (!hasChildren) return false;

    copyStr_(previousModule_, sizeof(previousModule_), currentModule_);
    copyStr_(currentModule_, sizeof(currentModule_), fullPath);
    pageIndex_ = 0;
    return true;
}

bool ConfigMenuModel::editRow(uint8_t rowOnPage)
{
    if (mode_ != ConfigMenuMode::Browse) return false;
    if (rowOnPage >= RowsPerPage) return false;

    const uint8_t idx = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage + rowOnPage);

    char fullPath[28]{};
    char label[28]{};
    bool hasChildren = false;
    bool hasModule = false;
    bool hasAttributes = false;
    if (!branchRowAt_(currentModule_,
                      idx,
                      fullPath,
                      sizeof(fullPath),
                      label,
                      sizeof(label),
                      hasChildren,
                      hasModule,
                      hasAttributes)) {
        return false;
    }
    if (!hasModule || !hasAttributes) return false;
    return openModule(fullPath);
}

uint8_t ConfigMenuModel::pageCount() const
{
    const uint8_t rows = currentRowCount_();
    if (rows == 0) return 1;
    return (uint8_t)((rows + RowsPerPage - 1U) / RowsPerPage);
}

bool ConfigMenuModel::nextPage()
{
    const uint8_t cnt = pageCount();
    if (pageIndex_ + 1U >= cnt) return false;
    ++pageIndex_;
    return true;
}

bool ConfigMenuModel::prevPage()
{
    if (pageIndex_ == 0) return false;
    --pageIndex_;
    return true;
}

uint8_t ConfigMenuModel::listSortedModules_(const char** modules, uint8_t max) const
{
    if (!modules || max == 0 || !cfgSvc_ || !cfgSvc_->listModules) return 0;
    uint8_t count = cfgSvc_->listModules(cfgSvc_->ctx, modules, max);
    if (count > max) count = max;

    for (uint8_t i = 0; i < count; ++i) {
        if (!modules[i]) modules[i] = "";
    }
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t j = (uint8_t)(i + 1U); j < count; ++j) {
            if (strcmp(modules[i], modules[j]) > 0) {
                const char* tmp = modules[i];
                modules[i] = modules[j];
                modules[j] = tmp;
            }
        }
    }
    return count;
}

uint8_t ConfigMenuModel::currentRowCount_() const
{
    if (mode_ == ConfigMenuMode::Browse) return branchRowCount_(currentModule_);
    return moduleRowCount_(currentModule_);
}

uint8_t ConfigMenuModel::branchRowCount_(const char* branch) const
{
    const char* modules[MaxModules]{};
    const uint8_t moduleCount = listSortedModules_(modules, MaxModules);

    uint8_t count = 0;
    char lastFullPath[28]{};
    for (uint8_t i = 0; i < moduleCount; ++i) {
        char child[28]{};
        char fullPath[28]{};
        if (!immediateChildForBranch_(modules[i],
                                      branch,
                                      child,
                                      sizeof(child),
                                      fullPath,
                                      sizeof(fullPath))) {
            continue;
        }
        if (lastFullPath[0] != '\0' && strcmp(lastFullPath, fullPath) == 0) continue;
        copyStr_(lastFullPath, sizeof(lastFullPath), fullPath);
        if (count < MaxRows) ++count;
    }
    return count;
}

uint8_t ConfigMenuModel::moduleRowCount_(const char* module) const
{
    if (!cfgSvc_ || !cfgSvc_->toJsonModule || !module || module[0] == '\0') return 0;

    char* jsonBuf = (char*)malloc(Limits::Mqtt::Buffers::StateCfg);
    if (!jsonBuf) return 0;
    jsonBuf[0] = '\0';

    bool truncated = false;
    const bool ok = cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                          module,
                                          jsonBuf,
                                          Limits::Mqtt::Buffers::StateCfg,
                                          &truncated);
    if (!ok || truncated) {
        free(jsonBuf);
        return 0;
    }

    DynamicJsonDocument doc(kMenuJsonDocCapacity);
    const DeserializationError err = deserializeJson(doc, jsonBuf);
    free(jsonBuf);
    if (err || !doc.is<JsonObjectConst>()) return 0;

    uint8_t count = 0;
    JsonObjectConst obj = doc.as<JsonObjectConst>();
    for (JsonPairConst kv : obj) {
        if (count >= MaxRows) break;
        Row row{};
        if (fillRowFromJson_(module, kv.key().c_str(), kv.value(), row)) {
            ++count;
        }
    }
    return count;
}

bool ConfigMenuModel::branchRowAt_(const char* branch,
                                   uint8_t index,
                                   char* fullPath,
                                   size_t fullPathLen,
                                   char* label,
                                   size_t labelLen,
                                   bool& hasChildren,
                                   bool& hasModule,
                                   bool& hasAttributes) const
{
    if (!fullPath || fullPathLen == 0 || !label || labelLen == 0) return false;
    fullPath[0] = '\0';
    label[0] = '\0';
    hasChildren = false;
    hasModule = false;
    hasAttributes = false;

    const char* modules[MaxModules]{};
    const uint8_t moduleCount = listSortedModules_(modules, MaxModules);

    uint8_t current = 0;
    char lastFullPath[28]{};
    for (uint8_t i = 0; i < moduleCount; ++i) {
        char child[28]{};
        char path[28]{};
        if (!immediateChildForBranch_(modules[i],
                                      branch,
                                      child,
                                      sizeof(child),
                                      path,
                                      sizeof(path))) {
            continue;
        }
        if (lastFullPath[0] != '\0' && strcmp(lastFullPath, path) == 0) continue;
        copyStr_(lastFullPath, sizeof(lastFullPath), path);
        if (current != index) {
            if (current < MaxRows) ++current;
            continue;
        }

        copyStr_(fullPath, fullPathLen, path);
        if (!decorateBranchLabel_(path, child, label, labelLen)) {
            copyStr_(label, labelLen, child);
        }
        hasModule = moduleExists_(path);
        hasChildren = moduleHasChildren_(path);
        hasAttributes = hasModule && moduleRowCount_(path) > 0;
        return fullPath[0] != '\0' && label[0] != '\0';
    }
    return false;
}

bool ConfigMenuModel::decorateBranchLabel_(const char* fullPath,
                                           const char* child,
                                           char* out,
                                           size_t outLen) const
{
    if (!fullPath || !child || !out || outLen == 0) return false;
    out[0] = '\0';

    const char* slash = strrchr(fullPath, '/');
    const char* ref = slash ? (slash + 1) : child;
    if (!ref || ref[0] == '\0') ref = child;
    if (!ref || ref[0] == '\0') return false;

    char textName[40]{};
    bool hasName = false;

    if (strncmp(fullPath, "io/input/", 9) == 0 && strlen(ref) == 3 &&
        (ref[0] == 'a' || ref[0] == 'i') &&
        isdigit((unsigned char)ref[1]) &&
        isdigit((unsigned char)ref[2])) {
        char nameKey[16]{};
        snprintf(nameKey, sizeof(nameKey), "%c%c%c_name", ref[0], ref[1], ref[2]);
        hasName = readModuleNameField_(fullPath, nameKey, textName, sizeof(textName));
    } else if (strncmp(fullPath, "pdm/pd", 6) == 0 && strlen(ref) >= 3 && strncmp(ref, "pd", 2) == 0) {
        const char* pdName = poolDeviceDisplayNameByRef_(ref);
        if (pdName && pdName[0] != '\0') {
            copyStr_(textName, sizeof(textName), pdName);
            hasName = true;
        }
    }

    if (hasName && textName[0] != '\0') {
        const int n = snprintf(out, outLen, "%s [%s]", ref, textName);
        if (n > 0 && (size_t)n < outLen) return true;
    }

    copyStr_(out, outLen, ref);
    return true;
}

bool ConfigMenuModel::readModuleNameField_(const char* modulePath,
                                           const char* key,
                                           char* out,
                                           size_t outLen) const
{
    if (!modulePath || !key || !out || outLen == 0) return false;
    out[0] = '\0';
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;

    char* jsonBuf = (char*)malloc(Limits::Mqtt::Buffers::StateCfg);
    if (!jsonBuf) return false;
    jsonBuf[0] = '\0';

    bool truncated = false;
    const bool ok = cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                          modulePath,
                                          jsonBuf,
                                          Limits::Mqtt::Buffers::StateCfg,
                                          &truncated);
    if (!ok || truncated) {
        free(jsonBuf);
        return false;
    }

    DynamicJsonDocument doc(kMenuJsonDocCapacity);
    const DeserializationError err = deserializeJson(doc, jsonBuf);
    free(jsonBuf);
    if (err || !doc.is<JsonObjectConst>()) return false;

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    JsonVariantConst nameVar = obj[key];
    if (!nameVar.is<const char*>()) return false;

    const char* raw = nameVar.as<const char*>();
    if (!raw) return false;
    while (*raw == ' ' || *raw == '\t') ++raw;
    if (*raw == '\0') return false;
    copyStr_(out, outLen, raw);
    return out[0] != '\0';
}

const char* ConfigMenuModel::poolDeviceDisplayNameByRef_(const char* ref) const
{
    if (!ref || strncmp(ref, "pd", 2) != 0) return nullptr;
    const int slot = atoi(ref + 2);
    if (slot < 0) return nullptr;
    const uint8_t uSlot = (uint8_t)slot;

    const uint8_t count = (uint8_t)(sizeof(PoolDomain::kPoolDevices) / sizeof(PoolDomain::kPoolDevices[0]));
    for (uint8_t i = 0; i < count; ++i) {
        const PoolDevicePreset& preset = PoolDomain::kPoolDevices[i];
        if (preset.id == uSlot) {
            return preset.displayName;
        }
    }
    return nullptr;
}

bool ConfigMenuModel::moduleExists_(const char* module) const
{
    if (!module || module[0] == '\0') return false;
    const char* modules[MaxModules]{};
    const uint8_t moduleCount = listSortedModules_(modules, MaxModules);
    for (uint8_t i = 0; i < moduleCount; ++i) {
        if (strcmp(modules[i], module) == 0) return true;
    }
    return false;
}

bool ConfigMenuModel::moduleHasChildren_(const char* module) const
{
    if (!module || module[0] == '\0') return false;
    const char* modules[MaxModules]{};
    const uint8_t moduleCount = listSortedModules_(modules, MaxModules);
    for (uint8_t i = 0; i < moduleCount; ++i) {
        if (startsWithBranch_(modules[i], module)) return true;
    }
    return false;
}

bool ConfigMenuModel::configRowAt_(const char* module, uint8_t index, Row& out) const
{
    out = Row{};
    if (!cfgSvc_ || !cfgSvc_->toJsonModule || !module || module[0] == '\0') return false;

    char* jsonBuf = (char*)malloc(Limits::Mqtt::Buffers::StateCfg);
    if (!jsonBuf) return false;
    jsonBuf[0] = '\0';

    bool truncated = false;
    const bool ok = cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                          module,
                                          jsonBuf,
                                          Limits::Mqtt::Buffers::StateCfg,
                                          &truncated);
    if (!ok || truncated) {
        free(jsonBuf);
        return false;
    }

    DynamicJsonDocument doc(kMenuJsonDocCapacity);
    const DeserializationError err = deserializeJson(doc, jsonBuf);
    free(jsonBuf);
    if (err || !doc.is<JsonObjectConst>()) return false;

    uint8_t current = 0;
    JsonObjectConst obj = doc.as<JsonObjectConst>();
    for (JsonPairConst kv : obj) {
        if (current >= MaxRows) break;
        Row row{};
        if (!fillRowFromJson_(module, kv.key().c_str(), kv.value(), row)) continue;
        if (current == index) {
            out = row;
            return true;
        }
        ++current;
    }
    return false;
}

bool ConfigMenuModel::fillRowFromJson_(const char* module, const char* key, JsonVariantConst value, Row& row) const
{
    row = Row{};
    if (!module || !key || key[0] == '\0') return false;

    row.editable = true;
    row.widget = ConfigMenuWidget::Text;
    copyStr_(row.module, sizeof(row.module), module);
    copyStr_(row.key, sizeof(row.key), key);
    copyStr_(row.label, sizeof(row.label), key);
    row.hexDisplay = isHexDisplayField_(module, key);

    if (value.is<bool>()) {
        row.type = ConfigMenuValueType::Bool;
        row.boolCur = value.as<bool>();
        row.widget = ConfigMenuWidget::Switch;
    } else if (value.is<int32_t>() || value.is<uint32_t>()) {
        row.type = ConfigMenuValueType::Int;
        row.intCur = value.as<int32_t>();
    } else if (value.is<float>() || value.is<double>()) {
        row.type = ConfigMenuValueType::Float;
        row.floatCur = value.as<float>();
    } else if (value.is<const char*>()) {
        row.type = ConfigMenuValueType::Text;
        copyStr_(row.textCur, sizeof(row.textCur), value.as<const char*>());
    } else {
        return false;
    }

    applyHints_(row);
    formatValueText_(row);
    return true;
}

bool ConfigMenuModel::setRowValueFromText_(Row& row, const char* value) const
{
    if (!value) value = "";

    switch (row.type) {
        case ConfigMenuValueType::Bool: {
            bool b = false;
            if (!parseBoolText_(value, b)) return false;
            row.boolCur = b;
            return true;
        }
        case ConfigMenuValueType::Int:
            return parseIntText_(value, row.hexDisplay, row.intCur);
        case ConfigMenuValueType::Float: {
            float v = 0.0f;
            if (!parseFloatText_(value, v)) return false;
            row.floatCur = v;
            return true;
        }
        case ConfigMenuValueType::Text:
            copyStr_(row.textCur, sizeof(row.textCur), value);
            return true;
        default:
            return false;
    }
}

bool ConfigMenuModel::setText(uint8_t rowOnPage, const char* value)
{
    if (mode_ != ConfigMenuMode::Edit) return false;
    if (rowOnPage >= RowsPerPage) return false;

    Row row{};
    const uint8_t idx = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage + rowOnPage);
    if (!configRowAt_(currentModule_, idx, row)) return false;
    if (!row.editable) return false;
    if (!setRowValueFromText_(row, value)) return false;

    return applySingleRow_(row);
}

bool ConfigMenuModel::toggleSwitch(uint8_t rowOnPage)
{
    if (mode_ != ConfigMenuMode::Edit) return false;
    if (rowOnPage >= RowsPerPage) return false;

    Row row{};
    const uint8_t idx = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage + rowOnPage);
    if (!configRowAt_(currentModule_, idx, row)) return false;
    if (!row.editable) return false;
    if (row.type != ConfigMenuValueType::Bool) return false;
    row.boolCur = !row.boolCur;
    return applySingleRow_(row);
}

bool ConfigMenuModel::cycleSelect(uint8_t rowOnPage, int8_t direction)
{
    if (mode_ != ConfigMenuMode::Edit) return false;
    if (rowOnPage >= RowsPerPage) return false;

    Row row{};
    const uint8_t idx = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage + rowOnPage);
    if (!configRowAt_(currentModule_, idx, row)) return false;
    if (!row.editable) return false;
    if (row.widget != ConfigMenuWidget::Select || row.optionCount == 0 || !row.optionsCsv) return false;

    uint8_t cur = 0;
    if (!csvOptionIndex_(row.optionsCsv, row.value, cur)) cur = 0;

    int16_t next = (int16_t)cur + direction;
    while (next < 0) next += row.optionCount;
    while (next >= row.optionCount) next -= row.optionCount;

    char nextValue[48]{};
    if (!csvOptionAt_(row.optionsCsv, (uint8_t)next, nextValue, sizeof(nextValue))) return false;
    if (!setRowValueFromText_(row, nextValue)) return false;
    return applySingleRow_(row);
}

bool ConfigMenuModel::setSlider(uint8_t rowOnPage, float value)
{
    if (mode_ != ConfigMenuMode::Edit) return false;
    if (rowOnPage >= RowsPerPage) return false;

    Row row{};
    const uint8_t idx = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage + rowOnPage);
    if (!configRowAt_(currentModule_, idx, row)) return false;
    if (!row.editable) return false;
    if (row.widget != ConfigMenuWidget::Slider) return false;
    if (row.type != ConfigMenuValueType::Int && row.type != ConfigMenuValueType::Float) return false;

    float v = value;
    if (v < row.sliderMin) v = row.sliderMin;
    if (v > row.sliderMax) v = row.sliderMax;
    if (row.sliderStep > 0.0f) {
        const float k = roundf((v - row.sliderMin) / row.sliderStep);
        v = row.sliderMin + (k * row.sliderStep);
    }

    if (row.type == ConfigMenuValueType::Int) row.intCur = (int32_t)lroundf(v);
    else row.floatCur = v;
    return applySingleRow_(row);
}

bool ConfigMenuModel::validate(char* ack, size_t ackLen)
{
    if (ack && ackLen > 0) snprintf(ack, ackLen, "{\"ok\":true,\"applied\":0}");
    return true;
}

void ConfigMenuModel::buildView(ConfigMenuView& out) const
{
    out = ConfigMenuView{};

    buildBreadcrumb_(out.breadcrumb, sizeof(out.breadcrumb));
    out.pageIndex = pageIndex_;
    out.pageCount = 1;
    out.canHome = true;
    out.canBack = (mode_ == ConfigMenuMode::Edit) || !isHome();
    out.canValidate = false;
    out.isHome = (mode_ == ConfigMenuMode::Browse && isHome());
    out.mode = mode_;

    if (mode_ == ConfigMenuMode::Browse) buildHomeView_(out);
    else buildModuleView_(out);
}

void ConfigMenuModel::buildHomeView_(ConfigMenuView& out) const
{
    const uint8_t count = branchRowCount_(currentModule_);
    out.pageCount = count == 0 ? 1 : (uint8_t)((count + RowsPerPage - 1U) / RowsPerPage);

    uint8_t visible = 0;
    const uint8_t first = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage);
    for (uint8_t i = 0; i < RowsPerPage; ++i) {
        const uint8_t idx = (uint8_t)(first + i);
        if (idx >= count) break;

        char fullPath[28]{};
        char label[28]{};
        bool hasChildren = false;
        bool hasModule = false;
        bool hasAttributes = false;
        if (!branchRowAt_(currentModule_,
                          idx,
                          fullPath,
                          sizeof(fullPath),
                          label,
                          sizeof(label),
                          hasChildren,
                          hasModule,
                          hasAttributes)) {
            break;
        }

        ConfigMenuRowView& vr = out.rows[i];
        vr.visible = true;
        vr.valueVisible = false;
        vr.editable = hasAttributes;
        vr.canEnter = hasChildren;
        vr.canEdit = hasAttributes;
        vr.widget = ConfigMenuWidget::Text;
        vr.editType = 0U;
        copyStr_(vr.key, sizeof(vr.key), fullPath);
        if (hasChildren) snprintf(vr.label, sizeof(vr.label), "%s/", label);
        else copyStr_(vr.label, sizeof(vr.label), label);
        vr.value[0] = '\0';
        ++visible;
    }
    out.rowCountOnPage = visible;
}

void ConfigMenuModel::buildModuleView_(ConfigMenuView& out) const
{
    if (!cfgSvc_ || !cfgSvc_->toJsonModule || currentModule_[0] == '\0') return;

    char* jsonBuf = (char*)malloc(Limits::Mqtt::Buffers::StateCfg);
    if (!jsonBuf) return;
    jsonBuf[0] = '\0';

    bool truncated = false;
    const bool ok = cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                          currentModule_,
                                          jsonBuf,
                                          Limits::Mqtt::Buffers::StateCfg,
                                          &truncated);
    if (!ok || truncated) {
        free(jsonBuf);
        return;
    }

    DynamicJsonDocument doc(kMenuJsonDocCapacity);
    const DeserializationError err = deserializeJson(doc, jsonBuf);
    free(jsonBuf);
    if (err || !doc.is<JsonObjectConst>()) return;

    const uint8_t first = (uint8_t)((uint16_t)pageIndex_ * RowsPerPage);
    uint8_t total = 0;
    uint8_t visible = 0;
    JsonObjectConst obj = doc.as<JsonObjectConst>();
    for (JsonPairConst kv : obj) {
        if (total >= MaxRows) break;

        Row row{};
        if (!fillRowFromJson_(currentModule_, kv.key().c_str(), kv.value(), row)) continue;

        if (total >= first && visible < RowsPerPage) {
            ConfigMenuRowView& vr = out.rows[visible];
            vr.visible = true;
            vr.valueVisible = true;
            vr.editable = row.editable;
            vr.dirty = false;
            vr.canEnter = false;
            vr.canEdit = false;
            vr.widget = row.widget;
            vr.editType = editTypeFromValueType_(row.type);
            copyStr_(vr.key, sizeof(vr.key), row.key);
            copyStr_(vr.label, sizeof(vr.label), row.label);
            copyStr_(vr.value, sizeof(vr.value), row.value);
            ++visible;
        }
        ++total;
    }

    out.pageCount = total == 0 ? 1 : (uint8_t)((total + RowsPerPage - 1U) / RowsPerPage);
    out.rowCountOnPage = visible;
}

bool ConfigMenuModel::applySingleRow_(const Row& row) const
{
    if (!cfgSvc_ || !cfgSvc_->applyJson) return false;
    if (row.module[0] == '\0' || row.key[0] == '\0') return false;

    char value[112]{};
    switch (row.type) {
        case ConfigMenuValueType::Bool:
            copyStr_(value, sizeof(value), row.boolCur ? "true" : "false");
            break;
        case ConfigMenuValueType::Int:
            snprintf(value, sizeof(value), "%ld", (long)row.intCur);
            break;
        case ConfigMenuValueType::Float:
            snprintf(value, sizeof(value), "%.6g", (double)row.floatCur);
            break;
        case ConfigMenuValueType::Text: {
            char escaped[96]{};
            if (!jsonEscape_(escaped, sizeof(escaped), row.textCur)) return false;
            const int n = snprintf(value, sizeof(value), "\"%s\"", escaped);
            if (n <= 0 || (size_t)n >= sizeof(value)) return false;
            break;
        }
        default:
            return false;
    }

    char payload[256]{};
    const int n = snprintf(payload,
                           sizeof(payload),
                           "{\"%s\":{\"%s\":%s}}",
                           row.module,
                           row.key,
                           value);
    if (n <= 0 || (size_t)n >= sizeof(payload)) return false;
    return cfgSvc_->applyJson(cfgSvc_->ctx, payload);
}

void ConfigMenuModel::formatValueText_(Row& row) const
{
    switch (row.type) {
        case ConfigMenuValueType::Bool:
            copyStr_(row.value, sizeof(row.value), row.boolCur ? "ON" : "OFF");
            break;
        case ConfigMenuValueType::Int:
            if (row.hexDisplay) formatHexInt_(row.value, sizeof(row.value), row.intCur);
            else snprintf(row.value, sizeof(row.value), "%ld", (long)row.intCur);
            break;
        case ConfigMenuValueType::Float:
            snprintf(row.value, sizeof(row.value), "%.3f", (double)row.floatCur);
            trimFloat_(row.value);
            break;
        case ConfigMenuValueType::Text:
            copyStr_(row.value, sizeof(row.value), row.textCur);
            break;
        default:
            row.value[0] = '\0';
            break;
    }
}

const ConfigMenuHint* ConfigMenuModel::findHint_(const char* module, const char* key) const
{
    if (!hints_ || hintCount_ == 0 || !key) return nullptr;
    for (uint8_t i = 0; i < hintCount_; ++i) {
        const ConfigMenuHint& h = hints_[i];
        if (!h.key || strcmp(h.key, key) != 0) continue;
        if (h.module && module && strcmp(h.module, module) != 0) continue;
        if (h.module && !module) continue;
        return &h;
    }
    return nullptr;
}

void ConfigMenuModel::applyHints_(Row& row) const
{
    const ConfigMenuHint* hint = findHint_(row.module, row.key);
    if (!hint) return;

    row.widget = hint->constraints.widget;
    row.editable = hint->constraints.editable;

    if (row.widget == ConfigMenuWidget::Select) {
        row.optionsCsv = hint->constraints.optionsCsv;
        row.optionCount = countCsvOptions_(row.optionsCsv);
        if (row.optionCount == 0) {
            row.optionsCsv = nullptr;
            row.widget = ConfigMenuWidget::Text;
        }
    }

    if (row.widget == ConfigMenuWidget::Slider &&
        (row.type == ConfigMenuValueType::Int || row.type == ConfigMenuValueType::Float)) {
        if (hint->constraints.maxValue > hint->constraints.minValue) {
            row.sliderMin = hint->constraints.minValue;
            row.sliderMax = hint->constraints.maxValue;
            row.sliderStep = hint->constraints.step > 0.0f ? hint->constraints.step : 1.0f;
        } else {
            row.widget = ConfigMenuWidget::Text;
        }
    }
}

bool ConfigMenuModel::appendText_(char* out, size_t outLen, size_t& pos, const char* text) const
{
    if (!out || outLen == 0 || !text) return false;
    const int n = snprintf(out + pos, outLen - pos, "%s", text);
    if (n <= 0) return false;
    pos += (size_t)n;
    if (pos >= outLen) {
        out[outLen - 1] = '\0';
        return false;
    }
    return true;
}

void ConfigMenuModel::buildBreadcrumb_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    size_t pos = 0;

    if (!appendText_(out, outLen, pos, "config")) return;
    if (isHome()) return;
    if (!appendText_(out, outLen, pos, " > ")) return;

    const char* p = currentModule_;
    while (*p != '\0') {
        if (*p == '/') {
            if (!appendText_(out, outLen, pos, " > ")) return;
            ++p;
            continue;
        }
        char c[2]{*p, '\0'};
        if (!appendText_(out, outLen, pos, c)) return;
        ++p;
    }

    if (mode_ == ConfigMenuMode::Edit) {
        (void)appendText_(out, outLen, pos, " [EDIT]");
    }
}
