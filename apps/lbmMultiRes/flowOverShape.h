#pragma once
#include "Neon/Neon.h"
#include "Neon/domain/mGrid.h"
#include "lbmMultiRes.h"

#include "init.h"

#include "igl/AABB.h"
#include "igl/max.h"
#include "igl/min.h"
#include "igl/read_triangle_mesh.h"
#include "igl/remove_unreferenced.h"
#include "igl/signed_distance.h"
#include "igl/writeOBJ.h"

template <typename T, int Q, typename sdfT>
void initFlowOverShape(Neon::domain::mGrid&                  grid,
                       Neon::domain::mGrid::Field<float>&    sumStore,
                       Neon::domain::mGrid::Field<T>&        fin,
                       Neon::domain::mGrid::Field<T>&        fout,
                       Neon::domain::mGrid::Field<CellType>& cellType,
                       Neon::domain::mGrid::Field<T>&        vel,
                       Neon::domain::mGrid::Field<T>&        rho,
                       const Neon::double_3d                 inletVelocity,
                       const sdfT                            shapeSDF)
{

    const Neon::index_3d gridDim = grid.getDimension();

    //init fields
    for (int level = 0; level < grid.getDescriptor().getDepth(); ++level) {

        auto container =
            grid.newContainer(
                "Init_" + std::to_string(level), level,
                [&fin, &fout, &cellType, &vel, &rho, &sumStore, level, gridDim, inletVelocity, shapeSDF](Neon::set::Loader& loader) {
                    auto&   in = fin.load(loader, level, Neon::MultiResCompute::MAP);
                    auto&   out = fout.load(loader, level, Neon::MultiResCompute::MAP);
                    auto&   type = cellType.load(loader, level, Neon::MultiResCompute::MAP);
                    auto&   u = vel.load(loader, level, Neon::MultiResCompute::MAP);
                    auto&   rh = rho.load(loader, level, Neon::MultiResCompute::MAP);
                    auto&   ss = sumStore.load(loader, level, Neon::MultiResCompute::MAP);
                    const T usqr = (3.0 / 2.0) * (inletVelocity.x * inletVelocity.x + inletVelocity.y * inletVelocity.y + inletVelocity.z * inletVelocity.z);


                    Neon::domain::mGrid::Partition<int8_t> sdf;
                    if constexpr (std::is_same_v<sdfT, Neon::domain::mGrid::Field<int8_t>>) {
                        sdf = shapeSDF.load(loader, level, Neon::MultiResCompute::MAP);
                    }

                    return [=] NEON_CUDA_HOST_DEVICE(const typename Neon::domain::mGrid::Idx& cell) mutable {
                        //velocity and density
                        (void)sdf;
                        (void)shapeSDF;

                        u(cell, 0) = 0;
                        u(cell, 1) = 0;
                        u(cell, 2) = 0;
                        rh(cell, 0) = 0;
                        type(cell, 0) = CellType::bulk;

                        for (int q = 0; q < Q; ++q) {
                            ss(cell, q) = 0;
                            in(cell, q) = 0;
                            out(cell, q) = 0;
                        }

                        if (!in.hasChildren(cell)) {
                            const Neon::index_3d idx = in.getGlobalIndex(cell);

                            if (idx.x == 0) {
                                type(cell, 0) = CellType::inlet;
                            }

                            if constexpr (std::is_same_v<sdfT, Neon::domain::mGrid::Field<int8_t>>) {
                                if (sdf(cell) == 1) {
                                    type(cell) = CellType::bounceBack;
                                }
                            } else {
                                if (shapeSDF(idx)) {
                                    type(cell) = CellType::bounceBack;
                                }
                            }

                            //if (idx.x == gridDim.x - (1 << level)) {
                            //    type(cell, 0) = CellType::outlet;
                            //}

                            //the cell classification
                            if (idx.y == 0 || idx.y == gridDim.y - (1 << level) ||
                                idx.z == 0 || idx.z == gridDim.z - (1 << level)) {
                                type(cell, 0) = CellType::bounceBack;
                            }

                            //population init value
                            for (int q = 0; q < Q; ++q) {
                                T pop_init_val = latticeWeights[q];

                                //bounce back
                                if (type(cell, 0) == CellType::bounceBack) {
                                    pop_init_val = 0;
                                }

                                if (type(cell, 0) == CellType::inlet) {
                                    pop_init_val = 0;

                                    for (int d = 0; d < 3; ++d) {
                                        pop_init_val += latticeVelocity[q][d] * inletVelocity.v[d];
                                    }
                                    pop_init_val *= -6. * latticeWeights[q];
                                }

                                out(cell, q) = pop_init_val;
                                in(cell, q) = pop_init_val;
                            }
                        }
                    };
                });

        container.run(0);
    }


    //init sumStore
    initSumStore<T, Q>(grid, sumStore);
}

template <typename T, int Q>
void flowOverJet(const Neon::Backend backend,
                 const Params&       params)
{
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);

    Neon::index_3d gridDim(19 * params.scale, 8 * params.scale, 8 * params.scale);

    Neon::index_3d jetBoxDim(2 * params.scale, 2 * params.scale, 2 * params.scale);
    Neon::index_3d jetBoxPosition(3 * params.scale, 3 * params.scale, 3 * params.scale);

    int depth = 3;

    const Neon::mGridDescriptor<1> descriptor(depth);

    Neon::domain::mGrid grid(
        backend, gridDim,
        {[&](const Neon::index_3d idx) -> bool {
             return idx.x >= 2 * params.scale && idx.x < 7 * params.scale &&
                    idx.y >= 3 * params.scale && idx.y < 5 * params.scale &&
                    idx.z >= 3 * params.scale && idx.z < 5 * params.scale;
         },
         [&](const Neon::index_3d idx) -> bool {
             return idx.x >= params.scale && idx.x < 11 * params.scale &&
                    idx.y >= 2 * params.scale && idx.y < 6 * params.scale &&
                    idx.z >= 2 * params.scale && idx.z < 6 * params.scale;
         },
         [&](const Neon::index_3d idx) -> bool {
             return true;
         }},
        Neon::domain::Stencil::s19_t(false), descriptor);


    //LBM problem
    const T               uin = 0.04;
    const T               clength = T(grid.getDimension(descriptor.getDepth() - 1).x);
    const T               visclb = uin * clength / static_cast<T>(params.Re);
    const T               omega = 1.0 / (3. * visclb + 0.5);
    const Neon::double_3d inletVelocity(uin, 0., 0.);

    //auto test = grid.newField<T>("test", 1, 0);
    //test.ioToVtk("Test", true, true, true, true, {-1, -1, 1});
    //exit(0);

    //allocate fields
    auto fin = grid.newField<T>("fin", Q, 0);
    auto fout = grid.newField<T>("fout", Q, 0);
    auto storeSum = grid.newField<float>("storeSum", Q, 0);
    auto cellType = grid.newField<CellType>("CellType", 1, CellType::bulk);

    auto vel = grid.newField<T>("vel", 3, 0);
    auto rho = grid.newField<T>("rho", 1, 0);

    //init fields
    initFlowOverShape<T, Q>(grid, storeSum, fin, fout, cellType, vel, rho, inletVelocity, [=] NEON_CUDA_HOST_DEVICE(Neon::index_3d idx) {
        idx.x -= jetBoxPosition.x;
        idx.y -= jetBoxPosition.y;
        idx.z -= jetBoxPosition.z;
        if (idx.x < 0 || idx.y < 0 || idx.z < 0) {
            return false;
        }

        idx.x = (jetBoxDim.x / 2) - (idx.x - (jetBoxDim.x / 2));
        return sdfJetfighter(glm::ivec3(idx.z, idx.y, idx.x), glm::ivec3(jetBoxDim.x, jetBoxDim.y, jetBoxDim.z)) <= 0;

        //Neon::index_4d sphere(jetBoxPosition.x + jetBoxDim.x / 2, jetBoxPosition.y + jetBoxDim.y / 2, jetBoxPosition.z + jetBoxDim.z / 2, jetBoxDim.x / 4);
        //const T dx = sphere.x - idx.x;
        //const T dy = sphere.y - idx.y;
        //const T dz = sphere.z - idx.z;
        //if ((dx * dx + dy * dy + dz * dz) < sphere.w * sphere.w) {
        //    return true;
        //} else {
        //    return false;
        //}
    });

    //cellType.updateHostData();
    //cellType.ioToVtk("cellType", true, true, true, true);

    runNonUniformLBM<T, Q>(grid,
                           params,
                           clength,
                           omega,
                           visclb,
                           inletVelocity,
                           cellType,
                           storeSum,
                           fin,
                           fout,
                           vel,
                           rho);
}

template <typename T, int Q>
void flowOverSphere(const Neon::Backend backend,
                    const Params&       params)
{
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);


    Neon::index_3d gridDim(136 * params.scale, 96 * params.scale, 136 * params.scale);

    Neon::index_4d sphere(52 * params.scale, 52 * params.scale, 68 * params.scale, 8 * params.scale);

    int depth = 3;

    const Neon::mGridDescriptor<1> descriptor(depth);

    Neon::domain::mGrid grid(
        backend, gridDim,
        {[&](const Neon::index_3d idx) -> bool {
             return idx.x >= 40 * params.scale && idx.x < 96 * params.scale && idx.y >= 40 * params.scale && idx.y < 64 * params.scale && idx.z >= 40 * params.scale && idx.z < 96 * params.scale;
         },
         [&](const Neon::index_3d idx) -> bool {
             return idx.x >= 24 * params.scale && idx.x < 112 * params.scale && idx.y >= 24 * params.scale && idx.y < 72 * params.scale && idx.z >= 24 * params.scale && idx.z < 112 * params.scale;
         },
         [&](const Neon::index_3d idx) -> bool {
             return true;
         }},
        Neon::domain::Stencil::s19_t(false), descriptor);


    //LBM problem
    const T uin = 0.04;
    //const T               clength = T(grid.getDimension(descriptor.getDepth() - 1).x);
    const T               clength = T(sphere.w / (1 << (depth - 1)));
    const T               visclb = uin * clength / static_cast<T>(params.Re);
    const T               omega = 1.0 / (3. * visclb + 0.5);
    const Neon::double_3d inletVelocity(uin, 0., 0.);

    //auto test = grid.newField<T>("test", 1, 0);
    //test.ioToVtk("Test", true, true, true, false);
    //exit(0);

    //allocate fields
    auto fin = grid.newField<T>("fin", Q, 0);
    auto fout = grid.newField<T>("fout", Q, 0);
    auto storeSum = grid.newField<float>("storeSum", Q, 0);
    auto cellType = grid.newField<CellType>("CellType", 1, CellType::bulk);

    auto vel = grid.newField<T>("vel", 3, 0);
    auto rho = grid.newField<T>("rho", 1, 0);

    //init fields
    initFlowOverShape<T, Q>(grid, storeSum, fin, fout, cellType, vel, rho, inletVelocity, [sphere] NEON_CUDA_HOST_DEVICE(const Neon::index_3d idx) {
        const T dx = sphere.x - idx.x;
        const T dy = sphere.y - idx.y;
        const T dz = sphere.z - idx.z;

        if ((dx * dx + dy * dy + dz * dz) < sphere.w * sphere.w) {
            return true;
        } else {
            return false;
        }
    });

    //cellType.updateHostData();
    //cellType.ioToVtk("cellType", true, true, true, true);

    runNonUniformLBM<T, Q>(grid,
                           params,
                           clength,
                           omega,
                           visclb,
                           inletVelocity,
                           cellType,
                           storeSum,
                           fin,
                           fout,
                           vel,
                           rho);
}


template <typename T, int Q>
void flowOverMesh(const Neon::Backend backend,
                  const Params&       params)
{
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);

    //define the gird and the box that will encompass the mesh
    Neon::index_3d gridDim(19 * params.scale, 10 * params.scale, 10 * params.scale);

    Eigen::RowVector3d meshBoxDim(2 * params.scale, 2 * params.scale, 2 * params.scale);
    Eigen::RowVector3d meshBoxCenter(4 * params.scale, 5 * params.scale, 5 * params.scale);


    //read the mesh and scale it such that it fits inside meshBox
    Eigen::MatrixXi faces;
    Eigen::MatrixXd vertices;
    igl::read_triangle_mesh(params.meshFile, vertices, faces);

    //remove unreferenced vertices because they may affect the scaling
    Eigen::VectorXi _1, _2;
    igl::remove_unreferenced(Eigen::MatrixXd(vertices), Eigen::MatrixXi(faces), vertices, faces, _1, _2);

    //mesh bounding box using the mesh coordinates
    Eigen::RowVector3d bbMax, bbMin;
    Eigen::RowVector3i bbMaxI, bbMinI;
    igl::max(vertices, 1, bbMax, bbMaxI);
    igl::min(vertices, 1, bbMin, bbMinI);

    //translate and scale the mesh
    vertices.rowwise() -= ((bbMin + bbMax) / 2.0);
    double scaling_factor = meshBoxDim.minCoeff() / (bbMax - bbMin).minCoeff();
    vertices *= scaling_factor;
    vertices.rowwise() += meshBoxCenter;


    igl::writeOBJ("scaled.obj", vertices, faces);

    polyscopeAddMesh(params.meshFile, faces, vertices);

    //get the mesh ready for query (inside/outside) using libigl winding number
    igl::AABB<Eigen::MatrixXd, 3> tree;
    tree.init(vertices, faces);
    igl::FastWindingNumberBVH fwn_bvh;
    igl::fast_winding_number(vertices, faces, 2, fwn_bvh);

    //define the grid
    int                            depth = 3;
    const Neon::mGridDescriptor<1> descriptor(depth);

    Neon::domain::mGrid grid(
        backend, gridDim,
        {[&](const Neon::index_3d idx) -> bool {
             return idx.x >= 2 * params.scale && idx.x < 8 * params.scale &&
                    idx.y >= 3 * params.scale && idx.y < 7 * params.scale &&
                    idx.z >= 3 * params.scale && idx.z < 7 * params.scale;
         },
         [&](const Neon::index_3d idx) -> bool {
             return idx.x >= params.scale && idx.x < 13 * params.scale &&
                    idx.y >= 2 * params.scale && idx.y < 8 * params.scale &&
                    idx.z >= 2 * params.scale && idx.z < 8 * params.scale;
         },
         [&](const Neon::index_3d idx) -> bool {
             return true;
         }},
        Neon::domain::Stencil::s19_t(false), descriptor);


    //LBM problem
    const T uin = 0.04;
    const T clength = T(meshBoxDim.minCoeff());
    //const T               clength = T(grid.getDimension(descriptor.getDepth() - 1).x);
    const T               visclb = uin * clength / static_cast<T>(params.Re);
    const T               omega = 1.0 / (3. * visclb + 0.5);
    const Neon::double_3d inletVelocity(uin, 0., 0.);

    //auto test = grid.newField<T>("test", 1, 0);
    //test.ioToVtk("Test", true, true, true, true, {-1, -1, 1});
    //exit(0);

    //a field with 1 if the voxel is inside the shape
    auto inside = grid.newField<int8_t>("inside", 1, 0);

    for (int l = 0; l < grid.getDescriptor().getDepth(); ++l) {
        grid.newContainer<Neon::Execution::host>("isInside", l, [&](Neon::set::Loader& loader) {
                auto& in = inside.load(loader, l, Neon::MultiResCompute::MAP);

                return [&](const typename Neon::domain::mGrid::Idx& cell) mutable {
                    if (!in.hasChildren(cell)) {
                        const double       voxelSpacing = 0.5 * double(grid.getDescriptor().getSpacing(l - 1));
                        Neon::index_3d     voxelGlobalLocation = in.getGlobalIndex(cell);
                        Eigen::RowVector3d point(voxelGlobalLocation.x + voxelSpacing, voxelGlobalLocation.y + voxelSpacing, voxelGlobalLocation.z + voxelSpacing);
                        in(cell, 0) = int8_t(igl::signed_distance_fast_winding_number(point, vertices, faces, tree, fwn_bvh) <= 0);
                    }
                };
            })
            .run(0);
    }
    grid.getBackend().syncAll();

    inside.updateDeviceData();

    //inside.ioToVtk("inside", true, true, true, true);
    //exit(0);

    //allocate fields
    auto fin = grid.newField<T>("fin", Q, 0);
    auto fout = grid.newField<T>("fout", Q, 0);
    auto storeSum = grid.newField<float>("storeSum", Q, 0);
    auto cellType = grid.newField<CellType>("CellType", 1, CellType::bulk);

    auto vel = grid.newField<T>("vel", 3, 0);
    auto rho = grid.newField<T>("rho", 1, 0);


    //init fields
    initFlowOverShape<T, Q>(grid, storeSum, fin, fout, cellType, vel, rho, inletVelocity, inside);

    //cellType.updateHostData();
    //cellType.ioToVtk("cellType", true, true, true, true);
    //exit(0);

    runNonUniformLBM<T, Q>(grid,
                           params,
                           clength,
                           omega,
                           visclb,
                           inletVelocity,
                           cellType,
                           storeSum,
                           fin,
                           fout,
                           vel,
                           rho);
}