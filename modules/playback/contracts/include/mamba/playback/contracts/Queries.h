#pragma once

#include <mamba/playback/contracts/Commands.h>

namespace mamba::playback::contracts {

/// 读播放器状态。返回类型是 PlayerSnapshot。
struct GetPlayerSnapshot {};

}  // namespace mamba::playback::contracts
