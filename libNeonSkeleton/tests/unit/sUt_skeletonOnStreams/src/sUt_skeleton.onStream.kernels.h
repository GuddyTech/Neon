#pragma once

#include <functional>

#include "Neon/domain/aGrid.h"
#include "Neon/domain/dGrid.h"
#include "Neon/domain/eGrid.h"
#include "Neon/set/Containter.h"
#include "Neon/set/container/ContainerAPI.h"
#include "Neon/skeleton/Skeleton.h"
#include "gtest/gtest.h"
#include "sUt.runHelper.h"

namespace UserTools {
template <typename Field>
auto xpy(const Field& x,
         Field&       y) -> Neon::set::Container
{
    auto Kontainer = x.getGrid().newContainer(
        "xpy", [&](Neon::set::Loader& L) -> auto {
            auto& xLocal = L.load(x);
            auto& yLocal = L.load(y);
            return [=] NEON_CUDA_HOST_DEVICE(const typename Field::Cell& e) mutable {
                for (int i = 0; i < yLocal.cardinality(); i++) {
                    yLocal(e, i) += xLocal(e, i);
                }
            };
        });
    return Kontainer;
}

template <typename Field, typename T>
auto aInvXpY(const Neon::template PatternScalar<T>& fR,
             const Field&                           x,
             Field&                                 y) -> Neon::set::Container
{
    auto Kontainer = x.getGrid().newContainer(
        "AXPY", [&](Neon::set::Loader& L) -> auto {
            auto&      xLocal = L.load(x);
            auto&      yLocal = L.load(y);
            auto       fRLocal = L.load(fR);
            const auto fRVal = fRLocal();
            return [=] NEON_CUDA_HOST_DEVICE(const typename Field::Cell& e) mutable {
                // printf("%d yLocal.cardinality()\n", yLocal.cardinality());

                for (int i = 0; i < yLocal.cardinality(); i++) {
                    // printf("%d %d (%d) x\n", e, xLocal(e, i), i);
                    yLocal(e, i) += (1.0 / fRVal) * xLocal(e, i);
                }
            };
        });
    return Kontainer;
}

template <typename Field, typename T>
auto axpy(const Neon::template PatternScalar<T>& fR,
          const Field&                           x,
          Field&                                 y) -> Neon::set::Container
{
    auto c = x.getGrid().newContainer(
        "AXPY", [&](Neon::set::Loader& L) -> auto {
            auto&      xLocal = L.load(x);
            auto&      yLocal = L.load(y);
            auto       fRLocal = L.load(fR);
            const auto fRVal = fRLocal();
            return [=] NEON_CUDA_HOST_DEVICE(const typename Field::Idx& e) mutable {
                // #ifdef NEON_PLACE_CUDA_DEVICE
                //                 if(yLocal.prtID()==0) {
                //                     __nanosleep(2000000U);
                //                 }
                // #endif

                for (int i = 0; i < yLocal.cardinality(); i++) {
                    yLocal(e, i) += fRVal * xLocal(e, i);
                }
            };
        });
    return c;
}

template <typename Field>
auto laplace(const Field& x,
             Field&       y) -> Neon::set::Container
{
    Neon::set::Container container = x.getGrid().newContainer(
        "Laplace", [&](Neon::set::Loader& L) -> auto {
            auto& xLocal = L.load(x, Neon::Pattern::STENCIL);
            auto& yLocal = L.load(y);

            return [=] NEON_CUDA_HOST_DEVICE(const typename Field::Idx& gidx) mutable {
                using Type = typename Field::Type;
                for (int card = 0; card < xLocal.cardinality(); card++) {
                    typename Field::Type res = 0;
                    int                  count = 0;

                    auto checkNeighbor = [&res, &count](Neon::domain::NghData<Type>& neighbor) {
                        if (neighbor.isValid()) {
                            res += neighbor.getData();
                            count++;
                        }
                    };

                    // Laplacian stencil operates on 6 neighbors (assuming 3D)
                    if constexpr (std::is_same<typename Field::Grid, Neon::domain::details::eGrid::eGrid>::value) {
                        for (int8_t nghIdx = 0; nghIdx < 6; ++nghIdx) {
                            auto neighbor = xLocal.getNghData(gidx, nghIdx, card);
                            checkNeighbor(neighbor);
                        }
                    } else {
                        typename Field::Partition::NghIdx ngh(0, 0, 0);

                        //+x
                        ngh.x = 1;
                        ngh.y = 0;
                        ngh.z = 0;
                        auto neighbor = xLocal.getNghData(gidx, ngh, card, Type(0));
                        checkNeighbor(neighbor);

                        //-x
                        ngh.x = -1;
                        ngh.y = 0;
                        ngh.z = 0;
                        neighbor = xLocal.getNghData(gidx, ngh, card, Type(0));
                        checkNeighbor(neighbor);

                        //+y
                        ngh.x = 0;
                        ngh.y = 1;
                        ngh.z = 0;
                        neighbor = xLocal.getNghData(gidx, ngh, card, Type(0));
                        checkNeighbor(neighbor);

                        //-y
                        ngh.x = 0;
                        ngh.y = -1;
                        ngh.z = 0;
                        neighbor = xLocal.getNghData(gidx, ngh, card, Type(0));
                        checkNeighbor(neighbor);

                        //+z
                        ngh.x = 0;
                        ngh.y = 0;
                        ngh.z = 1;
                        neighbor = xLocal.getNghData(gidx, ngh, card, Type(0));
                        checkNeighbor(neighbor);

                        //-z
                        ngh.x = 0;
                        ngh.y = 0;
                        ngh.z = -1;
                        neighbor = xLocal.getNghData(gidx, ngh, card, Type(0));
                        checkNeighbor(neighbor);
                    }


                    yLocal(gidx, card) = xLocal(gidx, card) - count * res;
                }
            };
        });
    return container;
}

}  // namespace UserTools