#pragma once

#include "App/BuildFlags.h"
#include "Board/FlowIODINBoard.h"
#include "Board/MicronovaBoard.h"
#include "Board/SupervisorBoardRev1.h"
#include "Board/WaveshareBoard.h"

namespace BoardCapacityProfile {

inline constexpr const BoardSpec& buildBoard()
{
#if FLOW_BUILD_IS_FLOWIO
    return BoardProfiles::kFlowIODINv1;
#elif FLOW_BUILD_IS_WAVESHARE
    return BoardProfiles::kWaveshareESP32S3;
#elif FLOW_BUILD_IS_SUPERVISOR
    return BoardProfiles::kSupervisorBoardRev1;
#elif FLOW_BUILD_IS_FLOW_CONNECT_DISPLAY
    return BoardProfiles::kFlowIODINv1;
#elif FLOW_BUILD_IS_MICRONOVA
    return BoardProfiles::kMicronovaBoardRev1;
#else
    return BoardProfiles::kFlowIODINv1;
#endif
}

inline constexpr IoCapacitySpec kIoCapacity = buildBoard().ioCapacity;
inline constexpr MqttCapacitySpec kMqttCapacity = buildBoard().mqttCapacity;
inline constexpr MqttBufferSpec kMqttBuffers = buildBoard().mqttBuffers;
inline constexpr HaCapacitySpec kHaCapacity = buildBoard().haCapacity;

}  // namespace BoardCapacityProfile
