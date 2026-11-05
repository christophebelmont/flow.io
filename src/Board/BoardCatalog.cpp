#include "Board/BoardCatalog.h"

#include "App/BuildFlags.h"
#include "Board/FlowIODINBoard.h"
#include "Board/MicronovaBoard.h"
#include "Board/SupervisorBoardRev1.h"
#include "Board/WaveshareBoard.h"

namespace BoardCatalog {

const BoardSpec& flowIODINv1()
{
    return BoardProfiles::kFlowIODINv1;
}

const BoardSpec& flowIODINv1S3()
{
    return BoardProfiles::kFlowIODINv1S3;
}

const BoardSpec& waveshareESP32S3()
{
    return BoardProfiles::kWaveshareESP32S3;
}

const BoardSpec& flowIODINv2()
{
    return BoardProfiles::kFlowIODINv2;
}

const BoardSpec& supervisorBoardRev1()
{
    return BoardProfiles::kSupervisorBoardRev1;
}

const BoardSpec& micronovaBoardRev1()
{
    return BoardProfiles::kMicronovaBoardRev1;
}

const BoardSpec& activeBoard()
{
#if FLOW_BUILD_IS_FLOWIO
#if defined(FLOW_BOARD_FLOWIO_S3)
    return flowIODINv1S3();
#else
    return flowIODINv1();
#endif
#elif FLOW_BUILD_IS_WAVESHARE
    return waveshareESP32S3();
#elif FLOW_BUILD_IS_SUPERVISOR
    return supervisorBoardRev1();
#elif FLOW_BUILD_IS_MICRONOVA
    return micronovaBoardRev1();
#else
    return flowIODINv1();
#endif
}

}  // namespace BoardCatalog
