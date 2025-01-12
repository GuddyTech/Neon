#pragma once

#include "Neon/core/core.h"
#include "Neon/core/types/Macros.h"
#include "Neon/domain/interface/FieldBaseTemplate.h"
#include "Neon/set/DevSet.h"
#include "Neon/set/HuOptions.h"


namespace Neon::domain::details::experimental::staggeredGrid::details {

struct NodeToVoxelMask
{
    static constexpr std::array<std::array<int8_t, 3>, 8> directions{
        {{{-1, -1, -1}},
         {{-1, -1, 1}},
         {{-1, 1, -1}},
         {{-1, 1, 1}},
         {{1, -1, -1}},
         {{1, -1, 1}},
         {{1, 1, -1}},
         {{1, 1, 1}}}};

    NodeToVoxelMask() = default;
    explicit NodeToVoxelMask(uint8_t);

    auto reset()
        -> void;

    NEON_CUDA_HOST_DEVICE
    auto isNeighbourVoxelValid(int8_t a, int8_t b, int8_t c)
        const -> bool;

    template <int8_t a,
              int8_t b,
              int8_t c>
    NEON_CUDA_HOST_DEVICE inline auto isNeighbourVoxelValid()
        const -> bool
    {
        constexpr size_t jump = (a + 1) * 4 / 2 +
                                (b + 1) * 2 / 2 +
                                (c + 1) / 2;
        const uint32_t nodeFlags = activeNodesOfVoxel;
        const uint32_t mask = 1 << jump;
        const uint32_t masked = nodeFlags & mask;

        return masked != 0;
    }

    auto setAsValid(int8_t a, int8_t b, int8_t c)
        -> void;

   private:
    // will usually occupy 2 bytes:
    // 1 bits: 1 is the voxel if active
    // 8 bits: 1 if from a voxel prospective the ith-node is active
    unsigned char activeNodesOfVoxel : 8;
};


}  // namespace Neon::domain::details::experimental::staggeredGrid::details
